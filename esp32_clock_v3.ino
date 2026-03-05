#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#include <ArduinoJson.h>
#include <math.h>

// ===== TFT PINS (ESP32-S3 wiring) =====
#define TFT_CS   47
#define TFT_DC   16
#define TFT_RST  15

// SPI pins (ESP32-S3 wiring)
#define TFT_SCK  19
#define TFT_MOSI 21
#define TFT_MISO 20   // not connected; dummy pin for SPI.begin()

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ===== WiFi =====
const char* WIFI_SSID = "Blazelton Pl";
const char* WIFI_PASS = "hazelton123";

// ===== Time (NTP) =====
const char* NTP_SERVER = "pool.ntp.org";

// ===== Weather (OpenWeather Current Weather API) =====
const char* OWM_API_KEY = "65075dca0eec5fb9c43ddc3b35e2a2ca"; // to be hidden later
const char* LAT = "48.47313001189578";
const char* LON = "-123.30691458915048";
const char* UNITS = "metric";    // "metric" or "imperial"

// Refresh intervals
const unsigned long WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL; // 10 minutes

unsigned long lastWeatherFetch = 0;

struct WeatherData {
  float temp = NAN;
  int humidity = -1;
  String icon = "";
  String desc = "";
} weather;

// Track last drawn time/day to avoid flicker
int lastDrawnMinute = -1;
int lastDrawnDay    = -1;

// ---------- Helpers ----------
String getDaySuffix(int day) {
  if (day >= 11 && day <= 13) return "th";
  switch (day % 10) {
    case 1: return "st";
    case 2: return "nd";
    case 3: return "rd";
    default: return "th";
  }
}

static inline String capitalizeFirst(String s) {
  if (s.length() > 0) s.setCharAt(0, toupper(s[0]));
  return s;
}

// ---------- Simple icon drawing (local; no downloads) ----------
void drawSun(int x, int y) {
  tft.fillCircle(x, y, 18, ILI9341_YELLOW);
  for (int i = 0; i < 8; i++) {
    float a = i * 3.14159f / 4.0f;
    int x1 = x + (int)(26 * cos(a));
    int y1 = y + (int)(26 * sin(a));
    int x2 = x + (int)(36 * cos(a));
    int y2 = y + (int)(36 * sin(a));
    tft.drawLine(x1, y1, x2, y2, ILI9341_YELLOW);
  }
}

void drawCloud(int x, int y) {
  tft.fillCircle(x - 18, y, 14, ILI9341_LIGHTGREY);
  tft.fillCircle(x, y - 10, 18, ILI9341_LIGHTGREY);
  tft.fillCircle(x + 18, y, 14, ILI9341_LIGHTGREY);
  tft.fillRect(x - 32, y, 64, 20, ILI9341_LIGHTGREY);
}

void drawRain(int x, int y) {
  drawCloud(x, y);
  for (int i = -20; i <= 20; i += 10) {
    tft.drawLine(x + i, y + 26, x + i - 4, y + 40, ILI9341_CYAN);
  }
}

void drawSnow(int x, int y) {
  drawCloud(x, y);
  for (int i = -15; i <= 15; i += 10) {
    tft.drawLine(x + i, y + 28, x + i, y + 40, ILI9341_WHITE);
    tft.drawLine(x + i - 4, y + 34, x + i + 4, y + 34, ILI9341_WHITE);
  }
}

void drawWeatherIcon(const String& iconCode, int x, int y) {
  // clear icon area
  tft.fillRect(x - 40, y - 40, 80, 80, ILI9341_BLACK);

  if (iconCode.startsWith("01")) {
    drawSun(x, y);
  } else if (iconCode.startsWith("02") || iconCode.startsWith("03") || iconCode.startsWith("04")) {
    drawCloud(x, y);
  } else if (iconCode.startsWith("09") || iconCode.startsWith("10")) {
    drawRain(x, y);
  } else if (iconCode.startsWith("13")) {
    drawSnow(x, y);
  } else {
    drawCloud(x, y);
  }
}

