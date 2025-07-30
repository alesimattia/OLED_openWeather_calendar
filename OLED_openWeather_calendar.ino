#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <ESP8266HTTPClient.h>
#include <time.h>
#include "config.h"
#include "icons.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

//#define FONT_SMALL u8g2_font_5x8_mf
#define FONT_SMALL u8g2_font_spleen5x8_mf

const uint8_t FONT_SMALL_HEIGHT = 8;
const uint8_t FONT_SMALL_WIDTH = 5;

const uint8_t ICON_SIZE = 16;
const uint8_t ARROW_SIZE = 8;
const uint8_t BOX_PADDING = 3;
const uint8_t BOX_RADIUS = 2;
const uint8_t BOX_HEIGHT = FONT_SMALL_HEIGHT + 2;
const uint8_t COLS = 4;
const uint8_t COL_WIDTH = DISPLAY_WIDTH / COLS;
const uint8_t CENTER_X = DISPLAY_WIDTH / 2;					//two text lines
const uint8_t BASE_Y = DISPLAY_HEIGHT - (ICON_SIZE + 2*BOX_HEIGHT);


const char* ssid = SSID;
const char* password = SSID_PASSWORD;

// OpenWeatherMap config
const String apiKey = APIKEY;
const String lat = (String)LAT;
const String lon = (String)LON;
const String units = "metric";
const short forecastNumber = 3;

// Display SSD1309 I2C 128x64
/* https://github.com/olikraus/u8g2/wiki/u8g2setupc */
U8G2_SSD1309_128X64_NONAME0_1_HW_I2C u8g2(U8G2_R0);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);


int currentDay = 1;
int currentHum = 0;
//int currentPress = 0;
double currentTemp = 0.0;
double forecastTemp[forecastNumber] = {0.0, 0.0, 0.0};
String forecastTimes[forecastNumber];
String forecastIcons[forecastNumber];

String currentTime = "00:00";
String currentDescription = "--";
String sunriseTime = "00:00";
String sunsetTime = "00:00";

unsigned long lastUpdate = 0;
const unsigned long updateInterval = UPDATE_INTERVAL * 60 * 1000;  //[minutes]

/*** https://openweathermap.org/api/hourly-forecast ***/
const String currentWeatherURL = "https://api.openweathermap.org/data/2.5/weather?lat="+lat + "&lon="+lon + "&appid="+apiKey + "&units=metric&lang=it";
const String forecastURL =      "https://api.openweathermap.org/data/2.5/forecast?lat="+lat + "&lon="+lon + "&appid="+apiKey + "&cnt="+forecastNumber+ "&units=metric&lang=it";


String httpGETRequest(const char* serverName) {
	Serial.println(String(serverName));

	WiFiClientSecure client;
	client.setInsecure();  // Disabilita la verifica del certificato (solo per test)

	HTTPClient https;
	if (https.begin(client, serverName)) {
		int httpCode = https.GET();
		if (httpCode == HTTP_CODE_OK) {
			String payload = https.getString();
			https.end();
			return payload;
		} 
		else {
			String error = String(https.errorToString(httpCode));
			Serial.println("[HTTPS] GET... failed, error:\n" + error);
			https.end();
			return error;
		}
	} 
	else {
		Serial.println("[HTTPS] Unable to connect");
		return "{}";
	}
}


