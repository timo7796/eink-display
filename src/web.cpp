#include <WiFi.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "app.h"

// Web-App: im Hotspot die WLAN-Einrichtung, im WLAN der Einrichtungs-Assistent
// (Haltestelle -> Richtung -> Wetter) und danach Notiz, Status und Einstellungen.

String setupApName();
extern String otaPendingUrl;

// ================= Seitengeruest =================

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>E-Ink Display</title>
<style>
  :root { --bg:#f4f4f1; --card:#fff; --text:#1d1d1b; --muted:#6b6b66; --accent:#c8102e; --line:#e2e2dc; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#161615; --card:#222220; --text:#eeeeea; --muted:#9a9a93; --accent:#ff5a6e; --line:#33332f; }
  }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text);
         font:16px/1.4 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
  main { max-width:480px; margin:0 auto; padding:24px 16px 40px; }
  h1 { font-size:22px; margin:0 0 4px; }
  h2 { font-size:15px; margin:18px 0 8px; }
  a { color:var(--accent); }
  .step { font-size:13px; color:var(--muted); margin:0 0 16px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:12px; padding:16px; margin-bottom:16px; }
  label { display:block; font-weight:600; margin-bottom:6px; }
  .accent { color:var(--accent); }
  input[type=text], input[type=password], input[type=number], input[type=search], textarea {
    width:100%; padding:10px; font:inherit; color:var(--text); background:var(--bg);
    border:1px solid var(--line); border-radius:8px; }
  textarea { min-height:84px; resize:vertical; }
  .field { margin-bottom:14px; }
  .row { display:flex; gap:8px; margin-top:12px; }
  button { flex:1; padding:12px; font:inherit; font-weight:600; border-radius:8px; border:0; cursor:pointer;
           background:var(--accent); color:#fff; }
  button.secondary { background:transparent; color:var(--text); border:1px solid var(--line); }
  button.danger { background:transparent; color:var(--accent); border:1px solid var(--accent); }
  .opt { display:flex; gap:10px; align-items:flex-start; padding:10px; border:1px solid var(--line);
         border-radius:8px; margin-bottom:8px; font-weight:400; cursor:pointer; }
  .opt input { margin-top:4px; }
  .opt small { display:block; color:var(--muted); }
  .check { display:flex; align-items:center; gap:8px; font-weight:400; }
  .hint { font-size:13px; color:var(--muted); margin:8px 0 0; }
  .msg { font-size:14px; padding:10px 12px; border-radius:8px; background:var(--card);
         border:1px solid var(--line); margin-bottom:16px; }
  .err { color:var(--accent); }
  .kv div { display:flex; justify-content:space-between; gap:12px; padding:4px 0; }
  .kv span:first-child { color:var(--muted); white-space:nowrap; }
  .kv span:last-child { text-align:right; }
  summary { font-weight:600; cursor:pointer; }
  details[open] summary { margin-bottom:8px; }
</style>
</head><body><main>
)HTML";

static String pageStart() {
  String html;
  html.reserve(6000);
  html += FPSTR(PAGE_HEAD);
  return html;
}

