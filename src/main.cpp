#include <Arduino.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPSServer.hpp>
#include <SSLCert.hpp>
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <util.hpp>
#include <string>

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

// EEPROM addresses for state
#define SSID_INDEX 1
#define PASSWORD_INDEX 2
#define WIFI_SET 3
#define MDNS_INDEX 4
#define MDNS_SET 5
#define AP_SET 6
#define USERNAME_INDEX 7
#define IDENTITY_INDEX 8

// We need to specify some content-type mapping, so the resources get delivered with the
// right content type and are displayed correctly in the browser
char contentTypes[][2][32] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpg"},
    {"", ""}};

GxEPD2_BW<GxEPD2_270, GxEPD2_270::HEIGHT> display(GxEPD2_270(/*CS=5*/ SS, /*DC=*/17, /*RST=*/16, /*BUSY=*/4)); // GDEW027W3

using namespace httpsserver;
void handleSPIFFS(HTTPRequest *req, HTTPResponse *res);
void handleGetWifi(HTTPRequest *req, HTTPResponse *res);
void handlePostWifi(HTTPRequest *req, HTTPResponse *res);

HTTPSServer *server;
SSLCert *getCertificate();

uint8_t g_Power = 1;
uint8_t apmode = 0;
String hostname = "ESP-";
bool RESET = false;
bool GET = false;
int TIMER_COUNTER = 0;
int GET_PERIOD = 600;
hw_timer_t *timer = NULL;

enum xPosition
{
  left,
  right
};

const char *quotesUrl = "https://api.quotable.io/random?tags=technology|success|business|inspirational|education|future|science|famous-quotes|life|literature|wisdom&maxLength=45";
String stockUrl = "https://query1.finance.yahoo.com/v8/finance/chart/";
String token = "?interval=1d";

#include <wifi_utils.h>
#include <eeprom_utils.h>

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

bool certFileExists()
{
  // Try to open key and cert file to see if they exist
  bool result = false;
  File keyFile = SPIFFS.open("/certs/key.der");
  File certFile = SPIFFS.open("/certs/cert.der");

  if (!keyFile || !certFile || keyFile.size() == 0 || certFile.size() == 0)
  {
    result = false;
  }
  else
  {
    if (keyFile)
      keyFile.close();
    if (certFile)
      certFile.close();
    result = true;
  }
  return result;
}

/**
 * This function will either read the certificate and private key from SPIFFS or
 * create a self-signed certificate and write it to SPIFFS for next boot
 */
SSLCert *getCertificate()
{
  SSLCert *cert;

  // Try to open key and cert file to see if they exist, if not, make them
  if (certFileExists())
  {
    Serial.println(F("Certificate FOUND in SPIFFS"));
    Serial.println(F("Reading certificate from SPIFFS."));
    File keyFile = SPIFFS.open("/certs/key.der");
    File certFile = SPIFFS.open("/certs/cert.der");

    // The files exist, so we can create a certificate based on them
    size_t keySize = keyFile.size();
    size_t certSize = certFile.size();

    uint8_t *keyBuffer = new uint8_t[keySize];
    if (keyBuffer == NULL)
    {
      Serial.println(F("Not enough memory to load private key"));
    }
    uint8_t *certBuffer = new uint8_t[certSize];
    if (certBuffer == NULL)
    {
      delete[] keyBuffer;
      Serial.println(F("Not enough memory to load certificate"));
    }
    keyFile.read(keyBuffer, keySize);
    certFile.read(certBuffer, certSize);

    // Close the files
    keyFile.close();
    certFile.close();
    Serial.printf("Read %u bytes of certificate and %u bytes of key from SPIFFS\n", certSize, keySize);
    cert = new SSLCert(certBuffer, certSize, keyBuffer, keySize);
  }
  else
  {
    Serial.println(F("No prexisting certificate found in SPIFFS"));
  }
  return cert;
}

/**
 * This handler function will try to load the requested resource from SPIFFS's /public folder.
 *
 * If the method is not GET, it will throw 405, if the file is not found, it will throw 404.
 */
