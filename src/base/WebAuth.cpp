#include "WebAuth.h"
#include <MD5Builder.h>

std::vector<const __FlashStringHelper *> WebAuth::_anonymousUris;

static const char WEB_AUTH_USER[] = "admin";
static const char WEB_AUTH_REALM[] = CUSTOM_APP_MODEL;

static String md5(const String &s)
{
  MD5Builder builder;
  builder.begin();
  builder.add(s);
  builder.calculate();
  return builder.toString();
}

// Value of a Digest parameter (quoted or not), empty if missing
static String digestParam(const String &header, const char *name)
{
  size_t nameLen = strlen(name);
  int pos = 0;
  while ((pos = header.indexOf(name, pos)) >= 0)
  {
    // must be a whole parameter name ("nonce" is also the end of "cnonce")
    if (pos > 0 && (header[pos - 1] == ' ' || header[pos - 1] == ',') && header[pos + nameLen] == '=')
    {
      int start = pos + nameLen + 1;
      if (header[start] == '"')
        return header.substring(start + 1, header.indexOf('"', start + 1));
      int end = header.indexOf(',', start);
      String value = header.substring(start, end < 0 ? header.length() : end);
      value.trim();
      return value;
    }
    pos += nameLen;
  }
  return String();
}

static const __FlashStringHelper *methodName(HTTPMethod method)
{
  switch (method)
  {
  case HTTP_HEAD:
    return F("HEAD");
  case HTTP_POST:
    return F("POST");
  case HTTP_PUT:
    return F("PUT");
  case HTTP_PATCH:
    return F("PATCH");
  case HTTP_DELETE:
    return F("DELETE");
  case HTTP_OPTIONS:
    return F("OPTIONS");
  default:
    return F("GET");
  }
}

bool WebAuth::authenticate()
{
  String header = _server.header(F("Authorization"));
  if (!header.startsWith(F("Digest ")))
    return false;

  // the hash covers the uri given by the browser: it must be the requested one
  String uri = digestParam(header, "uri");
  if (uri.substring(0, uri.indexOf('?')) != _server.uri())
    return false;

  // user and realm are part of ha1: a wrong one gives a wrong response
  String ha1 = md5(String(WEB_AUTH_USER) + ':' + WEB_AUTH_REALM + ':' + _password);
  String ha2 = md5(String(methodName(_server.method())) + ':' + uri);
  String expected = md5(ha1 + ':' + digestParam(header, "nonce") + ':' + digestParam(header, "nc") + ':' + digestParam(header, "cnonce") + F(":auth:") + ha2);

  return expected == digestParam(header, "response");
}

// called once headers are parsed
void WebAuth::check()
{
  if (_checked)
    return;
  _checked = true;
  _authorized = !_password[0] || SystemState::recoveryMode || isAnonymous() || authenticate();
}

bool WebAuth::isAnonymous()
{
  for (const __FlashStringHelper *uri : _anonymousUris)
    if (!strcmp_P(_server.uri().c_str(), (PGM_P)uri))
      return true;
  return false;
}

#ifdef ESP8266
bool WebAuth::canHandle(HTTPMethod method, const String &uri)
#else
bool WebAuth::canHandle(WebServer &server, HTTPMethod method, const String &uri)
#endif
{
  _checked = false;
  for (_target = next(); _target; _target = _target->next())
#ifdef ESP8266
    if (_target->canHandle(method, uri))
#else
    if (_target->canHandle(server, method, uri))
#endif
      break;
  return true;
}

#ifdef ESP8266
bool WebAuth::canUpload(const String &uri)
{
  check();
  return !_authorized || (_target && _target->canUpload(uri));
}
#else
bool WebAuth::canUpload(WebServer &server, const String &uri)
{
  check();
  return !_authorized || (_target && _target->canUpload(server, uri));
}
#endif

void WebAuth::upload(WebServer &server, const String &requestUri, HTTPUpload &upload)
{
  // uploaded data is dropped until authenticated
  if (_authorized && _target)
    _target->upload(server, requestUri, upload);
}

bool WebAuth::handle(WebServer &server, HTTPMethod requestMethod, const String &requestUri)
{
  check();
  _checked = false;

  if (!_authorized)
  {
    // any nonce is accepted back, so it doesn't need to be random
    char authenticate[80];
    snprintf_P(authenticate, sizeof(authenticate), PSTR("Digest realm=\"%s\", qop=\"auth\", nonce=\"%08lx\""), WEB_AUTH_REALM, (unsigned long)micros());
    server.sendHeader(F("WWW-Authenticate"), authenticate);
    server.send(401, F("text/plain"), F("Authentication required"));
    return true;
  }

  if (!_target)
    return false; // not found handler

#ifdef ESP8266
  return _target->handle(server, requestMethod, requestUri);
#else
  return _target->process(server, requestMethod, requestUri);
#endif
}
