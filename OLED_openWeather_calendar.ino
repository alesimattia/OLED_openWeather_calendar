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
#define UPDATE_INTERVAL 30 //minutes

const uint8_t FONT_SMALL_HEIGHT = 8;
const uint8_t FONT_SMALL_WIDTH = 5;
const uint8_t LINE_SPACING = 4;

const char* ssid = SSID;
const char* password = SSID_PASSWORD;

// OpenWeatherMap config
const String apiKey = APIKEY;
const String lat = (String)LAT;
const String lon = (String)LON;
const String units = "metric";
const short forecastNumber = 3;
const short TIMEZONE = 1; //UTC+1 => Rome

const uint8_t WEATHER_ICON_SIZE = 16;
const uint8_t ICON_SIZE = 8;
const uint8_t BOX_PADDING = 2;
const uint8_t BOX_RADIUS = 2;
const uint8_t BOX_HEIGHT = FONT_SMALL_HEIGHT + 2;
const uint8_t COLS = forecastNumber + 1; //one column for current weather
const uint8_t COL_WIDTH = DISPLAY_WIDTH / COLS;				//two text lines
const uint8_t BASE_Y = DISPLAY_HEIGHT - (WEATHER_ICON_SIZE + 2*BOX_HEIGHT);


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
String forecastTimes[forecastNumber] = {"--:--", "--:--", "--:--"};
String forecastIcons[forecastNumber] = {"", "", ""};
String forecastDownloadTime = "--:--";
String currentTime = "00:00";
String currentDescription = "--";
String sunriseTime = "00:00";
String sunsetTime = "00:00";

unsigned long lastUpdate = 0;
const unsigned long updateInterval = UPDATE_INTERVAL * 60 * 1000;  //[minutes]

/*** https://openweathermap.org/api/hourly-forecast ***/
const String currentWeatherURL = "https://api.openweathermap.org/data/2.5/weather?lat="+lat + "&lon="+lon + "&appid="+apiKey + "&units="+units+"&lang=it";
const String forecastURL =      "https://api.openweathermap.org/data/2.5/forecast?lat="+lat + "&lon="+lon + "&appid="+apiKey + "&cnt="+forecastNumber+ "&units="+units+"&lang=it";


