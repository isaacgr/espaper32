#include <eeprom/eeprom.h>
#include <EEPROM.h>
#include <defines/defines.h>
#include <string>
#include <stdexcept>

int writeString(uint8_t addr, const char data[])
{
  int _size = strlen(data);
  for (int i = 0; i < _size; i++)
  {
    EEPROM.write(addr + i, data[i]);
  }
  EEPROM.write(addr + _size, '\0'); // Add termination null character for String Data
  EEPROM.commit();
  return _size + 1;
}

void writeWifiEEPROM(char ssid[], char identity[], char username[], char password[])
{
  int index = 100;
  if (strlen(ssid) > 50 || strlen(password) > 50 || strlen(username) > 50 || strlen(identity) > 50)
  {
    throw std::length_error("Cannot exceed 50 characters");
  }
  EEPROM.write(SSID_INDEX, index);
  index += writeString(index, ssid);
  EEPROM.write(PASSWORD_INDEX, index);
  index += writeString(index, password);
  EEPROM.write(USERNAME_INDEX, index);
  index += writeString(index, username);
  EEPROM.write(IDENTITY_INDEX, index);
  index += writeString(index, identity);
  EEPROM.write(WIFI_SET, 1);
  EEPROM.commit();
}