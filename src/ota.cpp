#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "app.h"

// Updates aus dem Internet: Das Geraet laedt regelmaessig OTA_MANIFEST_URL
// ({"version": "1.2.0", "url": "https://.../firmware.bin"}) und installiert
// neuere Versionen selbst - funktioniert in jedem WLAN, auch bei Freunden.

String otaLatest = "";
String otaStatus = "";
String otaPendingUrl = "";   // vom Button in der Web-App angestossen

static unsigned long lastCheck = 0;
static bool checkedOnce = false;

/// "1.10.0" > "1.9.3" - Teile numerisch vergleichen.
static bool versionNewer(const String& candidate, const String& current) {
  int a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
  sscanf(candidate.c_str(), "%d.%d.%d", &a[0], &a[1], &a[2]);
  sscanf(current.c_str(), "%d.%d.%d", &b[0], &b[1], &b[2]);
  for (int i = 0; i < 3; i++) {
    if (a[i] != b[i]) return a[i] > b[i];
  }
  return false;
}

int otaCheck(String& url) {
  if (strlen(OTA_MANIFEST_URL) == 0) {
    otaStatus = "Kein Update-Server eingerichtet";
    return -1;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // GitHub leitet Downloads um
  http.setTimeout(10000);
  if (!http.begin(client, OTA_MANIFEST_URL)) {
    otaStatus = "Update-Server nicht erreichbar";
    return -1;
  }
  int code = http.GET();
  if (code != 200) {
    otaStatus = "Update-Server: HTTP " + String(code);
    http.end();
    return -1;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    otaStatus = "version.json nicht lesbar";
    return -1;
  }

  otaLatest = doc["version"] | "";
  url = doc["url"] | "";
  Serial.printf("OTA: installiert %s, verfuegbar %s\n", FIRMWARE_VERSION, otaLatest.c_str());

  if (otaLatest.length() == 0 || url.length() == 0) {
    otaStatus = "version.json unvollst\xc3\xa4ndig";
    return -1;
  }
  if (!versionNewer(otaLatest, FIRMWARE_VERSION)) {
    otaStatus = "Aktuell";
    return 0;
  }
  otaStatus = "Version " + otaLatest + " verf\xc3\xbc" "gbar";
  return 1;
}

bool otaInstall(const String& url, const String& version) {
  Serial.printf("OTA: installiere %s von %s\n", version.c_str(), url.c_str());
  drawUpdateScreen(version);

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);   // bei Erfolg startet das Geraet sofort neu

  t_httpUpdate_return result = httpUpdate.update(client, url);
  // Hier kommen wir nur bei einem Fehler an
  otaStatus = "Update fehlgeschlagen: " + httpUpdate.getLastErrorString();
  Serial.println(otaStatus);
  requestRedraw();
  return result == HTTP_UPDATE_OK;
}

/// Automatische Pruefung: kurz nach dem Start, dann alle OTA_CHECK_INTERVAL_MS.
void otaLoop() {
  if (otaPendingUrl.length() > 0) {
    String url = otaPendingUrl;
    otaPendingUrl = "";
    otaInstall(url, otaLatest);
    return;
  }

  if (!cfg.autoUpdate || strlen(OTA_MANIFEST_URL) == 0) return;
  if (!checkedOnce && millis() < 2UL * 60 * 1000) return;   // erst Daten laden und anzeigen
  if (checkedOnce && millis() - lastCheck < OTA_CHECK_INTERVAL_MS) return;

  checkedOnce = true;
  lastCheck = millis();
  String url;
  if (otaCheck(url) == 1) otaInstall(url, otaLatest);
}
