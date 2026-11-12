#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "app.h"

// Ablauf:
//  - Kein WLAN gespeichert oder Verbindung fehlgeschlagen -> eigener Hotspot
//    "E-Ink-Setup-XXXX" mit WLAN-Auswahl (Captive Portal).
//  - Im WLAN, aber noch nicht eingerichtet -> Display zeigt http://eink.local,
//    dort fuehrt ein Assistent durch Haltestelle, Richtung und Wetter.
//  - Danach Normalbetrieb: Abfahrten, Wetter, Musik (iOS-App) und Notiz.

WebServer server(80);
static DNSServer dnsServer;

Mode mode = Mode::Normal;
String portalSsid = "";
String portalError = "";
volatile bool isBusy = false;
unsigned long restartAt = 0;

static volatile bool needsUpdate = false;
static unsigned long lastRequestMillis = 0;
static const unsigned long DEBOUNCE_MS = 800;

static unsigned long lastTramFetch = 0;
static unsigned long lastWeatherFetch = 0;
static bool tramFetchedOnce = false;
static bool weatherFetchedOnce = false;
static bool drawnOnce = false;
static unsigned long lastDrawMillis = 0;
static unsigned long lastSignatureCheck = 0;
static unsigned long lastNoteCheck = 0;
static String lastSignature = "";
static unsigned long portalStarted = 0;
static unsigned long resetPressedSince = 0;

static const int RESET_BUTTON_PIN = 0;   // BOOT-Taste
static const unsigned long RESET_HOLD_MS = 10000;

void requestRedraw() {
  needsUpdate = true;
  lastRequestMillis = millis();
}

void requestDataFetch() {
  tramFetchedOnce = false;
  weatherFetchedOnce = false;
}

/// Hotspot-Name mit den letzten MAC-Stellen, damit mehrere Geraete unterscheidbar sind.
String setupApName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  return String(SETUP_AP_PREFIX) + "-" + suffix;
}

// ================= WLAN =================

static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  Serial.printf("Verbinde mit WLAN '%s'", cfg.wifiSsid.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 30000) {
      Serial.println(" fehlgeschlagen");
      return false;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Verbunden! IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static void startPortal(const String& error) {
  mode = Mode::Portal;
  portalError = error;
  portalSsid = setupApName();

  // Erst nach WLANs suchen, dann den Hotspot oeffnen
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  scanNetworks();
  WiFi.mode(WIFI_AP_STA);   // STA bleibt fuer "Neu suchen" aktiv
  WiFi.softAP(portalSsid.c_str());
  delay(100);

  // Captive Portal: alle DNS-Anfragen auf uns, damit sich die Seite von selbst oeffnet
  dnsServer.start(53, "*", WiFi.softAPIP());
  setupPortalRoutes();
  server.begin();
  portalStarted = millis();
  Serial.printf("Hotspot '%s' offen: http://%s\n", portalSsid.c_str(), WiFi.softAPIP().toString().c_str());

  lastSignature = displaySignature();
  updateDisplay();
  drawnOnce = true;
}

static void startStation() {
  mode = cfg.setupDone ? Mode::Normal : Mode::Setup;

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("Erreichbar als " HOSTNAME ".local");
  }

  // Zeitzone Berlin inkl. Sommerzeit
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
  Serial.print("Warte auf NTP");
  unsigned long start = millis();
  while (!timeValid() && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println(timeValid() ? " ok" : " fehlgeschlagen, versuche es weiter");

  setupNormalRoutes();
  server.begin();
  Serial.println(mode == Mode::Setup ? "Webserver laeuft - Einrichtung offen." : "Webserver laeuft.");
}

/// PIN vergessen? BOOT-Taste 10 s halten setzt alles zurueck.
static void checkResetButton() {
  if (digitalRead(RESET_BUTTON_PIN) != LOW) {
    resetPressedSince = 0;
    return;
  }
  if (resetPressedSince == 0) {
    resetPressedSince = millis();
  } else if (millis() - resetPressedSince >= RESET_HOLD_MS) {
    Serial.println("BOOT-Taste gehalten - Werkseinstellungen");
    factoryReset();
    ESP.restart();
  }
}

// ================= Setup / Loop =================

void setup() {
  Serial.begin(115200);
  Serial.printf("\nE-Ink Firmware %s\n", FIRMWARE_VERSION);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  initDisplay();
  loadSettings();
  loadNote();

  if (cfg.wifiSsid.length() == 0) {
    startPortal("");
  } else if (!connectWifi()) {
    startPortal("Verbindung zu \"" + cfg.wifiSsid + "\" fehlgeschlagen. Passwort pr\xc3\xbc" "fen oder anderes WLAN w\xc3\xa4hlen.");
  } else {
    startStation();
  }
}

static void portalLoop() {
  dnsServer.processNextRequest();
  server.handleClient();

  // Gespeichertes WLAN spaeter erneut versuchen, falls nur der Router kurz weg war
  if (cfg.wifiSsid.length() > 0 && millis() - portalStarted > PORTAL_TIMEOUT_MS &&
      WiFi.softAPgetStationNum() == 0) {
    Serial.println("Hotspot-Timeout, versuche gespeichertes WLAN erneut");
    ESP.restart();
  }
}

void loop() {
  if (restartAt > 0 && millis() > restartAt) ESP.restart();
  checkResetButton();

  if (mode == Mode::Portal) {
    portalLoop();
    return;
  }

  server.handleClient();

  if (mode == Mode::Normal && WiFi.status() == WL_CONNECTED && timeValid()) {
    if (!weatherFetchedOnce || millis() - lastWeatherFetch >= WEATHER_INTERVAL_MS) {
      // Bei Fehler nach 1 min erneut versuchen statt erst nach dem vollen Intervall
      bool ok = fetchWeather();
      lastWeatherFetch = ok ? millis() : millis() - WEATHER_INTERVAL_MS + 60000;
      weatherFetchedOnce = true;
    }
    if (!tramFetchedOnce || millis() - lastTramFetch >= TRAM_INTERVAL_MS) {
      tramError = !fetchTrams();
      if (!tramError) tramStand = formatTime(time(nullptr));
      lastTramFetch = millis();
      tramFetchedOnce = true;
    }
    otaLoop();
  }

  if (millis() - lastNoteCheck >= 30000) {
    lastNoteCheck = millis();
    checkNoteExpiry();
  }

  bool requested = needsUpdate && (millis() - lastRequestMillis >= DEBOUNCE_MS);
  // Erstes Bild: Einrichtung sofort, sonst sobald Daten da sind - spaetestens nach 30 s
  bool firstDraw = !drawnOnce && (mode == Mode::Setup || tramFetchedOnce || millis() > 30000);

  if (requested || firstDraw) {
    needsUpdate = false;
  } else {
    // Sonst hoechstens alle cfg.displayMinutes und nur wenn sich etwas geaendert hat.
    // Ein 3-Farb-Refresh dauert ~15 s und flackert.
    if (millis() - lastDrawMillis < (unsigned long)cfg.displayMinutes * 60000UL) return;
    if (millis() - lastSignatureCheck < 5000) return;
    lastSignatureCheck = millis();
    if (displaySignature() == lastSignature) return;
  }

  lastSignature = displaySignature();   // vor dem Zeichnen, das dauert ~15 s
  updateDisplay();
  lastDrawMillis = millis();
  drawnOnce = true;
}
