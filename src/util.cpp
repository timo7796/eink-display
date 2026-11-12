#include "app.h"

bool timeValid() {
  return time(nullptr) > 1700000000;
}

String formatTime(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &tm);
  return String(buf);
}

String formatDate(time_t t) {
  static const char* days[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                               "Donnerstag", "Freitag", "Samstag"};
  static const char* months[] = {"Januar", "Februar", "M\xc3\xa4rz", "April", "Mai", "Juni", "Juli",
                                 "August", "September", "Oktober", "November", "Dezember"};
  struct tm tm;
  localtime_r(&t, &tm);
  return String(days[tm.tm_wday]) + ", " + tm.tm_mday + ". " + months[tm.tm_mon];
}

String todayKey() {
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
  return String(buf);
}

String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

/// Prozent-Kodierung fuer URL-Parameter und Formular-Bodies.
String urlEncode(const String& str) {
  String out;
  for (const uint8_t* p = (const uint8_t*)str.c_str(); *p; p++) {
    uint8_t c = *p;
    bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '-' || c == '_' || c == '.' || c == '~';
    if (plain) {
      out += (char)c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

void trimUtf8Safe(String& s) {
  if (s.length() == 0) return;
  int len = s.length() - 1;
  while (len > 0 && ((uint8_t)s[len] & 0xC0) == 0x80) {
    len--;
  }
  s = s.substring(0, len);
}

/// "Augsburg, Firnhaberau" -> "Firnhaberau"
String shortenDest(String dest) {
  if (cfg.stopPlace.length() == 0) return dest;
  String prefix = cfg.stopPlace + ", ";
  if (dest.startsWith(prefix)) dest = dest.substring(prefix.length());
  return dest;
}

/// Gross-/Kleinschreibung ignorieren (Umlaute werden byteweise verglichen).
bool containsIgnoreCase(String haystack, String needle) {
  haystack.toLowerCase();
  needle.toLowerCase();
  return haystack.indexOf(needle) >= 0;
}
