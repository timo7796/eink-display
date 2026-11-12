#include <WiFi.h>
#include <SPI.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "app.h"

#define EPD_CS   5
#define EPD_DC   17
#define EPD_RST  16
#define EPD_BUSY 4

static GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(GxEPD2_290_C90c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;

void initDisplay() {
  display.init(115200);
}

// ================= Zeichenhilfen =================

/// lastLineWidth > 0 begrenzt die letzte Zeile schmaler, z.B. um rechts Platz freizuhalten.
static int drawWrappedText(const String& text, int x, int y, int maxWidth, int lineHeight, int maxLines,
                           int lastLineWidth = 0) {
  String remaining = text;
  remaining.trim();
  int linesDrawn = 0;
  int cursorY = y;

  while (remaining.length() > 0 && linesDrawn < maxLines) {
    String candidate = remaining;
    bool isLastLine = (linesDrawn == maxLines - 1);
    int width = (isLastLine && lastLineWidth > 0) ? lastLineWidth : maxWidth;

    while (u8g2.getUTF8Width(candidate.c_str()) > width && candidate.length() > 1) {
      int space = candidate.lastIndexOf(' ');
      if (space > 0) {
        candidate = candidate.substring(0, space);
      } else {
        trimUtf8Safe(candidate);
      }
    }

    int consumed = candidate.length();
    bool hasMore = consumed < (int)remaining.length();

    String toDraw = candidate;
    if (isLastLine && hasMore) {
      while (u8g2.getUTF8Width((toDraw + "...").c_str()) > width && toDraw.length() > 1) {
        trimUtf8Safe(toDraw);
      }
      toDraw += "...";
    }

    u8g2.setCursor(x, cursorY);
    u8g2.print(toDraw);

    cursorY += lineHeight;
    linesDrawn++;

    remaining = remaining.substring(consumed);
    remaining.trim();
  }

  return cursorY;
}

/// Zeichnet ein kleines Regenschirm-Symbol.
/// (cx, cy) ist die Mitte der Schirmkante, r ihr Radius.
static void drawUmbrella(int cx, int cy, int r, uint16_t color) {
  // Schirmdach: gefuellte obere Halbscheibe, spaltenweise
  for (int dx = -r; dx <= r; dx++) {
    int dy = (int)(sqrt((float)(r * r - dx * dx)) + 0.5);
    display.drawLine(cx + dx, cy - dy, cx + dx, cy, color);
  }
  // Zacken an der Unterkante andeuten
  display.drawPixel(cx - r / 2, cy + 1, color);
  display.drawPixel(cx + r / 2, cy + 1, color);

  // Stiel
  int stemBottom = cy + r + 3;
  display.drawLine(cx, cy, cx, stemBottom, GxEPD_BLACK);
  // Haken am Stielende
  display.drawLine(cx, stemBottom, cx - 2, stemBottom + 1, GxEPD_BLACK);
  display.drawLine(cx - 2, stemBottom + 1, cx - 3, stemBottom - 1, GxEPD_BLACK);
}

/// Zeichnet Text rechtsbuendig endend an x_right.
static void drawRightAligned(const String& text, int x_right, int y) {
  int w = u8g2.getUTF8Width(text.c_str());
  u8g2.setCursor(x_right - w, y);
  u8g2.print(text);
}

static void printAt(const uint8_t* font, uint16_t color, int x, int y, const String& text) {
  u8g2.setFont(font);
  u8g2.setForegroundColor(color);
  u8g2.setCursor(x, y);
  u8g2.print(text);
  u8g2.setForegroundColor(GxEPD_BLACK);
}

static void beginDrawing() {
  display.setRotation(1);
  display.setFullWindow();

  u8g2.begin(display);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setBackgroundColor(GxEPD_WHITE);
}

// ================= Bildschirme =================

/// Kein WLAN: Anleitung fuer den Hotspot.
static void drawPortalScreen() {
  printAt(u8g2_font_helvB12_tf, GxEPD_BLACK, 8, 18, "Einrichtung");
  printAt(u8g2_font_helvR10_tf, GxEPD_BLACK, 8, 40, "1. Handy mit diesem WLAN verbinden:");
  printAt(u8g2_font_helvB12_tf, GxEPD_RED, 20, 58, portalSsid);
  printAt(u8g2_font_helvR10_tf, GxEPD_BLACK, 8, 80, "2. Die Einrichtung \xc3\xb6""ffnet sich, sonst");
  printAt(u8g2_font_helvR10_tf, GxEPD_BLACK, 20, 96, "http://192.168.4.1 aufrufen");
  if (portalError.length() > 0) {
    u8g2.setFont(u8g2_font_helvR08_tf);
    u8g2.setForegroundColor(GxEPD_RED);
    drawWrappedText(portalError, 8, 118, 280, 10, 1);
    u8g2.setForegroundColor(GxEPD_BLACK);
  }
}

/// Im WLAN, aber Haltestelle/Wetter fehlen noch.
static void drawSetupScreen() {
  printAt(u8g2_font_helvB12_tf, GxEPD_BLACK, 8, 18, "Fast geschafft");
  printAt(u8g2_font_helvR10_tf, GxEPD_BLACK, 8, 40, "WLAN verbunden. Im selben WLAN \xc3\xb6""ffnen:");
  printAt(u8g2_font_helvB12_tf, GxEPD_RED, 20, 62, "http://" HOSTNAME ".local");
  printAt(u8g2_font_helvR10_tf, GxEPD_BLACK, 20, 80, "oder http://" + WiFi.localIP().toString());
  printAt(u8g2_font_helvR10_tf, GxEPD_BLACK, 8, 106, "Dort Haltestelle und Wetter ausw\xc3\xa4hlen.");
}

/// Normalbetrieb. Kompaktes Layout (296 x 128): Kopf bis y=30, darunter
/// bis zu 5 Abfahrten bzw. 3 Abfahrten + Notiz.
static void drawMainScreen(TramRow rows[], int rowCount) {
  bool hasNote = noteText.length() > 0;

  // ===== Wetter: oben rechts =====
  u8g2.setFont(u8g2_font_helvB12_tf);
  String tempStr = weatherTemp + "\xc2\xb0";   // UTF-8 Gradzeichen
  drawRightAligned(tempStr, 288, 15);
  int tempW = u8g2.getUTF8Width(tempStr.c_str());

  if (weatherRain) {
    // links neben die Temperatur, in der Zusatzfarbe des Displays
    drawUmbrella(288 - tempW - 12, 8, 6, GxEPD_RED);
  }

  u8g2.setFont(u8g2_font_helvR08_tf);
  drawRightAligned(weatherText, 288, 26);

  // ===== Musik bzw. Datum: oben links, Platz fuer Wetter freilassen =====
  const int MUSIC_W = 200;
  if (currentTitle.length() > 0) {
    u8g2.setFont(u8g2_font_helvB10_tf);
    drawWrappedText(currentTitle, 8, 13, MUSIC_W, 14, 1);
    u8g2.setFont(u8g2_font_helvR08_tf);
    drawWrappedText(currentArtist, 8, 25, MUSIC_W, 12, 1);
  } else if (timeValid()) {
    u8g2.setFont(u8g2_font_helvR10_tf);
    drawWrappedText(formatDate(time(nullptr)), 8, 19, MUSIC_W, 14, 1);
  }

  // ===== Trennlinie =====
  display.drawLine(8, 30, 288, 30, GxEPD_BLACK);

  // ===== Abfahrten =====
  const int ROW_Y = 45;
  const int ROW_H = 16;
  if (rowCount == 0) {
    u8g2.setFont(u8g2_font_helvR10_tf);
    u8g2.setCursor(8, ROW_Y + 2);
    u8g2.print(tramError ? "Abfahrten nicht abrufbar" : "Keine Abfahrten");
  } else {
    int y = ROW_Y;
    for (int i = 0; i < rowCount; i++) {
      // Liniennummer fett
      u8g2.setFont(u8g2_font_helvB10_tf);
      u8g2.setCursor(8, y);
      u8g2.print(rows[i].line);
      int lineW = u8g2.getUTF8Width(rows[i].line.c_str());

      // Abfahrtszeit rechtsbuendig, zuerst zeichnen um Platz zu kennen
      int timeW = u8g2.getUTF8Width(rows[i].time.c_str());
      drawRightAligned(rows[i].time, 288, y);

      // Ziel dazwischen, auf den freien Platz begrenzt
      int destX = 8 + max(lineW, 22) + 6;   // Ziele buendig, auch bei ein-/zweistelligen Linien
      int destW = (288 - timeW - 8) - destX;
      if (destW > 20) {
        u8g2.setFont(u8g2_font_helvR10_tf);
        drawWrappedText(rows[i].dest, destX, y, destW, ROW_H, 1);
      }

      y += ROW_H;
    }
  }

  // ===== Hinweis bei Abruffehler, unten rechts =====
  // Die Zeiten bleiben gueltig, koennen aber Verspaetungen nicht mehr enthalten.
  String footer = tramError ? "Echtzeit nicht abrufbar" : "";
  const int footerY = hasNote ? 115 : 124;
  int footerW = 0;
  if (footer.length() > 0) {
    u8g2.setFont(u8g2_font_helvR08_tf);
    drawRightAligned(footer, 288, footerY);
    footerW = u8g2.getUTF8Width(footer.c_str());
  }

  if (hasNote) {
    // ===== Heute wichtig: unter 3 Abfahrten, bis zu 2 Zeilen =====
    display.drawLine(8, 84, 288, 84, GxEPD_BLACK);
    const char* label = "Heute wichtig:";
    printAt(u8g2_font_helvB10_tf, GxEPD_RED, 8, 99, label);
    u8g2.setFont(u8g2_font_helvB10_tf);
    int textX = 8 + u8g2.getUTF8Width(label) + 6;
    u8g2.setFont(u8g2_font_helvR10_tf);
    int lastLineW = footerW > 0 ? 288 - footerW - 8 - textX : 0;
    drawWrappedText(noteText, textX, 99, 288 - textX, 16, 2, lastLineW);
  }
}

void updateDisplay() {
  isBusy = true;

  TramRow rows[MAX_TRAMS];
  int rowCount = upcomingTrams(rows, visibleTramRows());

  beginDrawing();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    switch (mode) {
      case Mode::Portal: drawPortalScreen(); break;
      case Mode::Setup:  drawSetupScreen(); break;
      case Mode::Normal: drawMainScreen(rows, rowCount); break;
    }
  } while (display.nextPage());

  isBusy = false;
}

