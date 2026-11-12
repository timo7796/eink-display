# E-Ink Display

ESP32 mit 2,9"-E-Ink-Display (3 Farben). Zeigt:

- die nächsten Abfahrten einer Haltestelle (Echtzeit über den Bayern Fahrplan, ganz Deutschland)
- Temperatur und einen Regenschirm, wenn es regnen soll (Open-Meteo)
- „Heute wichtig“ – eine Notiz, die jeder im WLAN über `http://eink.local` setzen kann
- optional den aktuellen Song (iOS-App, Endpunkt `/nowplaying`)

## Einrichtung

1. Display einstecken – es öffnet den Hotspot `E-Ink-Setup-XXXX`.
2. Mit dem Handy verbinden, WLAN auswählen und Passwort eingeben.
3. Zurück ins eigene WLAN, `http://eink.local` öffnen und Haltestelle, Richtung und Wetter wählen.

Bei der Einrichtung lässt sich eine PIN setzen. Sie schützt Einstellungen,
Zurücksetzen und Updates – die Notiz bleibt für alle im WLAN offen.

Zurücksetzen geht in der Weboberfläche unter *Einstellungen*. PIN vergessen:
die **BOOT-Taste 10 Sekunden halten** setzt alles zurück, auch das WLAN.

## Entwicklung

PlatformIO-Projekt (`nodemcu-32s`). Der erste Flash muss per USB laufen
(Partitionstabelle `min_spiffs.csv`), danach kommen Updates über OTA.

## Release (OTA)

1. `FIRMWARE_VERSION` in `include/config.h` hochzählen.
2. Neue Version auf dem eigenen Gerät testen.
3. `tools/release.sh timo7796/eink-display`

Die Geräte prüfen alle 6 Stunden
`releases/latest/download/version.json` und installieren neuere Versionen selbst.