static void sendPage(String& html) {
  html += "</main></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

static void redirectTo(const String& location) {
  server.sendHeader("Location", location);
  server.send(303);
}

static bool requirePost() {
  if (server.method() == HTTP_POST) return true;
  server.send(405, "text/plain", "Method Not Allowed");
  return false;
}

/// "Schritt 2 von 4" - nur waehrend der Ersteinrichtung.
static String stepLabel(int step, const char* name) {
  if (cfg.setupDone) return "";
  return "<p class=\"step\">Schritt " + String(step) + " von 4 \xc2\xb7 " + name + "</p>";
}

static String radio(const String& name, const String& value, bool checked,
                    const String& title, const String& subtitle = "") {
  String h = "<label class=\"opt\"><input type=\"radio\" name=\"" + name + "\" value=\"" + htmlEscape(value) + "\"";
  if (checked) h += " checked";
  h += "><span>" + title;
  if (subtitle.length() > 0) h += "<small>" + subtitle + "</small>";
  h += "</span></label>";
  return h;
}

// ================= Hotspot: WLAN-Einrichtung =================

struct WifiNetwork {
  String ssid;
  int rssi;
  bool secure;
};
static const int MAX_NETWORKS = 20;
static WifiNetwork networks[MAX_NETWORKS];
static int networkCount = 0;

void scanNetworks() {
  int n = WiFi.scanNetworks();
  networkCount = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    // Gleiches WLAN von mehreren Access Points: nur den staerksten behalten
    int existing = -1;
    for (int j = 0; j < networkCount; j++) {
      if (networks[j].ssid == ssid) existing = j;
    }
    if (existing >= 0) {
      if (WiFi.RSSI(i) > networks[existing].rssi) networks[existing].rssi = WiFi.RSSI(i);
      continue;
    }
    if (networkCount >= MAX_NETWORKS) continue;
    networks[networkCount++] = {ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
  }
  WiFi.scanDelete();
  // Staerkstes zuerst
  for (int i = 0; i < networkCount; i++) {
    for (int j = i + 1; j < networkCount; j++) {
      if (networks[j].rssi > networks[i].rssi) std::swap(networks[i], networks[j]);
    }
  }
  Serial.printf("%d WLANs gefunden\n", networkCount);
}

static void handlePortalRoot() {
  if (server.hasArg("rescan")) scanNetworks();

  String html = pageStart();
  html += "<h1>E-Ink einrichten</h1>" + stepLabel(1, "WLAN");
  if (portalError.length() > 0) html += "<p class=\"msg err\">" + htmlEscape(portalError) + "</p>";

  html += "<form class=\"card\" method=\"post\" action=\"/wifi\" accept-charset=\"utf-8\">"
          "<label>Mit welchem WLAN soll sich das Display verbinden?</label>";
  for (int i = 0; i < networkCount; i++) {
    const char* bars = networks[i].rssi > -60 ? "\xe2\x96\x82\xe2\x96\x84\xe2\x96\x86"   // stark
                     : networks[i].rssi > -75 ? "\xe2\x96\x82\xe2\x96\x84"               // mittel
                     : "\xe2\x96\x82";                                                  // schwach
    String sub = String(bars) + (networks[i].secure ? " \xc2\xb7 gesch\xc3\xbctzt" : " \xc2\xb7 offen");
    html += radio("ssid", networks[i].ssid, networks[i].ssid == cfg.wifiSsid || (i == 0 && cfg.wifiSsid.length() == 0),
                  htmlEscape(networks[i].ssid), sub);
  }
  if (networkCount == 0) html += "<p class=\"hint\">Keine WLANs gefunden.</p>";
  html += "<p class=\"hint\"><a href=\"/?rescan=1\">Neu suchen</a></p>"
          "<div class=\"field\" style=\"margin-top:14px\"><label for=\"manual\">Oder Name von Hand eingeben</label>"
          "<input type=\"text\" id=\"manual\" name=\"manual\" autocapitalize=\"none\" autocorrect=\"off\"></div>"
          "<div class=\"field\"><label for=\"pass\">Passwort</label>"
          "<input type=\"password\" id=\"pass\" name=\"pass\"></div>"
          "<div class=\"row\"><button type=\"submit\">Verbinden</button></div>"
          "</form>";
  sendPage(html);
}

static void handlePortalWifi() {
  if (!requirePost()) return;
  String ssid = server.arg("manual");
  ssid.trim();
  if (ssid.length() == 0) ssid = server.arg("ssid");
  if (ssid.length() == 0) {
    redirectTo("/");
    return;
  }

  cfg.wifiSsid = ssid;
  cfg.wifiPass = server.arg("pass");
  saveSettings();

  String html = pageStart();
  html += "<h1>Verbinde mit " + htmlEscape(ssid) + "\xe2\x80\xa6</h1>"
          "<div class=\"card\"><p>Das Display startet neu und verbindet sich mit deinem WLAN.</p>"
          "<p><b>So geht es weiter:</b></p><ol>"
          "<li>Verbinde dein Handy wieder mit <b>" + htmlEscape(ssid) + "</b>.</li>"
          "<li>\xc3\x96""ffne <a href=\"http://" HOSTNAME ".local\">http://" HOSTNAME ".local</a> "
          "\xe2\x80\x93 die Adresse steht auch auf dem Display.</li></ol>"
          "<p class=\"hint\">Klappt die Verbindung nicht, \xc3\xb6""ffnet das Display nach etwa einer Minute "
          "wieder diesen Hotspot.</p></div>";
  sendPage(html);
  restartAt = millis() + 3000;
}

/// Captive Portal: jede fremde Adresse auf die Einrichtung umleiten.
static void handlePortalNotFound() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
  server.send(302);
}

