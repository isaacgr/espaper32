#include <EEPROM.h>
#include <WiFi.h>
#include "esp_wpa2.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "wifi_utils.h"
#include "defines.h"
#include "secrets.h"

char mdns_name[] = "isaac-epaper";

char *toCharArray(String str)
{
  char charArray[str.length() + 1];

  for (int i = 0; i < str.length(); i++)
  {
    charArray[i] = str[i];
  }

  return charArray;
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  Serial.println(info.disconnected.reason);
  Serial.println("Trying to Reconnect");
  WiFi.begin(ssid, password);
}

void setupWifi(String hostname, int apMode, int enterpriseModePin)
{
  // Set Hostname.
  uint64_t chipid = ESP.getEfuseMac();
  uint16_t long1 = (unsigned long)((chipid & 0xFFFF0000) >> 16);
  uint16_t long2 = (unsigned long)((chipid & 0x0000FFFF));
  String hex = String(long1, HEX) + String(long2, HEX); // six octets
  hostname += hex;

  char hostnameChar[hostname.length() + 1];
  memset(hostnameChar, 0, hostname.length() + 1);

  for (uint8_t i = 0; i < hostname.length(); i++)
    hostnameChar[i] = hostname.charAt(i);

  WiFi.setHostname(hostnameChar);

  if (apMode != 0)
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(hostnameChar, WiFiAPPSK);
    Serial.printf("Connect to Wi-Fi access point: %s\n", hostnameChar);
    Serial.println("and open http://192.168.4.1 in your browser");
  }
  else
  {
    WiFi.onEvent(WiFiStationConnected, SYSTEM_EVENT_STA_CONNECTED);
    WiFi.onEvent(WiFiGotIP, SYSTEM_EVENT_STA_GOT_IP);
    WiFi.onEvent(WiFiStationDisconnected, SYSTEM_EVENT_STA_DISCONNECTED);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    Serial.print("MAC >> ");
    Serial.println(WiFi.macAddress());
    Serial.println("Wait for WiFi... ");
    if (EEPROM.read(WIFI_SET) == 1)
    {
      int idIndex = EEPROM.read(SSID_INDEX);
      strcpy(ssid, toCharArray(EEPROM.readString(idIndex)));
      int passwordIndex = EEPROM.read(PASSWORD_INDEX);
      strcpy(password, toCharArray(EEPROM.readString(passwordIndex)));
      int usernameIndex = EEPROM.read(USERNAME_INDEX);
      strcpy(username, toCharArray(EEPROM.readString(usernameIndex)));
      int identityIndex = EEPROM.read(IDENTITY_INDEX);
      strcpy(identity, toCharArray(EEPROM.readString(identityIndex)));
    }
    if (digitalRead(enterpriseModePin))
    {
      Serial.println("Enterprise wifi mode");
      esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT();
      // This part of the code is taken from the oficial wpa2_enterprise example from esp-idf
      ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
      ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_ca_cert((uint8_t *)incommon_ca, strlen(incommon_ca) + 1));
      ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)identity, strlen(identity)));
      ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_username((uint8_t *)username, strlen(username)));
      ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_password((uint8_t *)password, strlen(password)));
      ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_enable(&config));
      WiFi.begin(ssid);
    }
    else
    {
      WiFi.begin(ssid, password);
    }
  }
}