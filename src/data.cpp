#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "app.h"

// ===== Inhalte =====
String currentTitle = "";
String currentArtist = "";

String weatherTemp = "--";
String weatherText = "";
bool weatherRain = false;

Departure departures[MAX_DEPARTURES];
int departureCount = 0;
bool tramError = false;
String tramErrorText = "";
String tramStand = "--:--";

Route routes[MAX_ROUTES];
int routeCount = 0;

// ================= Hilfen =================

/// EFA liefert Zahlen meist als String - beides abfangen.
static String jsonStr(JsonVariantConst v) {
  if (v.is<const char*>()) return String(v.as<const char*>());
  if (v.is<long>()) return String(v.as<long>());
  return "";
}

static int jsonInt(JsonVariantConst v) {
  if (v.is<const char*>()) return atoi(v.as<const char*>());
  return v.as<int>();
}

/// EFA liefert Datum/Uhrzeit als Objekt mit einzelnen Feldern.
static time_t efaTime(JsonVariantConst t) {
  if (t.isNull()) return 0;
  struct tm tm = {};
  tm.tm_year = jsonInt(t["year"]) - 1900;
  tm.tm_mon  = jsonInt(t["month"]) - 1;
  tm.tm_mday = jsonInt(t["day"]);
  tm.tm_hour = jsonInt(t["hour"]);
  tm.tm_min  = jsonInt(t["minute"]);
  tm.tm_isdst = -1;
  return mktime(&tm);
}

/// HTTPS-GET und JSON direkt aus dem Stream parsen.
static bool getJson(const String& url, JsonDocument& doc, const char* tag,
                    const JsonDocument* filter = nullptr) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);   // kein chunked encoding, damit direkt aus dem Stream geparst werden kann
  http.setTimeout(10000);

  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("%s: HTTP %d\n", tag, code);
    http.end();
    return false;
  }

  DeserializationError err = filter
      ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
      : deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("%s: JSON-Fehler %s\n", tag, err.c_str());
    return false;
  }
  return true;
}

/// WMO-Wettercode von Open-Meteo als kurzer deutscher Text.
static String weatherCodeText(int code) {
  if (code == 0) return "klar";
  if (code == 1) return "heiter";
  if (code == 2) return "wolkig";
  if (code == 3) return "bedeckt";
  if (code == 45 || code == 48) return "Nebel";
  if (code >= 51 && code <= 57) return "Niesel";
  if (code >= 61 && code <= 67) return "Regen";
  if (code >= 71 && code <= 77) return "Schnee";
  if (code >= 80 && code <= 82) return "Schauer";
  if (code == 85 || code == 86) return "Schneeschauer";
  if (code >= 95) return "Gewitter";
  return "";
}

// ================= Wetter (Open-Meteo) =================

bool fetchWeather() {
  if (cfg.weatherLat.length() == 0) return false;

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + cfg.weatherLat +
               "&longitude=" + cfg.weatherLon +
               "&current=temperature_2m,precipitation,weather_code"
               "&hourly=precipitation_probability,precipitation"
               "&forecast_hours=" + String(RAIN_HOURS) +
               "&timezone=auto";

  JsonDocument doc;
  if (!getJson(url, doc, "Wetter")) return false;

  float temp = doc["current"]["temperature_2m"] | NAN;
  float precipNow = doc["current"]["precipitation"] | 0.0f;
  int wmo = doc["current"]["weather_code"] | -1;

  // Schirm, wenn es gerade regnet oder in den naechsten Stunden regnen soll
  bool rain = precipNow > 0;
  for (JsonVariantConst p : doc["hourly"]["precipitation_probability"].as<JsonArrayConst>()) {
    if ((p | 0) >= RAIN_PROB_PERCENT) rain = true;
  }
  for (JsonVariantConst m : doc["hourly"]["precipitation"].as<JsonArrayConst>()) {
    if ((m | 0.0f) >= RAIN_MM) rain = true;
  }

  weatherTemp = isnan(temp) ? "--" : String((int)lroundf(temp));
  weatherText = weatherCodeText(wmo);
  weatherRain = rain;

  Serial.printf("Wetter %s: %s Grad, %s, Schirm: %s\n", cfg.weatherName.c_str(),
                weatherTemp.c_str(), weatherText.c_str(), rain ? "ja" : "nein");
  return true;
}

// ================= Abfahrten (EFA) =================

static void addRoute(const String& platform, const String& line, const String& dest) {
  if (routeCount >= MAX_ROUTES) return;
  for (int i = 0; i < routeCount; i++) {
    if (routes[i].platform == platform && routes[i].line == line && routes[i].dest == dest) return;
  }
  routes[routeCount++] = {platform, line, dest};
}