void getWeatherData() {
	String currentWeather = httpGETRequest(currentWeatherURL.c_str());

	JsonDocument jsonCurrentWeather;
	DeserializationError error = deserializeJson(jsonCurrentWeather, currentWeather);
	if (error != DeserializationError::Ok) {
		Serial.println("Error parsing JSON : " + String(error.c_str()));
		return;
	}

	currentHum = (int)jsonCurrentWeather["main"]["humidity"];
	currentTemp = (double)jsonCurrentWeather["main"]["temp"].as<double>();
	currentDescription = String(jsonCurrentWeather["weather"][0]["main"]);
	time_t sunrise = (time_t)jsonCurrentWeather["sys"]["sunrise"];
	time_t sunset = (time_t)jsonCurrentWeather["sys"]["sunset"];

	struct tm* ti;
	ti = localtime(&sunrise);
	sunriseTime = (ti->tm_hour < 10 ? "0" : "") + String(ti->tm_hour) + ":" + (ti->tm_min < 10 ? "0" : "") + String(ti->tm_min);
	ti = localtime(&sunset);
	sunsetTime = (ti->tm_hour < 10 ? "0" : "") + String(ti->tm_hour) + ":" + (ti->tm_min < 10 ? "0" : "") + String(ti->tm_min);

	Serial.println("Current Humidity: " + String(currentHum));
	Serial.println("Current Temperature: " + String(currentTemp));
	Serial.println("Current Description: " + currentDescription);
	//currentPress = jsonCurrentWeather["main"]["currentPress"].as<int>();
	Serial.println("Sunrise Time: " + sunriseTime);
	Serial.println("Sunset Time: " + sunsetTime);
	Serial.println("Current Weather:\n" + currentWeather);
}



void getForecast() {
	String forecast = httpGETRequest(forecastURL.c_str());

	JsonDocument jsonForecast;
	DeserializationError error = deserializeJson(jsonForecast, forecast);
	if (error != DeserializationError::Ok) {
		Serial.println("Error parsing JSON : " + String(error.c_str()));
		return;
	}

	for (int i = 0; i < forecastNumber; i++) {
		forecastTemp[i] = (double)jsonForecast["list"][i]["main"]["feels_like"].as<double>();
		forecastIcons[i] = String(jsonForecast["list"][i]["weather"][0]["main"]);
		String dt_txt = String(jsonForecast["list"][i]["dt_txt"]);
		forecastTimes[i] = dt_txt.substring(11, 16);

		Serial.printf("Previsione +%s h: %.1f°C | %s\n",
					forecastTimes[i].c_str(), forecastTemp[i], forecastIcons[i].c_str());
	}
}



void updateCurrentTime() {
	time_t raw = timeClient.getEpochTime();
	struct tm* ti = localtime(&raw);
	currentTime = (ti->tm_hour < 10 ? "0" : "") + String(ti->tm_hour) + ":" + (ti->tm_min < 10 ? "0" : "") + String(ti->tm_min);
	currentDay = ti->tm_mday;
	if (currentDay < 1 || currentDay > 31) currentDay = 1;  // Fallback in case of invalid day
															//Serial.println("Current time: " + currentTime + " | Day: " + String(currentDay));
}



void drawWeatherInfo() {
    float bmeTemp = 88.8;
    int bmeHum = 88;

    u8g2.setFont(FONT_SMALL);

    // Temperatura BME280 in alto a sinistra
    u8g2.setCursor(0, FONT_SMALL_HEIGHT);
    u8g2.print("T ");
    u8g2.print(bmeTemp, 1);
    u8g2.print(" C");

    // Umidità BME280 sotto la temperatura
    u8g2.setCursor(0, FONT_SMALL_HEIGHT * 2 + 2);
    u8g2.print("H  ");
    u8g2.print(bmeHum);
    u8g2.print("%");

    // Alba e tramonto al centro
    int sunX = CENTER_X - ARROW_SIZE * 3;
    // Alba
    u8g2.drawXBMP(sunX, 2, ARROW_SIZE, ARROW_SIZE, icon_sunrise);
    u8g2.setCursor(sunX + ARROW_SIZE + 2, FONT_SMALL_HEIGHT);
    u8g2.print(sunriseTime);

    // Tramonto
    u8g2.drawXBMP(sunX, FONT_SMALL_HEIGHT + 4, ARROW_SIZE, ARROW_SIZE, icon_sunset);
    u8g2.setCursor(sunX + ARROW_SIZE + 2, FONT_SMALL_HEIGHT * 2 + 2);
    u8g2.print(sunsetTime);

    // Orario attuale in alto a destra
    int timeW = u8g2.getStrWidth(currentTime.c_str()) + BOX_PADDING * 2;
    int timeX = DISPLAY_WIDTH - timeW;
    u8g2.setDrawColor(1);
    u8g2.drawRBox(timeX, 0, timeW, BOX_HEIGHT, BOX_RADIUS);
    u8g2.setDrawColor(0);
    u8g2.setCursor(timeX + BOX_PADDING, FONT_SMALL_HEIGHT);
    u8g2.print(currentTime);
    u8g2.setDrawColor(1);
}


