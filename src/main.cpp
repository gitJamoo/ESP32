#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

const int          NUM_PANES   = 3;
const unsigned long DURATIONS[] = {8000, 5000, 5000}; // ms per pane

int           currentPane = 0;
unsigned long paneStart   = 0;

// ── Pane 0: Star Wars ─────────────────────────────────────────────────────────

const char* DEATH_STAR[] = {
  "      .-------.    ",
  "    .'  *      '.  ",
  "   /   .------.  \\",
  "  |   /  o  o  \\  |",
  "  |--              --|",
  "  |   \\  o  o  /  |",
  "   \\   '------'  /",
  "    '.           .'",
  "      '-------'    ",
};
const int DS_LINES = 9;

const char* CRAWL[] = {
  "A long time ago in a",
  "galaxy far, far away...",
  "",
  "     Episode IV",
  "     A NEW HOPE",
  "",
  "It is a period of",
  "civil war. Rebel",
  "spaceships have won",
  "their first victory.",
};
const int CRAWL_COUNT = 10;

void drawStarWarsPane() {
  tft.fillScreen(TFT_BLACK);

  // "STAR WARS" centered at top
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(3);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("STAR WARS", 160, 6);

  // Death Star ASCII, centered horizontally
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  for (int i = 0; i < DS_LINES; i++)
    tft.drawString(DEATH_STAR[i], 86, 38 + i * 10);

  // Episode crawl text, each line centered
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  for (int i = 0; i < CRAWL_COUNT; i++) {
    int x = (320 - (int)strlen(CRAWL[i]) * 6) / 2;
    if (x < 0) x = 0;
    tft.drawString(CRAWL[i], x, 132 + i * 10);
  }

  tft.setTextColor(tft.color565(50, 50, 50), TFT_BLACK);
  tft.drawString("1/3  Star Wars", 5, 230);
}

// ── Pane 1: Starfield ─────────────────────────────────────────────────────────

#define NUM_STARS 100
struct Star { float x, y, z; int16_t px, py; };
Star stars[NUM_STARS];

void resetStar(int i) {
  stars[i] = {
    (float)random(-160, 160),
    (float)random(-120, 120),
    (float)random(20, 100),
    -1, -1
  };
}

void initStars() {
  for (int i = 0; i < NUM_STARS; i++) resetStar(i);
}

void tickStarfield() {
  for (int i = 0; i < NUM_STARS; i++) {
    // erase old pixel
    if (stars[i].px >= 0)
      tft.drawPixel(stars[i].px, stars[i].py, TFT_BLACK);

    stars[i].z -= 1.2f;
    if (stars[i].z <= 1) { resetStar(i); continue; }

    int16_t nx = (int16_t)(stars[i].x / stars[i].z * 120 + 160);
    int16_t ny = (int16_t)(stars[i].y / stars[i].z * 120 + 120);
    stars[i].px = nx;
    stars[i].py = ny;

    if (nx >= 0 && nx < 320 && ny >= 0 && ny < 240) {
      uint8_t b = (uint8_t)(220 - stars[i].z * 1.85f);
      tft.drawPixel(nx, ny, tft.color565(b, b, b));
    }
  }
}

// ── Pane 2: System Info ───────────────────────────────────────────────────────

void drawSystemPane() {
  tft.fillScreen(TFT_BLACK);
  char buf[48];

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("[ ESP32 STATUS ]", 10, 8);

  tft.setTextSize(1);

  auto row = [&](int y, const char* label, const char* val, uint16_t col) {
    tft.setTextColor(tft.color565(130, 130, 130), TFT_BLACK);
    tft.drawString(label, 10, y);
    tft.setTextColor(col, TFT_BLACK);
    tft.drawString(val, 110, y);
  };

  unsigned long s = millis() / 1000;
  snprintf(buf, sizeof(buf), "%02luh %02lum %02lus", s/3600, (s%3600)/60, s%60);
  row(40, "Uptime:", buf, TFT_WHITE);

  snprintf(buf, sizeof(buf), "%lu KB", ESP.getFreeHeap() / 1024);
  row(56, "Free heap:", buf, TFT_CYAN);

  snprintf(buf, sizeof(buf), "%d MHz", getCpuFrequencyMhz());
  row(72, "CPU freq:", buf, TFT_YELLOW);

  snprintf(buf, sizeof(buf), "rev %d", ESP.getChipRevision());
  row(88, "Chip:", buf, TFT_WHITE);

  // Heap usage bar
  uint32_t total = ESP.getHeapSize();
  uint32_t used  = total - ESP.getFreeHeap();
  int barW  = 300;
  int usedW = (int)((float)used / total * barW);
  usedW = max(2, min(usedW, barW - 2));

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Heap:", 10, 108);
  snprintf(buf, sizeof(buf), "%d%%", (int)((float)used / total * 100));
  tft.setTextDatum(TR_DATUM);
  tft.drawString(buf, 312, 108);
  tft.setTextDatum(TL_DATUM);

  tft.drawRect(10, 120, barW, 14, TFT_WHITE);
  tft.fillRect(11, 121, usedW - 2, 12, TFT_RED);
  tft.fillRect(10 + usedW, 121, barW - usedW - 1, 12, TFT_DARKGREEN);

  tft.setTextColor(tft.color565(50, 50, 50), TFT_BLACK);
  tft.drawString("3/3  System Info", 5, 230);
}

// ── Pane switching ────────────────────────────────────────────────────────────

void switchPane(int p) {
  currentPane = p;
  paneStart   = millis();

  switch (p) {
    case 0:
      drawStarWarsPane();
      break;
    case 1:
      tft.fillScreen(TFT_BLACK);
      initStars();
      tft.setTextDatum(TL_DATUM);
      tft.setTextSize(1);
      tft.setTextColor(tft.color565(50, 50, 50), TFT_BLACK);
      tft.drawString("2/3  Starfield", 5, 230);
      break;
    case 2:
      drawSystemPane();
      break;
  }
}

// ── Arduino entry points ──────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1); // landscape 320x240
  randomSeed(analogRead(36));
  switchPane(0);
}

void loop() {
  if (millis() - paneStart >= DURATIONS[currentPane])
    switchPane((currentPane + 1) % NUM_PANES);

  if (currentPane == 1) {
    tickStarfield();
    delay(20);
  }
}
