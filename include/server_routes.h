void setupWeb()
{
  server.on(
      "/wifi", HTTP_POST, []()
      {
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    int ssidLen = ssid.length() + 1;
    int passLen = password.length() + 1;
    char ssidArr[ssidLen];
    char passArr[passLen];

    ssid.toCharArray(ssidArr, ssidLen);
    password.toCharArray(passArr, passLen);

    try
    {
      writeWifiEEPROM(ssidArr, passArr);
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

  server.on("/deviceName", HTTP_POST, []()
            {
    String name = server.arg("deviceName");
    int nameLen = name.length() + 1;
    char nameArr[nameLen];

    name.toCharArray(nameArr, nameLen);

    try
    {
      writeDeviceNameEEPROM(nameArr);
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

  server.onNotFound([]()
                    {
    if (!handleFileRead(server.uri()))
    {
      server.send(404, "text/plain", "Not Found");
    } });
}