#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <ESP8266HTTPClient.h>
#include <time.h>
#include "config.h"

const char* ssid = SSID;
const char* password = SSID_PASSWORD;

// OpenWeatherMap config
const String apiKey = APIKEY;
const String city = CITY; //e.g. "Ancona,it"
const String units = "metric";

// Display SSD1309 I2C 128x64
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0);

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

// Dati meteo e orario
String currentTemp = "--";
String currentDescription = "--";
float forecastTemp[6];
String forecastTimes[6];
String currentTime = "--:--";
int currentDay = 1;
int humidity = 0;
int pressure = 0;
String sunriseTime = "--:--";
String sunsetTime = "--:--";

// Weather update interval
const unsigned long updateInterval = UPDATE_INTERVAL * 60 * 1000; //UPDATE_INTERVAL [minutes]
unsigned long lastUpdate = 0;

// Freccia animata
int arrowPos = 0;
bool arrowDirection = true;

// Icone bitmap (16x16) -- qui definisci le icone come già mostrato
// ... (per brevità le puoi copiare da sopra)

String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, serverName);
  int httpResponseCode = http.GET();
  String payload = "{}";
  if (httpResponseCode == 200) payload = http.getString();
  http.end();
  return payload;
}

void fetchWeatherData() {
  String path = "http://api.openweathermap.org/data/2.5/forecast?q=" + city + "&appid=" + apiKey + "&units=" + units;
  String response = httpGETRequest(path.c_str());
  StaticJsonDocument<8192> doc;
  if (deserializeJson(doc, response)) return;
  currentTemp = String(doc["list"][0]["main"]["temp"].as<float>(), 1) + "°C";
  currentDescription = doc["list"][0]["weather"][0]["main"].as<String>();
  humidity = doc["list"][0]["main"]["humidity"].as<int>();
  pressure = doc["list"][0]["main"]["pressure"].as<int>();
  for (int i = 0; i < 6; i++) {
	forecastTemp[i] = doc["list"][i]["main"]["temp"].as<float>();
	forecastTimes[i] = String(doc["list"][i]["dt_txt"].as<String>()).substring(11, 16);
  }
}

void fetchSunTimes() {
  String path = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "&appid=" + apiKey + "&units=" + units;
  String response = httpGETRequest(path.c_str());
  StaticJsonDocument<4096> doc;
  if (deserializeJson(doc, response)) return;
  time_t sunrise = doc["sys"]["sunrise"].as<time_t>();
  time_t sunset = doc["sys"]["sunset"].as<time_t>();
  struct tm *ti;
  ti = localtime(&sunrise);
  sunriseTime = (ti->tm_hour < 10 ? "0" : "") + String(ti->tm_hour) + ":" + (ti->tm_min < 10 ? "0" : "") + String(ti->tm_min);
  ti = localtime(&sunset);
  sunsetTime = (ti->tm_hour < 10 ? "0" : "") + String(ti->tm_hour) + ":" + (ti->tm_min < 10 ? "0" : "") + String(ti->tm_min);
}

void updateCurrentTime() {
  time_t raw = timeClient.getEpochTime();
  struct tm *ti = localtime(&raw);
  currentTime = (ti->tm_hour < 10 ? "0" : "") + String(ti->tm_hour) + ":" + (ti->tm_min < 10 ? "0" : "") + String(ti->tm_min);
  currentDay = ti->tm_mday;
}

void drawCalendar() {
  if (currentDay < 1 || currentDay > 31) currentDay = 1;
  int day = 1;
  for (int y = 0; y < 5; y++) {
    for (int x = 0; x < 7 && day <= 31; x++) {
      int px = 2 + x * 16;
      int py = 10 + y * 10;
      if (day == currentDay) {
        u8g2.drawBox(px - 1, py - 8, 14, 10);
        u8g2.setDrawColor(0);
        u8g2.setCursor(px, py);
        u8g2.print(day);
        u8g2.setDrawColor(1);
      } else {
        u8g2.setCursor(px, py);
        u8g2.print(day);
      }
      day++;
    }
  }
}


void drawCurrentWeather() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(85, 10); u8g2.print(currentTemp);
  u8g2.setCursor(85, 20); u8g2.print(currentDescription);
  u8g2.setCursor(85, 30); u8g2.print(currentTime);
}

void drawForecast() {
  u8g2.setFont(u8g2_font_5x8_tr);
  for (int i = 0; i < 6; i++) {
	u8g2.setCursor(5 + i * 20, 45); u8g2.print(String(forecastTemp[i], 0) + "°");
	u8g2.setCursor(5 + i * 20, 55); u8g2.print(forecastTimes[i]);
  }
}

void drawArrow() {
  int y = 60, x = 5 + arrowPos * 20;
  u8g2.drawTriangle(x, y, x + 5, y - 3, x + 5, y + 3);
}

void drawHumidityPressure() {
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(85, 40); u8g2.print("H:"); u8g2.print(humidity); u8g2.print("%");
  u8g2.setCursor(85, 48); u8g2.print("P:"); u8g2.print(pressure); u8g2.print("hPa");
}

void drawSunTimes() {
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(85, 56); u8g2.print("S:"); u8g2.print(sunriseTime);
  u8g2.setCursor(85, 64); u8g2.print("s:"); u8g2.print(sunsetTime);
}

//-------------------------------------------------------------//

void setup() {
  Serial.begin(115200);
  Serial.println("\n------Setup------");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(300);
  Serial.println("Wireless intensity:" + String(WiFi.RSSI() ));
  Serial.println("------Wifi Connected------");
  timeClient.begin();
  Serial.println("------Time client started------");
  u8g2.begin();
  Serial.println("------LCD started------");
  fetchWeatherData();
  Serial.println("------Weather data fetched------");
  fetchSunTimes();
  timeClient.update();
  Serial.println("------Time client updated------");
  updateCurrentTime();
  Serial.println("------Current Time updated------");
  lastUpdate = millis();
}

void loop() {
  timeClient.update();
  updateCurrentTime();
  if (millis() - lastUpdate >= updateInterval) {
    fetchWeatherData();
    Serial.println("------Weather data fetched------");
    fetchSunTimes();
    lastUpdate = millis();
  }
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvR08_tr);
  arrowPos += arrowDirection ? 1 : -1;
  if (arrowPos > 5) arrowDirection = false;
  if (arrowPos < 0) arrowDirection = true;
  u8g2.firstPage();
  do {
    //drawCalendar();
    Serial.println("------Drawing Weather on LCD------");
    drawCurrentWeather();
    Serial.println("------Drawing Forecast on LCD------");
    drawForecast();
    Serial.println("------Drawing Arrow on LCD------");
    drawArrow();
    Serial.println("------Drawing Humidity on LCD------");
    drawHumidityPressure();
    Serial.println("------Drawing Sun Times on LCD------");
    drawSunTimes();
  } 
  while (u8g2.nextPage());
  u8g2.sendBuffer();
  Serial.println("------Next page on LCD------");
  delay(500);
}
