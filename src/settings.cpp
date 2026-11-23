#include <Preferences.h>
#include "app.h"

// Alles Geraetespezifische liegt im NVS-Flash und uebersteht Neustarts und OTA-Updates.

Settings cfg;
static Preferences prefs;

String noteText = "";
static String noteDate = "";   // Tag, an dem die Notiz gesetzt wurde - um Mitternacht wird sie geloescht
static const int NOTE_MAX_BYTES = 120;

void loadSettings() {
  prefs.begin("eink", false);
  cfg.wifiSsid = prefs.getString("ssid", "");
  cfg.wifiPass = prefs.getString("pass", "");

  cfg.stopId = prefs.getString("stopId", "");
  cfg.stopName = prefs.getString("stopName", "");
  cfg.stopPlace = prefs.getString("stopPlace", "");
  cfg.stopLat = prefs.getString("stopLat", "");
  cfg.stopLon = prefs.getString("stopLon", "");
  cfg.filter = prefs.getString("filter", "");
  cfg.onlyTrams = prefs.getBool("onlyTrams", false);
  cfg.efaServer = prefs.getInt("efaSrv", 0);
  if (cfg.efaServer < 0 || cfg.efaServer >= EFA_SERVER_COUNT) cfg.efaServer = 0;
  cfg.efaCheckDay = prefs.getString("efaChk", "");

  cfg.weatherName = prefs.getString("wName", "");
  cfg.weatherLat = prefs.getString("wLat", "");
  cfg.weatherLon = prefs.getString("wLon", "");

  cfg.displayMinutes = prefs.getInt("dispMin", DEFAULT_DISPLAY_MINUTES);
  cfg.autoUpdate = prefs.getBool("autoUpd", true);
  cfg.pin = prefs.getString("pin", "");
  cfg.setupDone = prefs.getBool("setupDone", false);
}

void saveSettings() {
  prefs.putString("ssid", cfg.wifiSsid);
  prefs.putString("pass", cfg.wifiPass);

  prefs.putString("stopId", cfg.stopId);
  prefs.putString("stopName", cfg.stopName);
  prefs.putString("stopPlace", cfg.stopPlace);
  prefs.putString("stopLat", cfg.stopLat);
  prefs.putString("stopLon", cfg.stopLon);
  prefs.putString("filter", cfg.filter);
  prefs.putBool("onlyTrams", cfg.onlyTrams);
  prefs.putInt("efaSrv", cfg.efaServer);
  prefs.putString("efaChk", cfg.efaCheckDay);

  prefs.putString("wName", cfg.weatherName);
  prefs.putString("wLat", cfg.weatherLat);
  prefs.putString("wLon", cfg.weatherLon);

  prefs.putInt("dispMin", cfg.displayMinutes);
  prefs.putBool("autoUpd", cfg.autoUpdate);
  prefs.putString("pin", cfg.pin);
  prefs.putBool("setupDone", cfg.setupDone);

  Serial.printf("Einstellungen: WLAN '%s' | %s (%s) | Filter '%s' | nur Trams %d | Wetter %s | alle %d min\n",
                cfg.wifiSsid.c_str(), cfg.stopName.c_str(), cfg.stopId.c_str(), cfg.filter.c_str(),
                cfg.onlyTrams, cfg.weatherName.c_str(), cfg.displayMinutes);
}

/// Loescht alles inkl. WLAN - nach dem Neustart startet die Ersteinrichtung.
void factoryReset() {
  prefs.clear();
  Serial.println("Auf Werkseinstellungen zurueckgesetzt");
}

// ================= Notiz =================

void loadNote() {
  noteText = prefs.getString("note", "");
  noteDate = prefs.getString("noteDate", "");
}

void saveNote(String text) {
  text.replace("\r", "");
  text.replace("\n", " ");
  text.trim();
  while (text.length() > NOTE_MAX_BYTES) trimUtf8Safe(text);

  noteText = text;
  noteDate = timeValid() ? todayKey() : "";
  prefs.putString("note", noteText);
  prefs.putString("noteDate", noteDate);
  Serial.printf("Notiz: %s\n", noteText.c_str());
  requestRedraw();
}

/// "Heute wichtig" gilt nur fuer heute - am naechsten Tag automatisch leeren.
void checkNoteExpiry() {
  if (noteText.length() == 0 || !timeValid()) return;
  if (noteDate.length() == 0) {
    // Gesetzt, bevor die Uhrzeit bekannt war
    noteDate = todayKey();
    prefs.putString("noteDate", noteDate);
  } else if (noteDate != todayKey()) {
    saveNote("");
  }
}
