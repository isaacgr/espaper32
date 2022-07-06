#ifndef EEPROM_UTILS_H
#define EEPROM_UTILS_H

#include <Arduino.h>

int writeString(uint8_t addr, const char data[]);
void writeWifiEEPROM(char ssid[], char identity[], char username[], char password[]);

#endif