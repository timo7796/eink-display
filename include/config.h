#pragma once

// Feste Einstellungen der Firmware. Alles Geraetespezifische (WLAN, Haltestelle,
// Wetterort, ...) wird bei der Ersteinrichtung abgefragt und im Geraet gespeichert.
// Diese Datei enthaelt deshalb keine Passwoerter und kann mit der Firmware
// veroeffentlicht werden.

// ===== Firmware / OTA =====
/// Bei jedem Release hochzaehlen - Geraete installieren nur neuere Versionen.
#define FIRMWARE_VERSION "1.1.0"

/// URL zur version.json fuer automatische Updates, leer = aus.
/// Beispiel GitHub: "https://github.com/<user>/<repo>/releases/latest/download/version.json"
/// Inhalt: {"version": "1.1.1", "url": "https://.../firmware.bin"}
#define OTA_MANIFEST_URL "https://github.com/timo7796/eink-display/releases/latest/download/version.json"
#define OTA_CHECK_INTERVAL_MS (6UL * 60 * 60 * 1000)   // alle 6 Stunden

// ===== Geraet =====
#define HOSTNAME        "eink"          // erreichbar als http://eink.local
#define SETUP_AP_PREFIX "E-Ink-Setup"   // Hotspot bei Ersteinrichtung, z.B. "E-Ink-Setup-3FA2"

/// Hotspot nach so langer Zeit ohne Verbindung beenden und das gespeicherte WLAN
/// erneut versuchen (z.B. wenn der Router nur kurz weg war).
#define PORTAL_TIMEOUT_MS (10UL * 60 * 1000)

// ===== Wetter =====
/// Regenschirm anzeigen, wenn in den naechsten RAIN_HOURS Stunden
/// die Regenwahrscheinlichkeit >= RAIN_PROB_PERCENT ist oder
/// mindestens RAIN_MM Niederschlag pro Stunde erwartet wird.
#define RAIN_HOURS        6
#define RAIN_PROB_PERCENT 50
#define RAIN_MM           0.3f

// ===== Abfahrten (EFA / Bayern Fahrplan, deckt ganz Deutschland ab) =====
#define EFA_BASE "https://mobile.defas-fgi.de/beg/"

// ===== Intervalle =====
#define TRAM_INTERVAL_MS        (60UL * 1000)        // Abfahrten neu laden
#define WEATHER_INTERVAL_MS     (10UL * 60 * 1000)   // Wetter neu laden
#define DEFAULT_DISPLAY_MINUTES 1                    // Standard, in der Web-App aenderbar
