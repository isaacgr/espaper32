#include <Arduino.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "defines.h"
#include "wifi_utils.h"
#include "eeprom_utils.h"
#include "https.h"

#include <GxEPD2_BW.h>
#include <Fonts/Roboto_Regular4pt7b.h>
#include <Fonts/Roboto_Regular6pt7b.h>
#include <Fonts/Roboto_Regular8pt7b.h>
#include <Fonts/Roboto_Bold8pt7b.h>
#include <Fonts/Roboto_Light6pt7b.h>
#include <Fonts/Roboto_LightItalic6pt7b.h>

// #define MAX_DISPLAY_BUFFER_SIZE 131072ul // e.g. half of available ram
// #define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

GxEPD2_BW<GxEPD2_270, GxEPD2_270::HEIGHT> display(GxEPD2_270(/*CS=5*/ SS, /*DC=*/17, /*RST=*/16, /*BUSY=*/4)); // GDEW027W3

/*****************
  GLOBALS
******************/
uint8_t g_Power = 1;
uint8_t apMode = 0;
String hostname = "ESP-";
bool RESET = false;
bool GET_QUOTE = false;
bool GET_STOCKS = false;
bool GET_TRAVEL_TIME = false;
int GET_QUOTE_COUNTER = 0;
int GET_STOCKS_COUNTER = 0;
int GET_TRAVEL_TIME_COUNTER = 0;
int GET_PERIOD = 60;
hw_timer_t *timer = NULL;

const char *quotesUrl = "https://api.quotable.io/random?tags=technology|success|business|inspirational|education|future|science|famous-quotes|life|literature|wisdom&maxLength=45";
const char *tavelTimeUrl = "https://dev.virtualearth.net/REST/V1/Routes/Driving?o=json&wp.0=43.39252853393555,-79.77173614501953&wp.1=43.251670837402344,-79.88003540039062&avoid=minimizeTolls&key=AnIS3IEKS30ivDfBr0AWq36z04STmWOiwPsGbECvRwh7kxHEEHqYlEiRVsMMvmvM&routeAttributes=routeSummariesOnly";
const char *tavelTimeToGymUrl = "https://dev.virtualearth.net/REST/V1/Routes/Driving?o=json&wp.0=43.39252853393555,-79.77173614501953&wp.1=43.2515715,-79.8475542&avoid=minimizeTolls&key=AnIS3IEKS30ivDfBr0AWq36z04STmWOiwPsGbECvRwh7kxHEEHqYlEiRVsMMvmvM&routeAttributes=routeSummariesOnly";

String stockUrl = "https://query1.finance.yahoo.com/v8/finance/chart/";
String token = "?interval=1d";

HTTPSServer *server;

void IRAM_ATTR timer1_ISR(void)
{
  GET_QUOTE_COUNTER++;
  GET_STOCKS_COUNTER++;
  GET_TRAVEL_TIME_COUNTER++;

  if (GET_STOCKS_COUNTER == GET_PERIOD * 10)
  {
    GET_STOCKS = true;
    GET_STOCKS_COUNTER = 0;
  }
  if (GET_TRAVEL_TIME_COUNTER == GET_PERIOD * 30)
  {
    GET_TRAVEL_TIME = true;
    GET_TRAVEL_TIME_COUNTER = 0;
  }
  if (GET_QUOTE_COUNTER == GET_PERIOD * 1440)
  {
    GET_QUOTE = true;
    GET_QUOTE_COUNTER = 0;
  }
}