void drawForecast() {

    for (int i = 0; i < COLS; i++) {
        int x = i * COL_WIDTH;

        // Icona meteo
        int iconX = x + (COL_WIDTH - ICON_SIZE) / 2;
        u8g2.drawXBMP(iconX, BASE_Y, ICON_SIZE, ICON_SIZE,
            i < 3 ? getWeatherIcon(forecastIcons[i]) : getWeatherIcon(currentDescription));

        // Temperatura sotto l'icona
        String temp = String(i < 3 ? forecastTemp[i] : currentTemp, 0) + " C";
        int tempW = u8g2.getStrWidth(temp.c_str());
        u8g2.setCursor(x + (COL_WIDTH - tempW) / 2, BASE_Y + ICON_SIZE + FONT_SMALL_HEIGHT);
        u8g2.setFont(u8g2_font_5x8_mf);
		u8g2.print(temp);
		u8g2.setFont(FONT_SMALL);

        // Orario sotto la temperatura in box bianco stondato
        String time = i < 3 ? forecastTimes[i] : currentTime;
        int timeW = u8g2.getStrWidth(time.c_str()) + BOX_PADDING * 2;
        int timeX = x + (COL_WIDTH - timeW) / 2;
        int timeY = BASE_Y + ICON_SIZE + FONT_SMALL_HEIGHT + 2;

        u8g2.setDrawColor(1);
        u8g2.drawRBox(timeX, timeY, timeW, BOX_HEIGHT, BOX_RADIUS);
        u8g2.setDrawColor(0);
        u8g2.setCursor(timeX + BOX_PADDING, timeY + FONT_SMALL_HEIGHT);
        u8g2.print(time);
        u8g2.setDrawColor(1);
    }
}


void setup() {
	Serial.begin(115200);
	Serial.println("\nSetup");
	WiFi.forceSleepBegin();
	delay(100);
	WiFi.mode(WIFI_OFF);
	delay(100);
	WiFi.mode(WIFI_STA);
	delay(100);
	WiFi.forceSleepWake();
	delay(100);
  	WiFi.setPhyMode(WIFI_PHY_MODE_11G);
	WiFi.hostname("ESP-host");
	WiFi.setSleepMode(WIFI_NONE_SLEEP);
	
	WiFi.begin(ssid, password);
	/*while (WiFi.status() != WL_CONNECTED) {
		Serial.print(WiFi.status());
		delay(500);
	}*/
	Serial.print(WiFi.localIP());
	Serial.println("  signal: " + String(WiFi.RSSI()) );


	timeClient.begin();

	u8g2.begin();
	u8g2.clearBuffer();
	u8g2.setFont(FONT_SMALL);

	getWeatherData();
	getForecast();
	Serial.println("-----Weather data downloaded-----");

	timeClient.update();
	updateCurrentTime();
	lastUpdate = millis();

	for (int i = 0; i < forecastNumber; i++) {
    forecastTimes[i] = "--:--";
		forecastIcons[i] = "---";
	}
}


void loop() {
	u8g2.clearBuffer();
	timeClient.update();
	updateCurrentTime();

	if (millis() - lastUpdate >= updateInterval) {
		getWeatherData();
		getForecast();
		lastUpdate = millis();
	}

	u8g2.firstPage();
	do {
		drawForecast();
		drawWeatherInfo();
	} while (u8g2.nextPage());
	delay(500);
}