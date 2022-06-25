#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>

#define MAX_DISPLAY_BUFFER_SIZE 131072ul // e.g. half of available ram
#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

#define POWER_BUTTON 4
#define EEPROM_SIZE 12

// select the display class and display driver class in the following file (new style):
// #include "GxEPD2_display_selection_new_style.h"

// alternately you can copy the constructor from GxEPD2_display_selection.h or GxEPD2_display_selection_added.h to here
// e.g. for Wemos D1 mini:
// GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(/*CS=D8*/ SS, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4)); // GDEH0154D67
GxEPD2_BW<GxEPD2_270, MAX_HEIGHT(GxEPD2_270)> display(GxEPD2_270(/*CS=*/SS, /*DC=*/17, /*RST=*/16, /*BUSY=*/4)); // GDEW027W3

WebServer server(80);
uint8_t g_Power = 1;
uint8_t apmode = 0;
bool writeFields = false;
bool RESET = false;

// EEPROM addresses for state
const uint8_t SSID_INDEX = 1;
const uint8_t PASS_INDEX = 2;
const uint8_t WIFI_SET = 3;
const uint8_t MDNS_INDEX = 4;
const uint8_t MDNS_SET = 5;
const uint8_t AP_SET = 6;

#include <secret.h>
#include <eeprom_utils.h>
#include <wifi_utils.h>
#include <file_manager.h>
#include <server_routes.h>

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
          EEPROM.write(i, 255);
        }
        EEPROM.write(AP_SET, 0);
        RESET = true;
      }
    }
    last_interrupt_time = interrupt_time;
  }
}

// void displaySimpleText(const char *text)
// {
//   display.setRotation(1);
//   display.setFont(&FreeMonoBold9pt7b);
//   display.setTextColor(GxEPD_BLACK);
//   int16_t tbx, tby;
//   uint16_t tbw, tbh;
//   display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
//   // center the bounding box by transposition of the origin:
//   uint16_t x = ((display.width() - tbw) / 2) - tbx;
//   uint16_t y = ((display.height() - tbh) / 2) - tby;
//   display.setFullWindow();
//   display.firstPage();
//   do
//   {
//     display.fillScreen(GxEPD_WHITE);
//     display.setCursor(x, y);
//     display.print(text);
//   } while (display.nextPage());
// }

// const char HelloWorld[] = "Test";

void displaySimpleText(const char *text)
{

  static int16_t bbx = 400;
  static int16_t bby = 300;
  static uint16_t bbw = 0;
  static uint16_t bbh = 0;
  display.setRotation(1);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  // place the bounding box
  int16_t tx = max(0, ((display.width() - tbw) / 2));
  int16_t ty = max(0, (display.height() * 3 / 4 - tbh / 2));
  bbx = min(bbx, tx);
  bby = min(bby, ty);
  bbw = max(bbw, tbw);
  bbh = max(bbh, tbh);
  // calculate the cursor
  uint16_t x = bbx - tbx;
  uint16_t y = bby - tby;
  display.setPartialWindow(bbx, bby, bbw, bbh);

  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print(text);
  } while (display.nextPage());
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
  // apmode = EEPROM.read(AP_SET);
  setupWifi();
  // SPIFFS.format(); // Prevents SPIFFS_ERR_NOT_A_FS
  SPIFFS.begin(); // Start the SPI Flash Files System
  server.begin();
  server.enableCORS();
  setupWeb();
  display.init(115200);
  delay(1000);
  Serial.println("setup done");
}

void loop()
{
  server.handleClient();
  if (apmode == 0)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      const char *ip = WiFi.localIP().toString().c_str();
      displaySimpleText(ip);
    }
    else
    {
      displaySimpleText("No wifi");
    }
  }
  else
  {
    displaySimpleText("AP Mode");
  }
};