void handleSPIFFS(HTTPRequest *req, HTTPResponse *res)
{

  // We only handle GET here
  if (req->getMethod() == "GET")
  {
    // Redirect / to /index.html
    std::string reqFile = req->getRequestString() == "/" ? "/index.html" : req->getRequestString();

    // Try to open the file
    // std::string filename = std::string("/") + reqFile;
    std::string filename = reqFile;

    // Check if the file exists
    if (!SPIFFS.exists(filename.c_str()))
    {
      // Send "404 Not Found" as response, as the file doesn't seem to exist
      res->setStatusCode(404);
      res->setStatusText("Not found");
      res->println("404 Not Found");
      return;
    }

    File file = SPIFFS.open(filename.c_str());

    // Set length
    res->setHeader("Content-Length", httpsserver::intToString(file.size()));

    // Content-Type is guessed using the definition of the contentTypes-table defined above
    int cTypeIdx = 0;
    do
    {
      if (reqFile.rfind(contentTypes[cTypeIdx][0]) != std::string::npos)
      {
        res->setHeader("Content-Type", contentTypes[cTypeIdx][1]);
        break;
      }
      cTypeIdx += 1;
    } while (strlen(contentTypes[cTypeIdx][0]) > 0);

    // Read the file and write it to the response
    uint8_t buffer[256];
    size_t length = 0;
    do
    {
      length = file.read(buffer, 256);
      res->write(buffer, length);
    } while (length > 0);

    file.close();
  }
  else
  {
    // If there's any body, discard it
    req->discardRequestBody();
    // Send "405 Method not allowed" as response
    res->setStatusCode(405);
    res->setStatusText("Method not allowed");
    res->println("405 Method not allowed");
  }
}

/**
 * This function will return the wifi credentials as JSON object:
 * {"username": "foo", "password": "bar", "ssid": "hello", "identity": "world"}
 */
void handleGetWifi(HTTPRequest *req, HTTPResponse *res)
{
  // Set the content type of the response
  res->setHeader("Content-Type", "application/json");
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
    serializeJsonPretty(root, *res);
  }
  catch (const std::exception &e)
  {
    res->setStatusCode(400);
    StaticJsonDocument<32> root;
    root["error"] = e.what();
    String error;
    serializeJsonPretty(root, *res);
  }
}

char *c_strToCharArray(std::string str)
{
  char charArray[str.length() + 1];

  for (int i = 0; i < str.length(); i++)
  {
    charArray[i] = str[i];
  }

  return charArray;
}

void handlePostWifi(HTTPRequest *req, HTTPResponse *res)
{
  // Set the content type of the response
  res->setHeader("Content-Type", "application/json");
  ResourceParameters *params = req->getParams();
  std::string ssid, password, username, identity;

  params->getQueryParameter("ssid", ssid);
  params->getQueryParameter("password", password);
  params->getQueryParameter("username", username);
  params->getQueryParameter("identity", identity);
  try
  {
    writeWifiEEPROM(c_strToCharArray(ssid), c_strToCharArray(identity), c_strToCharArray(username), c_strToCharArray(password));
    res->print("OK");
  }
  catch (const std::length_error &e)
  {
    res->setStatusCode(400);
    StaticJsonDocument<32> root;
    root["error"] = e.what();
    String error;
    serializeJsonPretty(root, *res);
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

void getTravelTime(xPosition xPos)
{
  HTTPClient http;

  http.begin(tavelTimeUrl);
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
    String travelDurationString = String(travelDurationTraffic / 60) + " mins to home";

    if (xPos == left)
    {
      printToDisplay(travelDurationString.c_str(), 0, display.height() * 60 / 100, display.width() / 2, 30, true);
    }
    else if (xPos == right)
    {
      printToDisplay(travelDurationString.c_str(), display.width() / 2, display.height() * 60 / 100, display.width() / 2, 30, true);
    }
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void getStocks(String ticker, xPosition xPos)
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

  GET = true;
  apmode = EEPROM.read(AP_SET);
  setupWifi(hostname, ENTERPRISE_MODE);

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
  server->loop();

  if (RESET)
  {
    RESET = !RESET;
    EEPROM.commit();
    ESP.restart();
  }

  if (apmode == 0)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      String ip = WiFi.localIP().toString();
      String ipString = "IP: " + ip;
      printToDisplay(ipString.c_str(), 0, 0, display.width() / 2, 10, true, &Roboto_Regular4pt7b);
      if (GET)
      {
        getQuote();
        getStocks("ET.TO", left);
        getStocks("BTC-CAD", right);
        getTravelTime(right);
        GET = false;
      }
    }
    else
    {
      printToDisplay("No WiFi", 0, 0, display.width() / 2, 10, true, &Roboto_Regular4pt7b);
    }
  }
  else
  {
    String statusString = "Hostname: " + hostname + "    IP: 192.168.4.1";
    printToDisplay(statusString.c_str(), 0, 0, display.width(), 10, true, &Roboto_Regular4pt7b);
  }
};
