void setupWeb()
{
  server.on(
      "/wifi", HTTP_POST, []()
      {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    id = ssid.c_str();
    pass = password.c_str();
    try
    {
      writeWifiEEPROM(id, pass);
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
    try
    {
      StaticJsonDocument<256> root;
      root["password"] = pass;
      root["ssid"] = id;
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