#include "https.h"
#include <SPIFFS.h>
#include <defines/defines.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <eeprom/eeprom.h>

using namespace httpsserver;

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

  // Try to open key and cert file to see if they exist
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