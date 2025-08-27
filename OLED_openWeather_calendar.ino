#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <ESP8266HTTPClient.h>
#include <time.h>
#include <Adafruit_BME280.h>
#include <user_interface.h> // for light sleep
//#include <c_types.h>

#include "config.h"
#include "icons.h"


#ifdef ESP01 // in Config.h
    #define SCL 0
    #define SDA 2
#endif

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define FONT_SMALL u8g2_font_spleen5x8_mf
#define UPDATE_INTERVAL 30 // minuti

const char* ssid = SSID;
const char* password = SSID_PASSWORD;

// OpenWeatherMap config
const String apiKey = APIKEY;
const String lat = (String)LAT;
const String lon = (String)LON;
const String units = "metric";
const uint8_t forecastNumber = 4; // show only 3 future forecasts

const uint8_t LINE_SPACING = 4;
const uint8_t FONT_SMALL_HEIGHT = 8;
const uint8_t WEATHER_ICON_SIZE = 16;
const uint8_t ICON_SIZE = 8;
const uint8_t BOX_PADDING = 2;
const uint8_t BOX_RADIUS = 2;
const uint8_t BOX_HEIGHT = FONT_SMALL_HEIGHT + 2;
const uint8_t BASE_Y = DISPLAY_HEIGHT - (WEATHER_ICON_SIZE + 2 * BOX_HEIGHT);

U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0);

// Variabili meteo
uint8_t currentDay = 1;
uint8_t currentHum = 0;
float currentTemp = 0.0;
float forecastTemp[3] = {0.0, 0.0, 0.0};
String forecastTimes[3] = {"--:--", "--:--", "--:--"};
String forecastIcons[3] = {"", "", ""};
String forecastDownloadTime = "--:--";
String currentTime = "00:00";
String currentDescription = "--";
String sunriseTime = "--:--";
String sunsetTime = "--:--";

unsigned long lastUpdate = 0;
const unsigned long updateInterval = UPDATE_INTERVAL * 60 * 1000;
const unsigned long lightSleepTime = 30UL * 1e6UL; // in uSeconds

// API URLs
const String currentWeatherURL =
    "https://api.openweathermap.org/data/2.5/weather?lat=" + lat +
    "&lon=" + lon +
    "&appid=" + apiKey +
    "&units=" + units + "&lang=it";

const String forecastURL =
    "https://api.openweathermap.org/data/2.5/forecast?lat=" + lat +
    "&lon=" + lon +
    "&appid=" + apiKey +
    "&cnt=" + String(forecastNumber) +
    "&units=" + units + "&lang=it";

Adafruit_BME280 bme;
float bmeTemp = 88.8; // placeholder
uint8_t bmeHum = 88;



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
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println(" Connected!" + WiFi.localIP().toString());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
}

String httpGETRequest(const char* serverName) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    Serial.println(String("Richiesta HTTP: ") + serverName);
    if (https.begin(client, serverName)) {
        int httpCode = https.GET();
        Serial.print("Codice risposta HTTP: ");
        Serial.println(httpCode);
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
    if (deserializeJson(jsonCurrentWeather, currentWeather) != DeserializationError::Ok) {
        Serial.println("Errore parse meteo attuale");
        return;
    }

    currentHum = (int)jsonCurrentWeather["main"]["humidity"];
    currentTemp = (float)jsonCurrentWeather["main"]["temp"].as<double>();
    currentDescription = String(jsonCurrentWeather["weather"][0]["main"]);
    time_t sunrise = (time_t)jsonCurrentWeather["sys"]["sunrise"];
    time_t sunset = (time_t)jsonCurrentWeather["sys"]["sunset"];

    sunriseTime = formatTimeFromEpoch(sunrise);
    sunsetTime = formatTimeFromEpoch(sunset);

    Serial.print("Meteo attuale: ");
    Serial.print(currentDescription);
    Serial.print(", ");
    Serial.print(currentTemp);
    Serial.print("°C, Umidità: ");
    Serial.println(currentHum);
}

