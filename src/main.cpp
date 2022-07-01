#include <Arduino.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <WiFi.h>
// #include <HTTPSServer.hpp>
// #include <SSLCert.hpp>
// #include <HTTPRequest.hpp>
// #include <HTTPResponse.hpp>

#include <GxEPD2_BW.h>
#include <Fonts/Roboto_Regular4pt7b.h>
#include <Fonts/Roboto_Regular6pt7b.h>
#include <Fonts/Roboto_Regular8pt7b.h>
#include <Fonts/Roboto_Bold8pt7b.h>
#include <Fonts/Roboto_Light6pt7b.h>
#include <Fonts/Roboto_LightItalic6pt7b.h>

// #define MAX_DISPLAY_BUFFER_SIZE 131072ul // e.g. half of available ram
// #define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

#define POWER_BUTTON 21
#define ENTERPRISE_MODE 15

GxEPD2_BW<GxEPD2_270, GxEPD2_270::HEIGHT> display(GxEPD2_270(/*CS=5*/ SS, /*DC=*/17, /*RST=*/16, /*BUSY=*/4)); // GDEW027W3

// using namespace httpsserver;
// SSLCert *cert;

// HTTPSServer *server;
// cert = new SSLCert();

WebServer server(80);

uint8_t g_Power = 1;
uint8_t apmode = 0;
String hostname = "ESP-";
bool RESET = false;
bool GET = false;
int TIMER_COUNTER = 0;
int GET_PERIOD = 600; // frequency to post, in seconds
hw_timer_t *timer = NULL;

const char *quotesUrl = "https://api.quotable.io/random?tags=technology|success|business|inspirational|education|future|science|famout-quotes|life|literature|wisdom&maxLength=45";
String stockUrl = "https://query1.finance.yahoo.com/v8/finance/chart/";
String token = "?interval=1d";

// EEPROM addresses for state
const uint8_t SSID_INDEX = 1;
const uint8_t PASSWORD_INDEX = 2;
const uint8_t WIFI_SET = 3;
const uint8_t MDNS_INDEX = 4;
const uint8_t MDNS_SET = 5;
const uint8_t AP_SET = 6;
const uint8_t USERNAME_INDEX = 7;
const uint8_t IDENTITY_INDEX = 8;

// Position options for text on display
enum xPosition
{
  center,
  left,
  right
};

#include <wifi_utils.h>
#include <eeprom_utils.h>
#include <file_manager.h>

