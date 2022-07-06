#ifndef DEFINES_H
#define DEFINES_H

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

enum xPosition
{
  left,
  right,
  top,
  bottom
};

#endif