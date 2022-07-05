#ifndef HTTPS_H
#define HTTPS_H

#include <HTTPSServer.hpp>
#include <SSLCert.hpp>
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <util.hpp>

using namespace httpsserver;
void handleSPIFFS(HTTPRequest *req, HTTPResponse *res);
void handleGetWifi(HTTPRequest *req, HTTPResponse *res);
void handlePostWifi(HTTPRequest *req, HTTPResponse *res);
SSLCert *getCertificate();
HTTPSServer *server;

#endif