void IRAM_ATTR timer1_ISR(void)
{
  TIMER_COUNTER++;
  if (TIMER_COUNTER == GET_PERIOD) // update every 10 mins
  {
    GET = true;
    TIMER_COUNTER = 0;
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
        apmode = !apmode;
        EEPROM.write(AP_SET, apmode);
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

void setupWeb()
{
  server.on(
      "/wifi", HTTP_POST, []()
      {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String username = server.arg("username");
    String identity = server.arg("identity");
    try
    {
      writeWifiEEPROM(toCharArray(ssid), toCharArray(identity), toCharArray(username), toCharArray(password));
      server.send(200, "text/plain", "OK");
    }
    catch (const std::length_error &e)
    {
      StaticJsonDocument<32> root;
      root["error"] = e.what();
      String error;
      serializeJsonPretty(root, error);
      server.send(400, "application/json", error);
    } });

  server.on(
      "/wifi", HTTP_GET, []()
      {
      int ssidIndex = EEPROM.read(SSID_INDEX);
      int passwordIndex = EEPROM.read(PASSWORD_INDEX);
      int usernameIndex = EEPROM.read(USERNAME_INDEX);
      int identityIndex = EEPROM.read(IDENTITY_INDEX);
    try
    {
      StaticJsonDocument<256> root;
      root["password"] = EEPROM.readString(passwordIndex);
      root["ssid"] = EEPROM.readString(ssidIndex);
      root["username"] = EEPROM.readString(usernameIndex);
      root["identity"] = EEPROM.readString(identityIndex);
      String result;
      serializeJsonPretty(root, result);
      server.send(200, "application/json", result);
    }
    catch (const std::exception &e)
    {
      StaticJsonDocument<32> root;
      root["error"] = e.what();
      String error;
      serializeJsonPretty(root, error);
      server.send(400, "application/json", error);
    } });

  server.onNotFound([]()
                    {
    if (!handleFileRead(server.uri()))
    {
      server.send(404, "text/plain", "Not Found");
    } });
}

void printToDisplay(const char *text, int heightRatio, const GFXfont *font = &Roboto_Regular6pt7b, xPosition xpos = center)
{
  int16_t bbx = 264;
  int16_t bby = 176;
  uint16_t bbw = 0;
  uint16_t bbh = 0;

  display.setRotation(1);
  display.setFont(font);
  display.setTextColor(GxEPD_BLACK);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  int16_t tx, ty;
  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);

  switch (xpos)
  {
  case center:
    tx = max(0, ((display.width() - tbw) / 2));
    break;
  case left:
    tx = max(0, ((display.width() * 3 / 10 - tbw)));
    break;
  case right:
    tx = max(0, ((display.width() * 8 / 10 - tbw)));
    break;
  }

  ty = max(0, (display.height() * heightRatio / 100 - tbh / 2));

  bbx = min(bbx, tx);
  bby = min(bby, ty);
  bbw = max(bbw, tbw);
  bbh = max(bbh, tbh);
  // calculate the cursor
  uint16_t x = bbx - tbx;
  uint16_t y = bby - tby;

  // display.setFullWindow();
  display.setPartialWindow(bbx, bby, bbw, bbh);
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.println(text);
  } while (display.nextPage());
}

void getStocks(String ticker, xPosition xpos)
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
    String prevCloseString = String(prevClose);

    printToDisplay(symbol, 40, &Roboto_Bold8pt7b, xpos);
    printToDisplay(priceString.c_str(), 50, &Roboto_Regular6pt7b, xpos);
    // printToDisplay(prevCloseString.c_str(), 50);
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void getQuote()
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

    // char contentArray[contentString.size() + 1];

    // for (int i = 0; i < sizeof(contentArray); i++)
    // {
    //   contentArray[i] = contentString[i];
    // }
    printToDisplay(author, 82, &Roboto_Regular6pt7b);
    printToDisplay(contentString, 70, &Roboto_LightItalic6pt7b);
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
  while (!Serial)
  {
  }
  // int createCertResult = createSelfSignedCert(
  //     *cert,
  //     KEYSIZE_2048,
  //     "CN=myesp.local,O=acme,C=US");

  // if (createCertResult != 0)
  // {
  //   Serial.printf("Error generating certificate");
  //   return;
  // }
  // server = new HTTPSServer(cert);

  GET = true;
  apmode = EEPROM.read(AP_SET);
  setupWifi(hostname, ENTERPRISE_MODE);
  setupWeb();
  // SPIFFS.format(); // Prevents SPIFFS_ERR_NOT_A_FS
  SPIFFS.begin();
  server.begin();
  // server.enableCORS();

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
  server.handleClient();
  if (apmode == 0)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      String ip = WiFi.localIP().toString();
      String ipString = "WiFi addr: " + ip;
      printToDisplay(ipString.c_str(), 95, &Roboto_Regular4pt7b, left);

      if (GET)
      {
        getQuote();
        getStocks("ET.TO", left);
        getStocks("BTC-CAD", right);
        GET = false;
      }
    }
    else
    {
      printToDisplay("No WiFi", 95, &Roboto_Regular4pt7b, left);
    }
  }
  else
  {
    String statusString = "Hostname: " + hostname + "    IP: 192.168.4.1";
    printToDisplay(statusString.c_str(), 95, &Roboto_Regular4pt7b, left);
  }
};