void setupPortalRoutes() {
  server.on("/", HTTP_GET, handlePortalRoot);
  server.on("/wifi", handlePortalWifi);
  server.onNotFound(handlePortalNotFound);
}

// ================= Einrichtungs-Assistent =================

static const int MAX_RESULTS = 8;
static StopResult stopResults[MAX_RESULTS];
static int stopResultCount = 0;
static PlaceResult placeResults[MAX_RESULTS];
static int placeResultCount = 0;

static void setupStepStop(String& html) {
  String q = server.arg("q");
  q.trim();

  html += "<h1>Haltestelle</h1>" + stepLabel(2, "Haltestelle");
  html += "<form class=\"card\" method=\"get\" action=\"/setup\">"
          "<input type=\"hidden\" name=\"step\" value=\"stop\">"
          "<label for=\"q\">Ort und Haltestelle</label>"
          "<input type=\"search\" id=\"q\" name=\"q\" placeholder=\"z.B. Augsburg Berliner Allee\" value=\"" +
          htmlEscape(q) + "\">"
          "<div class=\"row\"><button type=\"submit\">Suchen</button></div></form>";

  if (q.length() == 0) {
    if (cfg.stopName.length() > 0) html += "<p class=\"hint\">Aktuell: " + htmlEscape(cfg.stopName) + "</p>";
  } else {
    stopResultCount = searchStops(q, stopResults, MAX_RESULTS);
    if (stopResultCount < 0) {
      html += "<p class=\"msg err\">Suche fehlgeschlagen. Bitte nochmal versuchen.</p>";
    } else if (stopResultCount == 0) {
      html += "<p class=\"msg\">Nichts gefunden. Tipp: Ort mit angeben, z.B. \"Augsburg Rathausplatz\".</p>";
    } else {
      html += "<form class=\"card\" method=\"post\" action=\"/setup/stop\"><label>Treffer</label>";
      for (int i = 0; i < stopResultCount; i++) {
        html += radio("idx", String(i), i == 0, htmlEscape(stopResults[i].name));
      }
      html += "<div class=\"row\"><button type=\"submit\">Weiter</button></div></form>";
    }
  }
}

static void setupStepDirection(String& html) {
  // Linien der Haltestelle frisch laden, damit die Auswahl passt
  tramError = !fetchTrams();
  if (!tramError) tramStand = formatTime(time(nullptr));

  html += "<h1>Richtung</h1>" + stepLabel(3, "Richtung");
  html += "<form class=\"card\" method=\"post\" action=\"/setup/dir\" accept-charset=\"utf-8\">"
          "<label>Welche Abfahrten an " + htmlEscape(cfg.stopName) + " sollen angezeigt werden?</label>";

  bool matched = cfg.filter.length() == 0;
  html += radio("filter", "", cfg.filter.length() == 0, "Alle Richtungen");

  // Steige mit ihren Linien, z.B. "Bstg. e: 1 -> Goeggingen, 22 -> Augsburg Hbf"
  for (int i = 0; i < routeCount; i++) {
    String platform = routes[i].platform;
    bool seen = false;
    for (int j = 0; j < i; j++) {
      if (routes[j].platform == platform) seen = true;
    }
    if (seen || platform.length() == 0) continue;

    String lines;
    for (int j = i; j < routeCount; j++) {
      if (routes[j].platform != platform) continue;
      if (lines.length() > 0) lines += " \xc2\xb7 ";
      lines += htmlEscape(routes[j].line) + " \xe2\x86\x92 " + htmlEscape(routes[j].dest);
    }
    bool checked = cfg.filter == platform;
    if (checked) matched = true;
    html += radio("filter", platform, checked, htmlEscape(platform), lines);
  }
  if (routeCount == 0) {
    html += "<p class=\"msg err\">Abfahrten konnten nicht geladen werden" +
            (tramErrorText.length() > 0 ? String(": ") + htmlEscape(tramErrorText) : String("")) + "</p>";
  }

  html += "<div class=\"field\" style=\"margin-top:14px\"><label for=\"custom\">Oder nur Ziele, die so hei\xc3\x9f" "en</label>"
          "<input type=\"text\" id=\"custom\" name=\"custom\" placeholder=\"z.B. Hauptbahnhof\" value=\"" +
          (matched ? String("") : htmlEscape(cfg.filter)) + "\"></div>";
  html += "<label class=\"check\"><input type=\"checkbox\" name=\"onlyTrams\" value=\"1\"" +
          String(cfg.onlyTrams ? " checked" : "") + "> Nur Stra\xc3\x9f" "enbahnen (Busse ausblenden)</label>";
  html += "<p class=\"hint\">Die Liste zeigt die Linien der n\xc3\xa4" "chsten Abfahrten. Nachts oder am Wochenende "
          "fehlen evtl. Linien, die sonst fahren.</p>";
  html += "<div class=\"row\"><button type=\"submit\">" + String(cfg.setupDone ? "Speichern" : "Weiter") +
          "</button></div></form>";
}