// ---------- Weather fetch ----------
bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = String("https://api.openweathermap.org/data/2.5/weather?lat=") +
               LAT + "&lon=" + LON +
               "&units=" + UNITS +
               "&appid=" + OWM_API_KEY;

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  weather.temp = doc["main"]["temp"].as<float>();
  weather.humidity = doc["main"]["humidity"].as<int>();
  weather.desc = doc["weather"][0]["description"].as<String>();
  weather.icon = doc["weather"][0]["icon"].as<String>();

  return true;
}

// ---------- UI drawing ----------
void drawTimeIfNeeded(const tm& timeinfo) {
  if (timeinfo.tm_min == lastDrawnMinute) return;
  lastDrawnMinute = timeinfo.tm_min;

  // Clear just the time area
  tft.fillRect(0, 0, 320, 60, ILI9341_BLACK);

  char timeBuf[16];
  strftime(timeBuf, sizeof(timeBuf), "%I:%M %p", &timeinfo); // no seconds

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(5);
  tft.setCursor(12, 8);
  tft.print(timeBuf);
}

void drawDateIfNeeded(const tm& timeinfo) {
  if (timeinfo.tm_mday == lastDrawnDay) return;
  lastDrawnDay = timeinfo.tm_mday;

  // Clear just the date area
  tft.fillRect(0, 60, 320, 45, ILI9341_BLACK);

  char wday[8];
  strftime(wday, sizeof(wday), "%a", &timeinfo); // Thu

  char mon[8];
  strftime(mon, sizeof(mon), "%b", &timeinfo);   // Feb

  int day = timeinfo.tm_mday;
  String suffix = getDaySuffix(day);

  // Example: "Thu, Feb 19th"
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(3);
  tft.setCursor(12, 68);
  tft.print(wday);
  tft.print(", ");
  tft.print(mon);
  tft.print(" ");
  tft.print(day);
  tft.print(suffix);
}

void drawWeatherPanel() {
  // Clear weather area
  tft.fillRect(0, 110, 320, 130, ILI9341_BLACK);

  // Icon frame + icon
  tft.drawRect(230, 110, 80, 80, ILI9341_DARKGREY);
  drawWeatherIcon(weather.icon, 270, 150);

  // Temp big
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(4);
  tft.setCursor(12, 120);
  if (!isnan(weather.temp)) {
    tft.print(weather.temp, 0);
    tft.print(UNITS[0] == 'm' ? "C" : "F");
  } else {
    tft.print("--");
  }

  // Humidity
  tft.setTextSize(2);
  tft.setCursor(12, 165);
  tft.print("Humidity: ");
  if (weather.humidity >= 0) {
    tft.print(weather.humidity);
    tft.print("%");
  } else {
    tft.print("--");
  }

  // Description
  tft.setCursor(12, 190);
  tft.print(capitalizeFirst(weather.desc));
}

// ---------- WiFi ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);

  tft.setCursor(10, 10);
  tft.print("Connecting WiFi");

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    tft.setCursor(10, 40);
    tft.print("                ");
    tft.setCursor(10, 40);
    tft.print("Wait");
    for (int i = 0; i < dots; i++) tft.print(".");
    dots = (dots + 1) % 4;
  }
}

void setup() {
  Serial.begin(115200);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);

  connectWiFi();

  // NTP + Pacific time (auto DST)
  configTime(0, 0, NTP_SERVER);
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();

  // Draw initial time/date (force redraw)
  lastDrawnMinute = -1;
  lastDrawnDay = -1;

  // Weather
  tft.setTextSize(2);
  tft.setCursor(12, 120);
  tft.print("Getting weather...");

  if (fetchWeather()) {
    drawWeatherPanel();
  } else {
    tft.setCursor(12, 150);
    tft.print("Weather failed");
  }

  lastWeatherFetch = millis();
}

void loop() {
  // Update time/date without flicker (only on change)
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    drawTimeIfNeeded(timeinfo);
    drawDateIfNeeded(timeinfo);
  }

  // Update weather periodically
  unsigned long now = millis();
  if (now - lastWeatherFetch >= WEATHER_REFRESH_MS) {
    lastWeatherFetch = now;
    if (fetchWeather()) {
      drawWeatherPanel();
    }
  }

  delay(200); // reduces CPU + avoids hammering getLocalTime()
}