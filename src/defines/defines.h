#ifndef DEFINES_H
#define DEFINES_H

#include <Arduino.h>

/*****************
DEFINITIONS
******************/
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

/*****************
GLOBALS
******************/
uint8_t g_Power = 1;
uint8_t apmode = 0;
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

enum xPosition
{
  left,
  right
};

const char *quotesUrl = "https://api.quotable.io/random?tags=technology|success|business|inspirational|education|future|science|famous-quotes|life|literature|wisdom&maxLength=45";
const char *tavelTimeUrl = "https://dev.virtualearth.net/REST/V1/Routes/Driving?o=json&wp.0=43.39252853393555,-79.77173614501953&wp.1=43.251670837402344,-79.88003540039062&avoid=minimizeTolls&key=AnIS3IEKS30ivDfBr0AWq36z04STmWOiwPsGbECvRwh7kxHEEHqYlEiRVsMMvmvM&routeAttributes=routeSummariesOnly";
String stockUrl = "https://query1.finance.yahoo.com/v8/finance/chart/";
String token = "?interval=1d";

#endif