static void setupStepWeather(String& html) {
  String q = server.arg("q");
  q.trim();

  html += "<h1>Wetter</h1>" + stepLabel(4, "Wetter");

  if (q.length() > 0) placeResultCount = searchPlaces(q, placeResults, MAX_RESULTS);

  html += "<form class=\"card\" method=\"post\" action=\"/setup/weather\"><label>F\xc3\xbcr welchen Ort?</label>";
  bool anyOption = false;
  if (q.length() > 0 && placeResultCount > 0) {
    for (int i = 0; i < placeResultCount; i++) {
      html += radio("choice", String(i), i == 0, htmlEscape(placeResults[i].name), htmlEscape(placeResults[i].region));
    }
    anyOption = true;
  } else if (q.length() > 0) {
    html += "<p class=\"msg\">" + String(placeResultCount < 0 ? "Suche fehlgeschlagen." : "Kein Ort gefunden.") + "</p>";
  }
  if (cfg.stopLat.length() > 0) {
    html += radio("choice", "stop", !anyOption, "Am Ort der Haltestelle", htmlEscape(cfg.stopName));
    anyOption = true;
  }
  if (anyOption) {
    html += "<div class=\"row\"><button type=\"submit\">" + String(cfg.setupDone ? "Speichern" : "Fertig") +
            "</button></div>";
  }
  html += "</form>";

  html += "<form class=\"card\" method=\"get\" action=\"/setup\">"
          "<input type=\"hidden\" name=\"step\" value=\"weather\">"
          "<label for=\"q\">Anderen Ort suchen</label>"
          "<input type=\"search\" id=\"q\" name=\"q\" placeholder=\"z.B. M\xc3\xbcnchen\" value=\"" + htmlEscape(q) + "\">"
          "<div class=\"row\"><button class=\"secondary\" type=\"submit\">Suchen</button></div></form>";
}

static void handleSetup() {
  String step = server.arg("step");
  String html = pageStart();
  if (step == "dir") setupStepDirection(html);
  else if (step == "weather") setupStepWeather(html);
  else setupStepStop(html);

  if (cfg.setupDone) html += "<p class=\"hint\"><a href=\"/\">Abbrechen</a></p>";
  sendPage(html);
}

static void handleSetupStop() {
  if (!requirePost()) return;
  int idx = server.arg("idx").toInt();
  if (idx < 0 || idx >= stopResultCount) {
    redirectTo("/setup?step=stop");
    return;
  }
  const StopResult& s = stopResults[idx];
  cfg.stopId = s.id;
  cfg.stopName = s.name;
  cfg.stopPlace = s.place;
  cfg.stopLat = s.lat;
  cfg.stopLon = s.lon;
  cfg.filter = "";   // Steige gehoeren zur alten Haltestelle
  saveSettings();
  departureCount = 0;
  requestDataFetch();
  redirectTo("/setup?step=dir");
}

static void handleSetupDirection() {
  if (!requirePost()) return;
  String custom = server.arg("custom");
  custom.trim();
  cfg.filter = custom.length() > 0 ? custom : server.arg("filter");
  cfg.onlyTrams = server.hasArg("onlyTrams");
  saveSettings();
  requestDataFetch();
  redirectTo(cfg.setupDone ? "/?msg=saved" : "/setup?step=weather");
}