void IRAM_ATTR POWER_ISR()
{
  static unsigned long last_interrupt_time = 0;
  static unsigned long rise_time;
  static unsigned long fall_time;

  unsigned long interrupt_time = millis();

  unsigned long toggle_ap_mode_time = 2500; // toggle ap mode if button held for 3s
  unsigned long factory_reset_time = 9500;  // clear eeprom if button held for 10s

  uint8_t pinState = digitalRead(POWER_BUTTON);
  if (pinState == 1)
  {
    rise_time = millis();
  }
  else
  {
    fall_time = millis();
    // If interrupts come faster than 200ms, assume it's a bounce and ignore
    if (interrupt_time - last_interrupt_time > 200)
    {
      unsigned long diff = fall_time - rise_time;
      if (diff < toggle_ap_mode_time)
      {
        // assume user wants power off
        g_Power = !g_Power;
      }
      else if (diff >= toggle_ap_mode_time && diff < factory_reset_time)
      {
        apMode = !apMode;
        EEPROM.write(AP_SET, apMode);
        RESET = true;
      }
      else if (diff >= factory_reset_time)
      {
        for (int i = 0; i < 512; i++)
        {
          EEPROM.write(i, 0);
        }
        EEPROM.write(AP_SET, 0);
        RESET = true;
      }
    }
    last_interrupt_time = interrupt_time;
  }
}

void printToDisplay(const char *text, uint16_t windowX, uint16_t windowY, uint16_t windowW, uint16_t windowH, bool inverted = false, const GFXfont *font = &Roboto_Light6pt7b)
{
  bool test = false;
  display.setRotation(1);
  display.setFont(font);
  display.setTextColor(GxEPD_BLACK);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  uint16_t x = windowX + (windowW - tbw) / 2;
  uint16_t y;
  uint16_t wy;
  if (inverted)
  {
    y = display.height() - ((windowH - tbh - (tby / 2)) / 2) - windowY;
    wy = display.height() - windowH - windowY;
  }
  else
  {
    y = (windowH + tbh + (tby / 2)) / 2;
    wy = windowY;
  }
  display.setPartialWindow(windowX, wy, windowW, windowH);
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    if (test)
    {
      display.fillRect(windowX, wy, windowW, windowH, GxEPD_BLACK);
    }
    display.setCursor(x, y);
    display.println(text);
  } while (display.nextPage());
}

