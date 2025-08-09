#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <ESP8266HTTPClient.h>
#include <time.h>
#include <Adafruit_BME280.h>
#include "config.h"
#include "icons.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define FONT_SMALL u8g2_font_spleen5x8_mf
#define UPDATE_INTERVAL 30 // minutes

const char* ssid = SSID;
const char* password = SSID_PASSWORD;

// OpenWeatherMap config
const String apiKey = APIKEY;
const String lat = (String)LAT;
const String lon = (String)LON;
const String units = "metric";
const uint8_t forecastNumber = 3;


const uint8_t LINE_SPACING = 4;
const uint8_t FONT_SMALL_HEIGHT = 8;
const uint8_t WEATHER_ICON_SIZE = 16;
const uint8_t ICON_SIZE = 8;
const uint8_t BOX_PADDING = 2;
const uint8_t BOX_RADIUS = 2;
const uint8_t BOX_HEIGHT = FONT_SMALL_HEIGHT + 2;
const uint8_t COLS = forecastNumber + 1;
const uint8_t COL_WIDTH = DISPLAY_WIDTH / COLS;				
const uint8_t BASE_Y = DISPLAY_HEIGHT - (WEATHER_ICON_SIZE + 2*BOX_HEIGHT);

U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0);

// Variabili meteo
uint8_t currentDay = 1;
uint8_t currentHum = 0;
float currentTemp = 0.0;
float forecastTemp[forecastNumber] = {0.0, 0.0, 0.0};
String forecastTimes[forecastNumber] = {"--:--", "--:--", "--:--"};
String forecastIcons[forecastNumber] = {"", "", ""};
String forecastDownloadTime = "--:--";
String currentTime = "00:00";
String currentDescription = "--";
String sunriseTime = "--:--";
String sunsetTime = "--:--";

unsigned long lastUpdate = 0;
const unsigned long updateInterval = UPDATE_INTERVAL * 60 * 1000; 

// Openweather API
const String currentWeatherURL = "https://api.openweathermap.org/data/2.5/weather?lat="+lat + "&lon="+lon + "&appid="+apiKey + "&units="+units+"&lang=it";
const String forecastURL =      "https://api.openweathermap.org/data/2.5/forecast?lat="+lat + "&lon="+lon + "&appid="+apiKey + "&cnt="+forecastNumber+ "&units="+units+"&lang=it";

Adafruit_BME280 bme;
float bmeTemp = 88.8; //placeholder
uint8_t bmeHum = 88;

// ---------------------- Funzioni ----------------------

void setupTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov"); 
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

String formatTimeFromEpoch(time_t epoch) {
    struct tm timeinfo;
    localtime_r(&epoch, &timeinfo);
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return String(buffer);
}

void wifiConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
}


String httpGETRequest(const char* serverName) {
    WiFiClientSecure client;
    client.setInsecure();  
    HTTPClient https;
    if (https.begin(client, serverName)) {
        int httpCode = https.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = https.getString();
            https.end();
            return payload;
        } else {
            https.end();
            return "{}";
        }
    } else {
        return "{}";
    }
}

void getWeatherData() {
    String currentWeather = httpGETRequest(currentWeatherURL.c_str());
    if (currentWeather == "{}") return;

    JsonDocument jsonCurrentWeather;
    if (deserializeJson(jsonCurrentWeather, currentWeather) != DeserializationError::Ok) return;

    currentHum = (int)jsonCurrentWeather["main"]["humidity"];
    currentTemp = (float)jsonCurrentWeather["main"]["temp"].as<double>();
    currentDescription = String(jsonCurrentWeather["weather"][0]["main"]);
    time_t sunrise = (time_t)jsonCurrentWeather["sys"]["sunrise"];
    time_t sunset = (time_t)jsonCurrentWeather["sys"]["sunset"];

    sunriseTime = formatTimeFromEpoch(sunrise);
    sunsetTime = formatTimeFromEpoch(sunset);
}


void getForecast() {
    String forecast = httpGETRequest(forecastURL.c_str());
    if (forecast == "{}") return;

    JsonDocument jsonForecast;
    if (deserializeJson(jsonForecast, forecast) != DeserializationError::Ok) return;

    time_t now;
    time(&now);
    forecastDownloadTime = formatTimeFromEpoch(now);

    for (int i = 0; i < forecastNumber; i++) {
        forecastTemp[i] = (float)jsonForecast["list"][i]["main"]["feels_like"].as<double>();
        forecastIcons[i] = String(jsonForecast["list"][i]["weather"][0]["main"]);
        String dt_txt = String(jsonForecast["list"][i]["dt_txt"]);
        forecastTimes[i] = dt_txt.substring(11, 16);
    }
}


void updateCurrentTime() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    currentTime = buffer;
    currentDay = timeinfo.tm_mday;
}