static void handleSetupWeather() {
  if (!requirePost()) return;
  String choice = server.arg("choice");
  if (choice == "stop") {
    cfg.weatherName = cfg.stopPlace.length() > 0 ? cfg.stopPlace : cfg.stopName;
    cfg.weatherLat = cfg.stopLat;
    cfg.weatherLon = cfg.stopLon;
  } else {
    int idx = choice.toInt();
    if (choice.length() == 0 || idx < 0 || idx >= placeResultCount) {
      redirectTo("/setup?step=weather");
      return;
    }
    cfg.weatherName = placeResults[idx].name;
    cfg.weatherLat = placeResults[idx].lat;
    cfg.weatherLon = placeResults[idx].lon;
  }

  bool firstSetup = !cfg.setupDone;
  cfg.setupDone = true;
  saveSettings();
  mode = Mode::Normal;
  requestDataFetch();
  requestRedraw();
  redirectTo(firstSetup ? "/?msg=done" : "/?msg=saved");
}

// ================= Hauptseite =================

static void handleRoot() {
  if (!cfg.setupDone) {
    redirectTo("/setup?step=stop");
    return;
  }

  String msg = server.arg("msg");
  String html = pageStart();
  html += "<h1>E-Ink Display</h1><p class=\"step\">" + htmlEscape(cfg.stopName) + "</p>";

  if (msg == "done") html += "<p class=\"msg\">Eingerichtet! Das Display aktualisiert sich gleich.</p>";
  else if (msg == "saved") html += "<p class=\"msg\">Gespeichert. Das Display aktualisiert sich gleich.</p>";
  else if (msg == "ota") html += "<p class=\"msg\">" + htmlEscape(otaStatus) + "</p>";
  if (isBusy) html += "<p class=\"msg\">Das Display zeichnet gerade neu.</p>";

  // ===== Notiz =====
  html += "<form class=\"card\" method=\"post\" action=\"/note\" accept-charset=\"utf-8\">"
          "<label class=\"accent\" for=\"text\">Heute wichtig:</label>"
          "<textarea id=\"text\" name=\"text\" maxlength=\"100\" placeholder=\"z.B. M\xc3\xbcll rausbringen\">";
  html += htmlEscape(noteText);
  html += "</textarea>"
          "<div class=\"row\">"
          "<button type=\"submit\">Speichern</button>"
          "<button class=\"secondary\" type=\"submit\" name=\"clear\" value=\"1\">L\xc3\xb6schen</button>"
          "</div>"
          "<p class=\"hint\">Wird um Mitternacht automatisch gel\xc3\xb6scht.</p>"
          "</form>";

  // ===== Status =====
  html += "<div class=\"card kv\">";
  html += "<div><span>Musik</span><span>" +
          (currentTitle.length() > 0 ? htmlEscape(currentTitle + " \xe2\x80\x93 " + currentArtist) : String("\xe2\x80\x93")) +
          "</span></div>";
  html += "<div><span>Wetter " + htmlEscape(cfg.weatherName) + "</span><span>" + weatherTemp + "\xc2\xb0 " +
          htmlEscape(weatherText) + (weatherRain ? " \xe2\x98\x82" : "") + "</span></div>";
  TramRow rows[MAX_TRAMS];
  int n = upcomingTrams(rows, MAX_TRAMS);
  for (int i = 0; i < n; i++) {
    html += "<div><span>" + htmlEscape(rows[i].line + " " + rows[i].dest) + "</span><span>" + rows[i].time + "</span></div>";
  }
  if (n == 0) html += "<div><span>Abfahrten</span><span>keine</span></div>";
  if (tramError) {
    html += "<div><span>Fehler</span><span class=\"err\">" + htmlEscape(tramErrorText) + "</span></div>";
  }
  html += "</div>";

  // ===== Einstellungen =====
  String direction = cfg.filter.length() > 0 ? htmlEscape(cfg.filter) : String("alle");
  if (cfg.onlyTrams) direction += ", nur Trams";

  html += "<details class=\"card\"" + String(server.hasArg("settings") ? " open" : "") + ">"
          "<summary>Einstellungen</summary><div class=\"kv\">";
  html += "<div><span>Haltestelle</span><span>" + htmlEscape(cfg.stopName) +
          " <a href=\"/setup?step=stop\">\xc3\xa4ndern</a></span></div>";
  html += "<div><span>Richtung</span><span>" + direction + " <a href=\"/setup?step=dir\">\xc3\xa4ndern</a></span></div>";
  html += "<div><span>Wetter</span><span>" + htmlEscape(cfg.weatherName) +
          " <a href=\"/setup?step=weather\">\xc3\xa4ndern</a></span></div>";
  html += "<div><span>WLAN</span><span>" + htmlEscape(cfg.wifiSsid) + "</span></div>";
  html += "</div>";

  html += "<form method=\"post\" action=\"/settings\" style=\"margin-top:14px\">"
          "<div class=\"field\"><label for=\"dispMin\">Display h\xc3\xb6" "chstens alle \xe2\x80\xa6 Minuten neu zeichnen</label>"
          "<input type=\"number\" id=\"dispMin\" name=\"dispMin\" min=\"1\" max=\"30\" value=\"" +
          String(cfg.displayMinutes) + "\"></div>"
          "<label class=\"check\"><input type=\"checkbox\" name=\"autoUpdate\" value=\"1\"" +
          String(cfg.autoUpdate ? " checked" : "") + "> Updates automatisch installieren</label>"
          "<div class=\"row\"><button type=\"submit\">Speichern</button></div></form>";

  // Firmware
  html += "<h2>Firmware</h2><div class=\"kv\"><div><span>Installiert</span><span>" FIRMWARE_VERSION "</span></div>";
  if (otaStatus.length() > 0) html += "<div><span>Update</span><span>" + htmlEscape(otaStatus) + "</span></div>";
  html += "</div>";
  if (strlen(OTA_MANIFEST_URL) > 0) {
    html += "<form method=\"post\" action=\"/ota/check\"><div class=\"row\">"
            "<button class=\"secondary\" type=\"submit\">Nach Updates suchen</button></div></form>";
  }
  html += "<form method=\"post\" action=\"/update\" enctype=\"multipart/form-data\" style=\"margin-top:12px\">"
          "<label for=\"fw\" style=\"font-weight:400\">Oder firmware.bin hochladen</label>"
          "<input type=\"file\" id=\"fw\" name=\"fw\" accept=\".bin\" required>"
          "<div class=\"row\"><button class=\"secondary\" type=\"submit\">Hochladen und installieren</button></div></form>";

  // Zuruecksetzen
  html += "<h2>Zur\xc3\xbc" "cksetzen</h2>"
          "<form method=\"post\" action=\"/reset\" onsubmit=\"return confirm('WLAN wirklich l\xc3\xb6schen? "
          "Das Display \xc3\xb6""ffnet danach den Einrichtungs-Hotspot.')\">"
          "<input type=\"hidden\" name=\"what\" value=\"wifi\">"
          "<div class=\"row\"><button class=\"secondary\" type=\"submit\">Anderes WLAN einrichten</button></div></form>"
          "<form method=\"post\" action=\"/reset\" onsubmit=\"return confirm('Wirklich alles zur\xc3\xbc" "cksetzen? "
          "WLAN, Haltestelle, Wetter und Notiz werden gel\xc3\xb6scht.')\">"
          "<input type=\"hidden\" name=\"what\" value=\"all\">"
          "<div class=\"row\"><button class=\"danger\" type=\"submit\">Alles zur\xc3\xbc" "cksetzen</button></div></form>";

  html += "</details>";
  sendPage(html);
}

