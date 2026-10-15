#include "nightscout.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "timekeeping.h"

namespace {

// "cgm.example.com", "https://cgm.example.com/" -> "https://cgm.example.com"
String baseUrl(const char* configured) {
  String url(configured);
  url.trim();
  if (url.indexOf("://") < 0) url = "https://" + url;
  while (url.endsWith("/")) url.remove(url.length() - 1);
  return url;
}

}  // namespace

bool nightscoutFetch(const AppConfig& cfg, NsReading& out, char* status, size_t statusLen) {
  if (cfg.nightscout[0] == 0) {
    strlcpy(status, "no nightscout URL in INI", statusLen);
    return false;
  }

  String url = baseUrl(cfg.nightscout) + "/api/v1/entries/sgv.json?count=2";
  if (cfg.token[0]) url += String("&token=") + cfg.token;

  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = url.startsWith("https://");
  if (https) secure.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(https ? static_cast<WiFiClient&>(secure) : plain, url)) {
    strlcpy(status, "bad URL", statusLen);
    return false;
  }
  http.addHeader("Accept", "application/json");

  const char* headers[] = {"Date"};
  http.collectHeaders(headers, 1);
  int code = http.GET();
  if (code > 0) timeSeedFromHttpDate(http.header("Date").c_str());
  if (code != HTTP_CODE_OK) {
    if (code < 0)
      snprintf(status, statusLen, "error: %s", HTTPClient::errorToString(code).c_str());
    else
      snprintf(status, statusLen, "HTTP %d", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument filter;
  filter[0]["sgv"] = true;
  filter[0]["direction"] = true;
  filter[0]["date"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    snprintf(status, statusLen, "JSON: %s", err.c_str());
    return false;
  }
  JsonArray entries = doc.as<JsonArray>();
  if (entries.size() == 0 || entries[0]["sgv"].isNull()) {
    strlcpy(status, "no entries", statusLen);
    return false;
  }

  // Nightscout returns newest first, but don't rely on it.
  int newest = 0;
  if (entries.size() > 1 && entries[1]["date"].as<int64_t>() > entries[0]["date"].as<int64_t>())
    newest = 1;
  JsonObject a = entries[newest];

  NsReading r;
  r.valid = true;
  r.sgv = lroundf(a["sgv"].as<float>());
  r.dateMs = a["date"].as<int64_t>();
  strlcpy(r.direction, a["direction"] | "", sizeof(r.direction));
  if (entries.size() > 1 && !entries[1 - newest]["sgv"].isNull()) {
    r.hasPrev = true;
    r.prevSgv = lroundf(entries[1 - newest]["sgv"].as<float>());
  }
  out = r;
  snprintf(status, statusLen, "OK %d mg/dL %s", r.sgv, r.direction);
  return true;
}