void getTravelTime(const char *url, String location, xPosition xPos)
{
  HTTPClient http;

  http.begin(url);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0)
  {
    StaticJsonDocument<1536> doc;
    DeserializationError error = deserializeJson(doc, http.getString());
    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    JsonObject result = doc["resourceSets"][0]["resources"][0];
    float travelDurationTraffic = result["travelDurationTraffic"];
    String travelDurationString = String(travelDurationTraffic / 60) + "m to " + location;

    if (xPos == top)
    {
      printToDisplay(travelDurationString.c_str(), display.width() / 2, display.height() * 65 / 100, display.width() / 2, 20, true);
    }
    else if (xPos == bottom)
    {
      printToDisplay(travelDurationString.c_str(), display.width() / 2, display.height() * 50 / 100, display.width() / 2, 20, true);
    }
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void getStocks(String stockUrl, String ticker, xPosition xPos)
{

  HTTPClient http;

  http.begin(stockUrl + ticker + token);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0)
  {
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, http.getStream());

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    JsonObject chart_result = doc["chart"]["result"][0];
    const char *symbol = chart_result["meta"]["symbol"];
    float price = chart_result["meta"]["regularMarketPrice"];
    float prevClose = chart_result["meta"]["chartPreviousClose"];

    String priceString = String(price);
    String direction = "<";

    if (price < prevClose)
    {
      direction = ">";
    }

    String value = direction + " " + priceString;

    if (xPos == left)
    {
      printToDisplay(symbol, 0, display.height() * 60 / 100, display.width() / 4, 30, true, &Roboto_Bold8pt7b);
      printToDisplay(value.c_str(), 0, display.height() * 48 / 100, display.width() / 4, 20, true);
    }
    else if (xPos == right)
    {
      printToDisplay(symbol, display.width() / 4, display.height() * 60 / 100, display.width() / 4, 30, true, &Roboto_Bold8pt7b);
      printToDisplay(value.c_str(), display.width() / 4, display.height() * 48 / 100, display.width() / 4, 20, true);
    }
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void getQuote(const char *quotesUrl)
{

  HTTPClient http;
  http.begin(quotesUrl);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0)
  {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, http.getStream());

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    const char *contentString = doc["content"];
    const char *author = doc["author"];

    printToDisplay(contentString, 0, display.height() * 25 / 100, display.width(), 25, true, &Roboto_LightItalic6pt7b);
    printToDisplay(author, 0, display.height() * 14 / 100, display.width(), 15, true);
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void setup()
{

  if (!EEPROM.begin(512))
  {
    Serial.println("Failed to initialize EEPROM!");
    return;
  }
  Serial.begin(115200);
  // Try to mount SPIFFS without formatting on failure
  if (!SPIFFS.begin(false))
  {
    // If SPIFFS does not work, we wait for serial connection...
    while (!Serial)
      ;
    delay(1000);

    // Ask to format SPIFFS using serial interface
    Serial.print("Mounting SPIFFS failed. Try formatting? (y/n): ");
    while (!Serial.available())
      ;
    Serial.println();

    // If the user did not accept to try formatting SPIFFS or formatting failed:
    if (Serial.read() != 'y' || !SPIFFS.begin(true))
    {
      Serial.println("SPIFFS not available. Stop.");
      while (true)
        ;
    }
    else
    {
      SPIFFS.format();
    }
    Serial.println("SPIFFS has been formated.");
  }
  Serial.println("SPIFFS has been mounted.");
  SPIFFS.begin();

  // Now that SPIFFS is ready, we can create or load the certificate
  SSLCert *cert = getCertificate();
  if (cert == NULL)
  {
    Serial.println("Could not load certificate. Stop.");
    while (true)
      ;
  }

  server = new HTTPSServer(cert);

  // We register the SPIFFS handler as the default node, so every request that does
  // not hit any other node will be redirected to the file system.
  ResourceNode *spiffsNode = new ResourceNode("", "", &handleSPIFFS);
  server->setDefaultNode(spiffsNode);

  // Add a handler that serves the current system uptime at GET /api/uptime
  ResourceNode *getWifiNode = new ResourceNode("/api/wifi", "GET", &handleGetWifi);
  server->registerNode(getWifiNode);
  ResourceNode *postWifiNode = new ResourceNode("/api/wifi", "POST", &handlePostWifi);
  server->registerNode(postWifiNode);

  GET_QUOTE = true;
  GET_STOCKS = true;
  GET_TRAVEL_TIME = true;

  apMode = EEPROM.read(AP_SET);
  setupWifi(hostname, apMode, ENTERPRISE_MODE);

  Serial.println("Starting server...");
  server->start();
  if (server->isRunning())
  {
    Serial.println("Server ready.");
  }

  display.init(115200);

  // Setup interrupts
  noInterrupts();
  attachInterrupt(POWER_BUTTON, POWER_ISR, CHANGE);
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &timer1_ISR, true);
  timerAlarmWrite(timer, 1000000, true);
  timerAlarmEnable(timer);
  interrupts();
}

void loop()
{
  if (RESET)
  {
    RESET = !RESET;
    EEPROM.commit();
    ESP.restart();
  }

  if (apMode == 0)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      String ip = WiFi.localIP().toString();
      String ipString = "IP: " + ip;
      printToDisplay(ipString.c_str(), 0, 0, display.width() / 2, 10, true, &Roboto_Regular4pt7b);
      if (GET_QUOTE)
      {
        getQuote(quotesUrl);
        GET_QUOTE = false;
      }
      if (GET_STOCKS)
      {
        getStocks(stockUrl, "ET.TO", left);
        getStocks(stockUrl, "BTC-CAD", right);
        GET_STOCKS = false;
      }
      if (GET_TRAVEL_TIME)
      {
        getTravelTime(tavelTimeUrl, "home", top);
        getTravelTime(tavelTimeToGymUrl, "the gym", bottom);
        GET_TRAVEL_TIME = false;
      }
    }
    else
    {
      printToDisplay("No WiFi", 0, 0, display.width() / 2, 10, true, &Roboto_Regular4pt7b);
    }
  }
  else
  {
    server->loop();
    String statusString = "Hostname: " + hostname + "    IP: 192.168.4.1";
    printToDisplay(statusString.c_str(), 0, 0, display.width(), 10, true, &Roboto_Regular4pt7b);
  }
};