static void handleNote() {
  if (!requirePost()) return;
  saveNote(server.hasArg("clear") ? String("") : server.arg("text"));
  // Zurueck zur Seite, damit ein Neuladen nicht erneut absendet
  redirectTo("/?msg=saved");
}

static void handleSettings() {
  if (!requirePost()) return;
  cfg.displayMinutes = constrain(server.arg("dispMin").toInt(), 1, 30);
  cfg.autoUpdate = server.hasArg("autoUpdate");
  saveSettings();
  redirectTo("/?msg=saved&settings=1");
}

static void handleReset() {
  if (!requirePost()) return;
  String what = server.arg("what");
  if (what == "all") {
    factoryReset();
  } else {
    cfg.wifiSsid = "";
    cfg.wifiPass = "";
    saveSettings();
  }

  String html = pageStart();
  html += "<h1>Zur\xc3\xbc" "ckgesetzt</h1><div class=\"card\"><p>Das Display startet neu und \xc3\xb6""ffnet den Hotspot "
          "<b>" + htmlEscape(setupApName()) + "</b>. Verbinde dich damit, um es neu einzurichten.</p></div>";
  sendPage(html);
  restartAt = millis() + 2000;
}

static void handleOtaCheck() {
  if (!requirePost()) return;
  String url;
  if (otaCheck(url) != 1) {
    redirectTo("/?msg=ota&settings=1");
    return;
  }
  otaPendingUrl = url;   // installiert die Hauptschleife, damit diese Antwort noch rausgeht
  String html = pageStart();
  html += "<h1>Update wird installiert</h1><div class=\"card\"><p>Version " + htmlEscape(otaLatest) +
          " wird geladen. Das Display zeigt den Fortschritt und startet danach neu.</p>"
          "<p class=\"hint\">Diese Seite in etwa einer Minute neu laden.</p></div>";
  sendPage(html);
}

