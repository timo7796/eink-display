#pragma once

// Gemeinsamer Zustand und Funktionen aller Module.

#include <Arduino.h>
#include <WebServer.h>
#include <time.h>
#include "config.h"

// ================= Einstellungen (settings.cpp) =================

struct Settings {
  String wifiSsid;
  String wifiPass;

  String stopId;      // EFA-Haltestellen-ID, z.B. "2000227"
  String stopName;    // "Augsburg, Berliner Allee"
  String stopPlace;   // "Augsburg" - wird bei den Zielen abgeschnitten
  String stopLat;
  String stopLon;
  String filter;      // Steig oder Ziel, leer = alle
  bool onlyTrams;
  int efaServer;        // Index in EFA_SERVERS, automatisch gewaehlt
  String efaCheckDay;   // Tag der letzten automatischen Serverwahl

  String weatherName;
  String weatherLat;
  String weatherLon;

  int displayMinutes;
  bool autoUpdate;
  String pin;         // schuetzt Einstellungen, Reset und Updates - leer = kein Schutz
  bool setupDone;
};
extern Settings cfg;

void loadSettings();
void saveSettings();
void factoryReset();

// ================= Betriebsart =================

enum class Mode {
  Portal,   // kein WLAN: eigener Hotspot mit WLAN-Einrichtung
  Setup,    // im WLAN, Haltestelle/Wetter noch nicht eingerichtet
  Normal
};
extern Mode mode;
extern String portalSsid;    // Name des eigenen Hotspots
extern String portalError;   // z.B. "Verbindung zu X fehlgeschlagen"

extern WebServer server;
extern volatile bool isBusy;
extern unsigned long restartAt;   // > 0: Neustart zu diesem millis()-Zeitpunkt

void requestRedraw();      // Display sofort neu zeichnen (nach kurzer Entprellung)
void requestDataFetch();   // Abfahrten und Wetter sofort neu laden

// ================= Inhalte =================

extern String currentTitle;
extern String currentArtist;

extern String weatherTemp;
extern String weatherText;
extern bool weatherRain;

struct Departure {
  String line;
  String dest;
  time_t when;   // Abfahrt inkl. Echtzeit-Verspaetung
};
const int MAX_DEPARTURES = 10;
extern Departure departures[MAX_DEPARTURES];
extern int departureCount;
extern bool tramError;
extern String tramErrorText;
extern String tramStand;

/// Linien je Steig - fuer die Richtungsauswahl in der Web-App.
struct Route {
  String platform;
  String line;
  String dest;
};
const int MAX_ROUTES = 24;
extern Route routes[MAX_ROUTES];
extern int routeCount;

/// Eine angezeigte Abfahrt.
struct TramRow {
  String line;
  String dest;
  String time;
};
const int MAX_TRAMS = 5;          // ohne Notiz
const int MAX_TRAMS_WITH_NOTE = 3;
int upcomingTrams(TramRow rows[], int maxRows);
int visibleTramRows();

extern String noteText;
void loadNote();
void saveNote(String text);
void checkNoteExpiry();

// ================= Hilfen (util.cpp) =================

bool timeValid();
String formatTime(time_t t);
String formatDate(time_t t);
String todayKey();
String htmlEscape(const String& s);
String urlEncode(const String& s);
void trimUtf8Safe(String& s);
String shortenDest(String dest);
bool containsIgnoreCase(String haystack, String needle);

// ================= Datenabruf (data.cpp) =================

bool fetchWeather();
bool fetchTrams();
void chooseEfaServer();   // beide Server testen, den mit Echtzeit nehmen
void efaDailyCheck();     // einmal am Tag tagsueber neu waehlen

struct StopResult {
  String id;
  String name;
  String place;
  String lat;
  String lon;
};
int searchStops(const String& query, StopResult out[], int maxResults);

struct PlaceResult {
  String name;
  String region;
  String lat;
  String lon;
};
int searchPlaces(const String& query, PlaceResult out[], int maxResults);

// ================= Display (display.cpp) =================

void initDisplay();
void updateDisplay();
void drawUpdateScreen(const String& version);
String displaySignature();

// ================= Web (web.cpp) =================

void setupPortalRoutes();
void setupNormalRoutes();
void scanNetworks();

// ================= OTA (ota.cpp) =================

extern String otaLatest;   // zuletzt gefundene Version auf dem Server
extern String otaStatus;   // Text fuer die Web-App
/// 1 = neuere Version gefunden (url gesetzt), 0 = aktuell, -1 = Fehler
int otaCheck(String& url);
bool otaInstall(const String& url, const String& version);
void otaLoop();
