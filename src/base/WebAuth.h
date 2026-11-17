#ifndef WebAuth_h
#define WebAuth_h

#include "Application.h"
#include <vector>

#ifdef ESP8266
using WebRequestHandler = ESP8266WebServer::RequestHandlerType;
#else
using WebRequestHandler = RequestHandler;
#endif

// HTTP Digest authentication (user "admin") for every request of the web server.
// Stateless: any nonce is accepted (no replay protection), the password just never travels over the network.
// Registered as the first handler, it takes every request (its headers aren't parsed yet at that time),
// then checks the credentials and hands the request over to the handler that would have taken it.
class WebAuth : public WebRequestHandler
{
private:
  static std::vector<const __FlashStringHelper *> _anonymousUris;

  WebServer &_server;
  const char *_password; // empty: authentication disabled
  WebRequestHandler *_target = nullptr;
  bool _checked = false;
  bool _authorized = false;

  void check();
  bool isAnonymous();
  bool authenticate();

public:
  WebAuth(WebServer &server, const char *password) : _server(server), _password(password) {}

  // uri reachable without authentication (any method), e.g. API for third-party clients
  static void allowAnonymous(const __FlashStringHelper *uri) { _anonymousUris.push_back(uri); }

#ifdef ESP8266
  bool canHandle(HTTPMethod method, const String &uri) override;
  bool canUpload(const String &uri) override;
#else
  bool canHandle(WebServer &server, HTTPMethod method, const String &uri) override;
  bool canUpload(WebServer &server, const String &uri) override;
#endif
  bool handle(WebServer &server, HTTPMethod requestMethod, const String &requestUri) override;
  void upload(WebServer &server, const String &requestUri, HTTPUpload &upload) override;
};

#endif