void drawUpdateScreen(const String& version) {
  isBusy = true;
  beginDrawing();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    printAt(u8g2_font_helvB12_tf, GxEPD_BLACK, 8, 40, "Update wird installiert");
    printAt(u8g2_font_helvR10_tf, GxEPD_BLACK, 8, 64, String("Version ") + FIRMWARE_VERSION + " -> " + version);
    printAt(u8g2_font_helvR10_tf, GxEPD_BLACK, 8, 90, "Bitte nicht ausstecken.");
  } while (display.nextPage());
  isBusy = false;
}

/// Alles, was auf dem Display landet - aendert sich das nicht, wird nicht neu gezeichnet.
String displaySignature() {
  if (mode == Mode::Portal) return "portal|" + portalSsid + "|" + portalError;
  if (mode == Mode::Setup) return "setup|" + WiFi.localIP().toString();

  TramRow rows[MAX_TRAMS];
  int n = upcomingTrams(rows, visibleTramRows());
  String sig = currentTitle + "|" + currentArtist + "|" + weatherTemp + "|" + weatherText + "|" +
               (weatherRain ? "R" : "-") + "|" + (tramError ? "E" : "-") + "|" + noteText;
  for (int i = 0; i < n; i++) sig += "|" + rows[i].line + rows[i].dest + rows[i].time;
  if (currentTitle.length() == 0 && timeValid()) sig += "|" + formatDate(time(nullptr));
  return sig;
}