/// Firmware-Upload aus dem Browser (lokal, ohne Update-Server).
static void handleUploadDone() {
  bool ok = !Update.hasError();
  String html = pageStart();
  if (ok) {
    html += "<h1>Update installiert</h1><div class=\"card\"><p>Das Display startet neu.</p></div>";
    restartAt = millis() + 1500;
  } else {
    html += "<h1>Update fehlgeschlagen</h1><div class=\"card\"><p class=\"err\">" +
            htmlEscape(Update.errorString()) + "</p><p><a href=\"/\">Zur\xc3\xbc" "ck</a></p></div>";
  }
  sendPage(html);
}

static void handleUploadData() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Firmware-Upload: %s\n", upload.filename.c_str());
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
    Serial.printf("Firmware-Upload fertig: %u Bytes\n", upload.totalSize);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

// ================= Schnittstelle fuer die iOS-App =================

/// POST {"title": "...", "artist": "..."} - leerer Titel = keine Musik.
static void handleNowPlaying() {
  if (!requirePost()) return;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    Serial.printf("JSON error: %s\n", error.c_str());
    return;
  }

  String title  = doc["title"]  | "";
  String artist = doc["artist"] | "";

  // Die App schickt den Titel regelmaessig erneut - nur bei Aenderung neu zeichnen
  if (title != currentTitle || artist != currentArtist) {
    currentTitle = title;
    currentArtist = artist;
    Serial.printf("Song: %s / %s\n", title.c_str(), artist.c_str());
    requestRedraw();
  }

  server.send(200, "text/plain", "OK");
}

static void handleStatus() {
  JsonDocument doc;
  doc["busy"] = isBusy;
  doc["version"] = FIRMWARE_VERSION;
  doc["title"] = currentTitle;
  doc["temp"] = weatherTemp;
  doc["rain"] = weatherRain;
  doc["tramError"] = tramError;
  doc["tramErrorText"] = tramErrorText;
  doc["tramStand"] = tramStand;
  doc["note"] = noteText;
  JsonArray arr = doc["trams"].to<JsonArray>();
  TramRow rows[MAX_TRAMS];
  int n = upcomingTrams(rows, MAX_TRAMS);
  for (int i = 0; i < n; i++) {
    JsonObject t = arr.add<JsonObject>();
    t["line"] = rows[i].line;
    t["dest"] = rows[i].dest;
    t["time"] = rows[i].time;
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void setupNormalRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, handleSetup);
  server.on("/setup/stop", handleSetupStop);
  server.on("/setup/dir", handleSetupDirection);
  server.on("/setup/weather", handleSetupWeather);
  server.on("/note", handleNote);
  server.on("/settings", handleSettings);
  server.on("/reset", handleReset);
  server.on("/ota/check", handleOtaCheck);
  server.on("/update", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/nowplaying", handleNowPlaying);
  server.on("/display", handleNowPlaying);   // alter Endpunkt, bleibt kompatibel
  server.on("/status", HTTP_GET, handleStatus);
}
