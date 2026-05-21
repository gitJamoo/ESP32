#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "config.h"

TFT_eSPI tft = TFT_eSPI();

struct Usage {
  int  sessionPct  = 0;   // 5h utilization 0-100
  int  sessionReset = 0;  // minutes until 5h window resets
  int  weeklyPct   = 0;   // 7d utilization 0-100
  int  weeklyReset  = 0;  // minutes until 7d window resets
  bool ok          = false;
};

Usage latest;
unsigned long lastFetchMs = 0;
char          lastSyncTime[8] = "--:--";
bool          wifiOk = false;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Format minutes as "47m", "2h 15m", or "3d 4h"
String fmtTime(int minutes) {
  if (minutes <= 0) return "now";
  if (minutes < 60) {
    char buf[8]; snprintf(buf, sizeof(buf), "%dm", minutes); return buf;
  }
  if (minutes < 1440) {
    char buf[12]; snprintf(buf, sizeof(buf), "%dh %dm", minutes / 60, minutes % 60); return buf;
  }
  char buf[12]; snprintf(buf, sizeof(buf), "%dd %dh", minutes / 1440, (minutes % 1440) / 60);
  return buf;
}

// ── Fetch ─────────────────────────────────────────────────────────────────────

void fetchUsage() {
  HTTPClient http;
  char url[64];
  snprintf(url, sizeof(url), "http://" PC_HOST ":%d/usage", PC_PORT);

  if (!http.begin(url)) return;
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { http.end(); return; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err || !doc["ok"].as<bool>()) return;

  latest.sessionPct   = doc["s"].as<int>();
  latest.sessionReset = doc["sr"].as<int>();
  latest.weeklyPct    = doc["w"].as<int>();
  latest.weeklyReset  = doc["wr"].as<int>();
  latest.ok           = true;

  // Capture sync time from NTP
  time_t now = time(nullptr);
  if (now > 1000000) strftime(lastSyncTime, sizeof(lastSyncTime), "%H:%M", localtime(&now));

  lastFetchMs = millis();
}

// ── Display ───────────────────────────────────────────────────────────────────

void drawBar(int x, int y, int w, int h, int pct) {
  int      fill   = max(0, min(w - 2, ((w - 2) * pct) / 100));
  uint16_t barCol = (pct >= 80) ? TFT_RED : (pct >= 60 ? TFT_YELLOW : TFT_GREEN);
  tft.drawRect(x, y, w, h, TFT_WHITE);
  if (fill > 0)
    tft.fillRect(x + 1, y + 1, fill, h - 2, barCol);
  if (fill < w - 2)
    tft.fillRect(x + 1 + fill, y + 1, w - 2 - fill, h - 2, tft.color565(35, 35, 35));
}

void drawFooter() {
  tft.fillRect(0, 175, 320, 65, TFT_BLACK);
  tft.drawFastHLine(0, 175, 320, tft.color565(55, 55, 55));

  char buf[64];
  if (!wifiOk) {
    snprintf(buf, sizeof(buf), "WiFi: connecting...");
  } else if (!latest.ok) {
    snprintf(buf, sizeof(buf), "Waiting for daemon...");
  } else {
    unsigned long nextIn = 5 - min(5UL, (millis() - lastFetchMs) / 60000UL);
    snprintf(buf, sizeof(buf), "Last sync: %s   next in %lum", lastSyncTime, nextIn);
  }
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(tft.color565(80, 80, 80), TFT_BLACK);
  tft.drawString(buf, 8, 182);
}

void drawScreen() {
  tft.fillScreen(TFT_BLACK);

  // ── Header ────────────────────────────────────────────────
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("CLAUDE USAGE", 160, 6);
  tft.drawFastHLine(0, 27, 320, tft.color565(55, 55, 55));

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  if (!latest.ok) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(!wifiOk ? "Connecting to WiFi..." : "Waiting for daemon on PC...", 160, 100);
    tft.setTextDatum(TL_DATUM);
    drawFooter();
    return;
  }

  // ── Session (5h) ──────────────────────────────────────────
  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.drawString("SESSION  (5h)", 8, 34);

  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", latest.sessionPct);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(latest.sessionPct >= 80 ? TFT_RED : TFT_WHITE, TFT_BLACK);
  tft.drawString(pctBuf, 314, 34);
  tft.setTextDatum(TL_DATUM);

  drawBar(8, 48, 304, 22, latest.sessionPct);

  tft.setTextColor(tft.color565(110, 110, 110), TFT_BLACK);
  tft.drawString(("Resets in " + fmtTime(latest.sessionReset)).c_str(), 8, 76);

  tft.drawFastHLine(0, 90, 320, tft.color565(55, 55, 55));

  // ── Weekly (7d) ───────────────────────────────────────────
  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.drawString("WEEKLY  (7d)", 8, 97);

  snprintf(pctBuf, sizeof(pctBuf), "%d%%", latest.weeklyPct);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(latest.weeklyPct >= 80 ? TFT_RED : TFT_WHITE, TFT_BLACK);
  tft.drawString(pctBuf, 314, 97);
  tft.setTextDatum(TL_DATUM);

  drawBar(8, 111, 304, 22, latest.weeklyPct);

  tft.setTextColor(tft.color565(110, 110, 110), TFT_BLACK);
  tft.drawString(("Resets in " + fmtTime(latest.weeklyReset)).c_str(), 8, 139);

  drawFooter();
}

// ── WiFi / NTP ────────────────────────────────────────────────────────────────

void connectWifi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting to WiFi...", 160, 110);
  tft.drawString(WIFI_SSID, 160, 124);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  wifiOk = (WiFi.status() == WL_CONNECTED);

  if (wifiOk) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm t;
    for (int i = 0; i < 20 && !getLocalTime(&t); i++) delay(500);
  }
}

// ── Entry points ──────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  connectWifi();
  if (wifiOk) fetchUsage();
  drawScreen();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiOk = false;
    WiFi.reconnect();
    delay(3000);
    wifiOk = (WiFi.status() == WL_CONNECTED);
  }

  if (wifiOk && (lastFetchMs == 0 || millis() - lastFetchMs >= 5UL * 60 * 1000)) {
    fetchUsage();
    drawScreen();
  } else {
    drawFooter();
  }

  delay(30000);
}