void readBME280() {
    bmeTemp = (float) bme.readTemperature();
    bmeHum = (int) bme.readHumidity();
}

void drawWeatherInfo() {
    u8g2.setFont(FONT_SMALL);

    // Temperatura BME280
    u8g2.drawXBMP(0, 0, 9, 9, icon_temp);
    u8g2.setCursor(12, FONT_SMALL_HEIGHT);
    u8g2.print(bmeTemp, 1);
    u8g2.print(" °C ");

    // Umidità BME280
    u8g2.drawXBMP(0, FONT_SMALL_HEIGHT + LINE_SPACING, 9, 9, icon_humidity);
    u8g2.setCursor(12, FONT_SMALL_HEIGHT * 2 + LINE_SPACING);
    u8g2.print(bmeHum);
    u8g2.print("%");

    // Alba
    int sunX = (DISPLAY_WIDTH / 2) - ICON_SIZE * 3;
    u8g2.drawXBMP(sunX, 1, ICON_SIZE, ICON_SIZE, icon_sunrise);
    u8g2.setCursor(sunX + ICON_SIZE + LINE_SPACING, FONT_SMALL_HEIGHT);
    u8g2.print(sunriseTime);

    // Tramonto
    u8g2.drawXBMP(sunX, ICON_SIZE + LINE_SPACING + 1, ICON_SIZE, ICON_SIZE, icon_sunset);
    u8g2.setCursor(sunX + ICON_SIZE + LINE_SPACING, FONT_SMALL_HEIGHT * 2 + LINE_SPACING);
    u8g2.print(sunsetTime);

    // Orologio in alto a destra
    int timeW = u8g2.getStrWidth(currentTime.c_str()) + BOX_PADDING * 2;
    int timeXpos = DISPLAY_WIDTH - timeW;
    u8g2.drawVLine(timeXpos - 4, 0, DISPLAY_HEIGHT);
    u8g2.setDrawColor(1);
    u8g2.drawRBox(timeXpos, 0, timeW, BOX_HEIGHT, BOX_RADIUS);
    u8g2.setDrawColor(0);
    u8g2.setCursor(timeXpos + BOX_PADDING, FONT_SMALL_HEIGHT);
    u8g2.print(currentTime);
    u8g2.setDrawColor(1);
}

void drawForecast() {
    u8g2.setFont(FONT_SMALL);

    const int colRemainder = DISPLAY_WIDTH % COLS;
    for (int i = 0; i < COLS; i++) {
        int extra = (i < colRemainder) ? 1 : 0;
        int x = i * COL_WIDTH + (i < colRemainder ? i : colRemainder);

        int iconX = x + (COL_WIDTH + extra - WEATHER_ICON_SIZE) / 2;
        u8g2.drawXBMP(iconX, BASE_Y, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE,
            (i < forecastNumber) ? getWeatherIcon(forecastIcons[i]) : getWeatherIcon(currentDescription));

        float temp = (i < forecastNumber) ? forecastTemp[i] : currentTemp;
        String tempStr = String(temp, 1);
        int tempW = u8g2.getStrWidth(tempStr.c_str());
        u8g2.setCursor(x + (COL_WIDTH + extra - tempW) / 2, BASE_Y + WEATHER_ICON_SIZE + FONT_SMALL_HEIGHT);
        u8g2.print(tempStr);

        String time = (i == COLS - 1) ? forecastDownloadTime : forecastTimes[i];
        int timeW = u8g2.getStrWidth(time.c_str()) + BOX_PADDING * 2;
        int timeXpos = x + (COL_WIDTH + extra - timeW) / 2;
        int timeY = BASE_Y + WEATHER_ICON_SIZE + FONT_SMALL_HEIGHT + 2;

        u8g2.setDrawColor(1);
        u8g2.drawRBox(timeXpos, timeY, timeW, BOX_HEIGHT, BOX_RADIUS);
        u8g2.setDrawColor(0);
        u8g2.setCursor(timeXpos + BOX_PADDING, timeY + FONT_SMALL_HEIGHT);
        u8g2.print(time);
        u8g2.setDrawColor(1);
    }
}



// ---------------------- Setup & Loop ----------------------

void setup() {
    Serial.begin(115200);
    u8g2.begin();
    bme.begin(0x76);

    wifiConnect();
    setupTime();
    getWeatherData();
    getForecast();
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF); 

    readBME280();
    updateCurrentTime();
    lastUpdate = millis();
}

void loop() {
    updateCurrentTime();
    readBME280();

    if (millis() - lastUpdate >= updateInterval) {
        wifiConnect();       // Riattiva Wi-Fi solo quando serve
        getWeatherData();
        getForecast();
				WiFi.disconnect(false);
				WiFi.mode(WIFI_OFF);
        lastUpdate = millis();
    }

    u8g2.firstPage();
    do {
        drawForecast();
        drawWeatherInfo();
    } while (u8g2.nextPage());

    delay(55000);
}