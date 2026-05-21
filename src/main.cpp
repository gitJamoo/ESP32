#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "config.h"

TFT_eSPI tft = TFT_eSPI();

struct UsageData {
  long input  = 0;
  long output = 0;
  long total  = 0;
  bool valid  = false;
};

UsageData monthly;
UsageData session;
unsigned long lastFetchMs = 0;
char          lastSyncTime[8] = "--:--";
bool          wifiOk          = false;

// ── Helpers ───────────────────────────────────────────────────────────────────

String fmtNum(long n) {
  String s   = String(n);
  int    pos = s.length() - 3;
  while (pos > 0) { s = s.substring(0, pos) + "," + s.substring(pos); pos -= 3; }
  return s;
}

void toISO(char* buf, size_t len, time_t t) {
  strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
}

// ── API ───────────────────────────────────────────────────────────────────────

bool fetchUsage(const char* startISO, const char* endISO,
                const char* bucketWidth, int limit, UsageData& out) {
  WiFiClientSecure client;
  client.setInsecure(); // home device — skip cert pinning
  client.setTimeout(15);

  HTTPClient http;
  char url[300];
  snprintf(url, sizeof(url),
    "https://api.anthropic.com/v1/organizations/usage_report/messages"
    "?starting_at=%s&ending_at=%s&bucket_width=%s&limit=%d",
    startISO, endISO, bucketWidth, limit);

  if (!http.begin(client, url)) return false;
  http.addHeader("anthropic-version", "2023-06-01");
  http.addHeader("x-api-key", ANTHROPIC_ADMIN_KEY);
  http.setTimeout(15000);

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  long inputSum = 0, outputSum = 0;
  for (JsonObject bucket : doc["data"].as<JsonArray>()) {
    for (JsonObject r : bucket["results"].as<JsonArray>()) {
      inputSum  += r["uncached_input_tokens"].as<long>();
      inputSum  += r["cache_read_input_tokens"].as<long>();
      inputSum  += r["cache_creation"]["ephemeral_1h_input_tokens"].as<long>();
      inputSum  += r["cache_creation"]["ephemeral_5m_input_tokens"].as<long>();
      outputSum += r["output_tokens"].as<long>();
    }
  }

  out = { inputSum, outputSum, inputSum + outputSum, true };
  return true;
}

void fetchAll() {
  time_t now = time(nullptr);
  if (now < 1000000) return; // NTP not synced

  char nowStr[30], monthStartStr[30], sessionStartStr[30];
  toISO(nowStr, sizeof(nowStr), now);
  toISO(sessionStartStr, sizeof(sessionStartStr), now - 5 * 3600);

  // Start of current UTC month
  struct tm t = *gmtime(&now);
  t.tm_mday = 1; t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
  toISO(monthStartStr, sizeof(monthStartStr), mktime(&t));

  fetchUsage(monthStartStr, nowStr, "1d", 31, monthly);
  fetchUsage(sessionStartStr, nowStr, "1h",  5, session);

  strftime(lastSyncTime, sizeof(lastSyncTime), "%H:%M", gmtime(&now));
  lastFetchMs = millis();
}

// ── Display ───────────────────────────────────────────────────────────────────

void drawFooter() {
  tft.fillRect(0, 163, 320, 240 - 163, TFT_BLACK);
  tft.drawFastHLine(0, 163, 320, tft.color565(60, 60, 60));

  char buf[64];
  if (!wifiOk) {
    snprintf(buf, sizeof(buf), "WiFi: connecting...");
  } else if (!monthly.valid) {
    snprintf(buf, sizeof(buf), "Fetching from Anthropic...");
  } else {
    unsigned long nextIn = 5 - min(5UL, (millis() - lastFetchMs) / 60000UL);
    snprintf(buf, sizeof(buf), "Last sync: %s   next in %lum", lastSyncTime, nextIn);
  }

  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(tft.color565(80, 80, 80), TFT_BLACK);
  tft.drawString(buf, 8, 170);
}

void drawScreen() {
  tft.fillScreen(TFT_BLACK);

  // ── Header ────────────────────────────────────────────────
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("CLAUDE API USAGE", 160, 5);
  tft.drawFastHLine(0, 26, 320, tft.color565(60, 60, 60));

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  // ── Monthly ───────────────────────────────────────────────
  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.drawString("THIS MONTH", 8, 31);

  if (!monthly.valid) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Fetching...", 8, 44);
  } else {
    auto row = [&](int y, const char* label, String val, uint16_t col) {
      tft.setTextColor(tft.color565(110, 110, 110), TFT_BLACK);
      tft.drawString(label, 8, y);
      tft.setTextColor(col, TFT_BLACK);
      tft.drawString(val.c_str(), 60, y);
    };
    row(44, "In:",    fmtNum(monthly.input)  + " tk", TFT_WHITE);
    row(56, "Out:",   fmtNum(monthly.output) + " tk", TFT_WHITE);
    row(68, "Total:", fmtNum(monthly.total),           TFT_CYAN);

    // Progress bar
    float    pct   = min(1.0f, (float)monthly.total / MONTHLY_TOKEN_LIMIT);
    int      barW  = 300;
    int      fillW = max(0, (int)(pct * barW));
    uint16_t barCol = (pct >= 0.9f) ? TFT_RED : TFT_GREEN;

    tft.drawRect(10, 81, barW, 12, TFT_WHITE);
    if (fillW > 2)          tft.fillRect(11,          82, fillW - 2,      10, barCol);
    if (fillW < barW - 1)   tft.fillRect(11 + fillW,  82, barW - fillW - 1, 10, tft.color565(35, 35, 35));

    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)(pct * 100));
    tft.setTextColor(tft.color565(100, 100, 100), TFT_BLACK);
    tft.drawString((fmtNum(monthly.total) + " of " + fmtNum(MONTHLY_TOKEN_LIMIT)).c_str(), 8, 96);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(pctBuf, 312, 96);
    tft.setTextDatum(TL_DATUM);
  }

  tft.drawFastHLine(0, 108, 320, tft.color565(60, 60, 60));

  // ── Session ───────────────────────────────────────────────
  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.drawString("SESSION  (last 5h)", 8, 113);

  if (!session.valid) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Fetching...", 8, 126);
  } else {
    auto row = [&](int y, const char* label, String val, uint16_t col) {
      tft.setTextColor(tft.color565(110, 110, 110), TFT_BLACK);
      tft.drawString(label, 8, y);
      tft.setTextColor(col, TFT_BLACK);
      tft.drawString(val.c_str(), 60, y);
    };
    row(126, "In:",    fmtNum(session.input)  + " tk", TFT_WHITE);
    row(138, "Out:",   fmtNum(session.output) + " tk", TFT_WHITE);
    row(150, "Total:", fmtNum(session.total),           TFT_CYAN);
  }

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
  if (wifiOk) fetchAll();
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
    fetchAll();
    drawScreen();
  } else {
    drawFooter(); // update the countdown without a full redraw
  }

  delay(30000);
}