bool fetchTrams() {
  if (cfg.stopId.length() == 0) {
    tramErrorText = "Keine Haltestelle eingerichtet";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);

  if (!http.begin(client, EFA_BASE "XML_DM_REQUEST")) {
    tramErrorText = "Verbindung fehlgeschlagen";
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Ohne Datum/Uhrzeit nimmt EFA die aktuelle Zeit. Die Haltestelle per ID ist eindeutig.
  String body = "language=de&mode=direct&outputFormat=JSON&type_dm=stop"
                "&useProxFootSearch=0&useRealtime=1&limit=30"
                "&name_dm=" + urlEncode(cfg.stopId);

  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("Tram: HTTP %d\n", code);
    tramErrorText = "HTTP " + String(code);
    http.end();
    return false;
  }

  // Die Antwort ist ~40 KB gross - nur die benoetigten Felder behalten
  JsonDocument filter;
  JsonObject f = filter["departureList"][0].to<JsonObject>();
  f["servingLine"]["number"] = true;
  f["servingLine"]["direction"] = true;
  f["servingLine"]["motType"] = true;
  f["platformName"] = true;
  f["dateTime"] = true;
  f["realDateTime"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("Tram: JSON-Fehler %s\n", err.c_str());
    tramErrorText = "Antwort nicht lesbar";
    return false;
  }

  JsonArrayConst list = doc["departureList"];
  if (list.isNull() || list.size() == 0) {
    Serial.println("Tram: keine Abfahrten");
    tramErrorText = "Keine Abfahrten gefunden";
    return false;
  }

  Serial.printf("--- EFA %s: %d Abfahrten ---\n", cfg.stopName.c_str(), list.size());
  String filterText = cfg.filter;
  filterText.trim();
  routeCount = 0;

  int count = 0;
  for (JsonVariantConst dep : list) {
    JsonVariantConst sl = dep["servingLine"];
    String line = jsonStr(sl["number"]);
    String dest = jsonStr(sl["direction"]);
    String platform = jsonStr(dep["platformName"]);
    int motType = jsonInt(sl["motType"]);
    time_t when = efaTime(dep["realDateTime"]);
    if (when == 0) when = efaTime(dep["dateTime"]);

    Serial.printf("  Linie %s -> %s | %s | Typ %d | %s\n",
                  line.c_str(), dest.c_str(), platform.c_str(), motType, formatTime(when).c_str());
    addRoute(platform, line, shortenDest(dest));

    if (cfg.onlyTrams && motType != 4) continue;   // 4 = Strassenbahn, 5 = Bus
    if (filterText.length() > 0 &&
        !containsIgnoreCase(dest, filterText) && !containsIgnoreCase(platform, filterText)) continue;
    if (count >= MAX_DEPARTURES) continue;

    departures[count].line = line;
    departures[count].dest = shortenDest(dest);
    departures[count].when = when;
    count++;
  }
  departureCount = count;
  if (count == 0) Serial.println("Tram: Filter passt auf nichts");
  tramErrorText = "";
  return true;
}

/// Naechste Abfahrten. Laeuft auch mit aelteren Daten weiter, abgefahrene fallen raus.
int upcomingTrams(TramRow rows[], int maxRows) {
  time_t now = time(nullptr);
  int n = 0;
  for (int i = 0; i < departureCount && n < maxRows; i++) {
    if (departures[i].when < now - 30) continue;   // schon weg
    rows[n].line = departures[i].line;
    rows[n].dest = departures[i].dest;
    rows[n].time = formatTime(departures[i].when);
    n++;
  }
  return n;
}

/// Mit Notiz ist nur Platz fuer drei Abfahrten.
int visibleTramRows() {
  return noteText.length() > 0 ? MAX_TRAMS_WITH_NOTE : MAX_TRAMS;
}

// ================= Suche fuer die Einrichtung =================

/// Haltestellen per Freitext, z.B. "Augsburg Berliner Allee".
int searchStops(const String& query, StopResult out[], int maxResults) {
  String url = EFA_BASE "XML_STOPFINDER_REQUEST?outputFormat=JSON&language=de&type_sf=any"
               "&anyObjFilter_sf=2&coordOutputFormat=WGS84%5BDD.ddddd%5D&name_sf=" + urlEncode(query);

  JsonDocument filter;
  filter["stopFinder"]["points"] = true;
  JsonDocument doc;
  if (!getJson(url, doc, "Haltestellensuche", &filter)) return -1;

  // EFA liefert mehrere Treffer als Array, einen einzelnen als {"point": {...}}
  JsonVariantConst points = doc["stopFinder"]["points"];
  JsonArrayConst list;
  JsonDocument single;
  if (points.is<JsonArrayConst>()) {
    list = points.as<JsonArrayConst>();
  } else if (points["point"].is<JsonObjectConst>()) {
    single.add(points["point"]);
    list = single.as<JsonArrayConst>();
  }

  int n = 0;
  for (JsonVariantConst p : list) {
    if (n >= maxResults) break;
    JsonVariantConst ref = p["ref"];
    String coords = jsonStr(ref["coords"]);   // "lon,lat"
    int comma = coords.indexOf(',');
    out[n].id = jsonStr(ref["id"]);
    out[n].name = jsonStr(p["name"]);
    out[n].place = jsonStr(ref["place"]);
    out[n].lon = comma > 0 ? coords.substring(0, comma) : "";
    out[n].lat = comma > 0 ? coords.substring(comma + 1) : "";
    if (out[n].id.length() > 0) n++;
  }
  return n;
}

/// Orte fuer das Wetter (Open-Meteo Geocoding).
int searchPlaces(const String& query, PlaceResult out[], int maxResults) {
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=" + String(maxResults) +
               "&language=de&format=json&name=" + urlEncode(query);

  JsonDocument doc;
  if (!getJson(url, doc, "Ortssuche")) return -1;

  int n = 0;
  for (JsonVariantConst r : doc["results"].as<JsonArrayConst>()) {
    if (n >= maxResults) break;
    out[n].name = r["name"] | "";
    String region = r["admin1"] | "";
    String country = r["country"] | "";
    out[n].region = region.length() > 0 ? region + ", " + country : country;
    out[n].lat = String(r["latitude"] | 0.0f, 4);
    out[n].lon = String(r["longitude"] | 0.0f, 4);
    n++;
  }
  return n;
}