void getForecast() {
    String forecast = httpGETRequest(forecastURL.c_str());
    if (forecast == "{}") return;

    JsonDocument jsonForecast;
    DeserializationError err = deserializeJson(jsonForecast, forecast);
    if (err != DeserializationError::Ok) {
        Serial.print("deserializeJson forecast error: ");
        Serial.println(err.f_str());
        return;
    }

    time_t now;
    time(&now);
    forecastDownloadTime = formatTimeFromEpoch(now);

    // reset placeholders (se non vengono riempite rimangono --:--)
    for (uint8_t k = 0; k < 3; k++) {
        forecastTemp[k] = 0.0;
        forecastIcons[k] = "";
        forecastTimes[k] = "--:--";
    }

    uint8_t filled = 0;
    size_t listSize = jsonForecast["list"].size();
    Serial.println("Previsioni ricevute (dt epoch UTC -> orario locale):");
    for (size_t i = 0; i < listSize && filled < 3; i++) {
        // dt in epoch (UTC)
        long dt_val = jsonForecast["list"][i]["dt"].as<long>();
        time_t forecastEpoch = (time_t)dt_val;

        Serial.print("  entry "); Serial.print(i);
        Serial.print(" dt(utc): "); Serial.print((unsigned long)forecastEpoch);
        Serial.print(" now(utc): "); Serial.print((unsigned long)now);

        if (forecastEpoch > now) { // solo future rispetto all'epoch corrente
            // prendo feels_like (come facevi tu)
            forecastTemp[filled] = (float)jsonForecast["list"][i]["main"]["feels_like"].as<double>();
            forecastIcons[filled] = String(jsonForecast["list"][i]["weather"][0]["main"]);
            // -> converto l'epoch in orario locale per la visualizzazione
            forecastTimes[filled] = formatTimeFromEpoch(forecastEpoch);

            Serial.print(" -> KEEP ");
            Serial.print(forecastTimes[filled]);
            Serial.print(" temp: ");
            Serial.print(forecastTemp[filled]);
            Serial.print(" icon: ");
            Serial.println(forecastIcons[filled]);

            filled++;
        } else {
            Serial.println(" -> SKIP (passata)");
        }
    }

    if (filled < 3) {
        Serial.print("Attenzione: trovate solo ");
        Serial.print(filled);
        Serial.println(" previsioni future (potrebbe essere necessario aumentare cnt).");
    }
}

void readBME280() {
    bmeTemp = (float)bme.readTemperature();
    bmeHum = (int)bme.readHumidity();
}

void drawWeatherInfo() {
    u8g2.setFont(FONT_SMALL);

    // Temp BME280
    u8g2.drawXBMP(0, 0, 9, 9, icon_temp);
    u8g2.setCursor(12, FONT_SMALL_HEIGHT);
    u8g2.print(bmeTemp, 1);
    u8g2.print(" °C ");

    // Umidità
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

    uint8_t totalCols = 3 + 1; // 3 future + 1 attuale
    const int colWidth = DISPLAY_WIDTH / totalCols;
    const int colRemainder = DISPLAY_WIDTH % totalCols;

    for (int i = 0; i < totalCols; i++) {
        int extra = (i < colRemainder) ? 1 : 0;
        int x = i * colWidth + (i < colRemainder ? i : colRemainder);

        const uint8_t* iconData = (i < totalCols - 1)
            ? getWeatherIcon(forecastIcons[i])
            : getWeatherIcon(currentDescription);

        int iconX = x + (colWidth + extra - WEATHER_ICON_SIZE) / 2;
        u8g2.drawXBMP(iconX, BASE_Y, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, iconData);

        float temp = (i < totalCols - 1) ? forecastTemp[i] : currentTemp;
        String tempStr = String(temp, 1);
        int tempW = u8g2.getStrWidth(tempStr.c_str()) + BOX_PADDING * 2;
        int tempXpos = x + (colWidth + extra - tempW) / 2;
        int tempY = BASE_Y + WEATHER_ICON_SIZE;

        // Rounded square around Temps.
        u8g2.setDrawColor(1);
        u8g2.drawRBox(tempXpos, tempY, tempW, BOX_HEIGHT, BOX_RADIUS);
        u8g2.setDrawColor(0);
        u8g2.setCursor(tempXpos + BOX_PADDING, tempY + FONT_SMALL_HEIGHT);
        u8g2.print(tempStr);
        u8g2.setDrawColor(1);


        String time = (i < totalCols - 1) ? forecastTimes[i] : forecastDownloadTime;
        int timeW = u8g2.getStrWidth(time.c_str());
        int timeXpos = x + (colWidth + extra - timeW) / 2;
        int timeY = BASE_Y + WEATHER_ICON_SIZE + FONT_SMALL_HEIGHT + 2;
        u8g2.setCursor(timeXpos, timeY + FONT_SMALL_HEIGHT);
        u8g2.print(time);
    }
}



void setup() {
    Serial.begin(115200);
    if (ESP01) Wire.begin(SDA, SCL);

    u8g2.begin();
    bme.begin(0x76);

    wifiConnect();

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    // POSIX TZ string per CET/CEST (va bene su ESP8266)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    //getWeatherData();
    //getForecast();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    readBME280();
    updateCurrentTime();
    lastUpdate = millis();

    wifi_set_sleep_type(LIGHT_SLEEP_T);
}


void loop() {
    updateCurrentTime();
    readBME280();

    if (millis() - lastUpdate >= updateInterval) {
        wifiConnect();
        getWeatherData();
        getForecast();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        lastUpdate = millis();
    }

    u8g2.firstPage();
    do {
        drawForecast();
        drawWeatherInfo();
    } while (u8g2.nextPage());

    wifi_fpm_do_sleep(lightSleepTime);
}