String httpGETRequest(const char* serverName) {
	Serial.println(String(serverName));

	WiFiClientSecure client;
	client.setInsecure();  // Disabilita la verifica del certificato (solo per test)

	HTTPClient https;
	if (https.begin(client, serverName)) {
		int httpCode = https.GET();
		if (httpCode == HTTP_CODE_OK) {
			String payload = https.getString();
			Serial.println("\n\n"+payload);
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
		Serial.println("Err. parsing JSON currentWeather: " + String(error.c_str()));
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

	Serial.println("Current Day: " + String(currentDay));
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
    if(forecast!="") Serial.println("\n Forecast data downloaded");

    JsonDocument jsonForecast;
    DeserializationError error = deserializeJson(jsonForecast, forecast);
    if (error != DeserializationError::Ok) {
        Serial.println("Err. parsing JSON forecast: " + String(error.c_str()));
        return;
    }

    for (int i = 0; i < forecastNumber; i++) {
        forecastTemp[i] = (double)jsonForecast["list"][i]["main"]["feels_like"].as<double>();
        forecastIcons[i] = String(jsonForecast["list"][i]["weather"][0]["main"]);
        String dt_txt = String(jsonForecast["list"][i]["dt_txt"]);
        forecastTimes[i] = dt_txt.substring(11, 16).c_str();

        Serial.printf("Previsione +%s h: %.1f°C | %s\n",
                    forecastTimes[i].c_str(), forecastTemp[i], forecastIcons[i].c_str());
    }
}


void updateCurrentTime() {
		time_t raw = timeClient.getEpochTime();
		raw += TIMEZONE * 3600;
		struct tm* ti = localtime(&raw);
		currentTime = (ti->tm_hour < 10 ? "0" : "") + String(ti->tm_hour) + ":" + (ti->tm_min < 10 ? "0" : "") + String(ti->tm_min);
		currentDay = ti->tm_mday;
		if (currentDay < 1 || currentDay > 31) currentDay = 1;  // Fallback in case of invalid day
}


void drawWeatherInfo() {
		float bmeTemp = 88.8;
		int bmeHum = 88;

		u8g2.setFont(FONT_SMALL);

		// Temperatura BME280 in alto a sinistra
		u8g2.drawXBMP(0, 0, 9, 9, icon_temp);
		u8g2.setCursor(12, FONT_SMALL_HEIGHT);
		u8g2.print(bmeTemp, 1);
		u8g2.print(" °C ");

		// Umidità BME280 sotto la temperatura
		u8g2.drawXBMP(0, FONT_SMALL_HEIGHT + LINE_SPACING, 9, 9, icon_humidity);
		u8g2.setCursor(12, FONT_SMALL_HEIGHT * 2 + LINE_SPACING);
		u8g2.print(bmeHum);
		u8g2.print("%");

	
		int sunX = (DISPLAY_WIDTH / 2) - ICON_SIZE * 3;
		// Sunrise
		u8g2.drawXBMP(sunX, 1, ICON_SIZE, ICON_SIZE, icon_sunrise);
		u8g2.setCursor(sunX + ICON_SIZE + LINE_SPACING, FONT_SMALL_HEIGHT);
		u8g2.print(sunriseTime);

		// Sunset
		u8g2.drawXBMP(sunX, ICON_SIZE + LINE_SPACING + 1, ICON_SIZE, ICON_SIZE, icon_sunset);
		u8g2.setCursor(sunX + ICON_SIZE + LINE_SPACING, FONT_SMALL_HEIGHT * 2 + LINE_SPACING);
		u8g2.print(sunsetTime);

		// Clock at top right corner
		int timeW = u8g2.getStrWidth(currentTime.c_str()) + BOX_PADDING * 2;
		int timeXpos = DISPLAY_WIDTH - timeW;
		// Vertical line dividing current weather (right) and forecasts (left)
		u8g2.drawVLine(timeXpos - 4, 0, DISPLAY_HEIGHT);


		u8g2.setDrawColor(1);
		u8g2.drawRBox(timeXpos, 0, timeW, BOX_HEIGHT, BOX_RADIUS);
		u8g2.setDrawColor(0);
		u8g2.setCursor(timeXpos + BOX_PADDING, FONT_SMALL_HEIGHT);
		u8g2.print(currentTime);
		u8g2.setDrawColor(1);
}


void drawForecast() {
    // Calcola larghezza colonna in modo che la prima tocchi il bordo sinistro e l'ultima il destro
    const int colRemainder = DISPLAY_WIDTH % COLS; // Per compensare eventuali pixel persi

    for (int i = 0; i < COLS; i++) {
        // Per compensare eventuali pixel persi, aggiungi 1px alle prime 'colRemainder' colonne
        int extra = (i < colRemainder) ? 1 : 0;
        int x = i * COL_WIDTH + (i < colRemainder ? i : colRemainder);

        // Icona meteo
        int iconX = x + (COL_WIDTH + extra - WEATHER_ICON_SIZE) / 2;
        u8g2.drawXBMP(iconX, BASE_Y, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE,
            i < 3 ? getWeatherIcon(forecastIcons[i]) : getWeatherIcon(currentDescription));

        // Temperatura sotto l'icona
        float temp = i < 3 ? forecastTemp[i] : currentTemp;
        String tempStr = String(temp, 1);
        int tempW = u8g2.getStrWidth(tempStr.c_str());
        u8g2.setCursor(x + (COL_WIDTH + extra - tempW) / 2, BASE_Y + WEATHER_ICON_SIZE + FONT_SMALL_HEIGHT);
        u8g2.print(tempStr);

        // Orario sotto la temperatura in box bianco stondato
        String time = i < 3 ? forecastTimes[i] : forecastDownloadTime;
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


void setup() {
	Serial.begin(115200);
	Serial.println("\nSetup");

	WiFi.mode(WIFI_STA);
	WiFi.setPhyMode(WIFI_PHY_MODE_11G);
	WiFi.hostname("ESP-host");
	
	WiFi.begin(ssid, password);
	while (WiFi.status() != WL_CONNECTED) {
		Serial.print("Connecting to WIFI... status:");
		Serial.println(WiFi.status());
		delay(500);
	}
	Serial.print(WiFi.localIP());
	Serial.println(" signal: " + String(WiFi.RSSI()));

	timeClient.begin();
	timeClient.update();
	updateCurrentTime();

	u8g2.begin();
	u8g2.clearBuffer();
	u8g2.setFont(FONT_SMALL);

	getWeatherData();
	getForecast();
	lastUpdate = millis();
}


void loop() {
	Serial.println("\n-------------");
	Serial.print("Connected!");
	Serial.print(WiFi.localIP());
	Serial.print(" signal: " + String(WiFi.RSSI()));

	timeClient.update();
	updateCurrentTime();

	if ( (millis() - lastUpdate >= updateInterval) && (WiFi.status() == WL_CONNECTED) ){
		getWeatherData();
		getForecast();
		forecastDownloadTime = currentTime;
		lastUpdate = millis();
	}

	u8g2.clearBuffer();
	u8g2.firstPage();
	do {
		drawForecast();
		drawWeatherInfo();
	} 
	while (u8g2.nextPage());

	delay(55000);
}