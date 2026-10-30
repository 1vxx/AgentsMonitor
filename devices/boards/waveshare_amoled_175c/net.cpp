// GET /v1/summary rather than the WebSocket: the socket opens with a snapshot
// of every session, far bigger than the summary, and a second's polling is
// plenty for a display. Three failures in a row count as the link lost. Turns
// the person saw on the display go back as POSTs, just before the next poll.

#include "net.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "secrets.h"

namespace net {
namespace {

constexpr int POLL_MS = 1000, TIMEOUT_MS = 2500, LOST_AFTER = 3, MAX_BODY = 32 * 1024;

volatile State current = CONNECTING;
SemaphoreHandle_t lock;
char *latest;
bool fresh;

// Turns seen on the display, waiting to be told
struct Seen {
  char key[96];
  int64_t endedAt;
};
Seen seens[4];
int seenHead, seenTail;

// A session key as one path segment: everything but letters, digits and
// -._~: escaped
void pathOf(const char *key, char *out, size_t len) {
  int n = snprintf(out, len, "http://%s:%d/v1/sessions/", DAEMON_HOST, DAEMON_PORT);
  for (const char *c = key; *c && n + 4 < int(len); c++) {
    if (isalnum((unsigned char)*c) || strchr("-._~:", *c))
      out[n++] = *c;
    else
      n += snprintf(out + n, len - n, "%%%02X", (unsigned char)*c);
  }
  snprintf(out + n, len - n, "/seen");
}

void tellSeen(HTTPClient &http, const char *auth) {
  for (;;) {
    xSemaphoreTake(lock, portMAX_DELAY);
    bool any = seenHead != seenTail;
    Seen s = seens[seenHead];
    if (any) seenHead = (seenHead + 1) % 4;
    xSemaphoreGive(lock);
    if (!any) return;
    char url[400], body[48];
    pathOf(s.key, url, sizeof url);
    snprintf(body, sizeof body, "{\"ended_at\":%lld}", (long long)s.endedAt);
    if (!http.begin(url)) continue;
    if (DAEMON_TOKEN[0]) http.addHeader("Authorization", auth);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code != 204) Serial.printf("net: POST %s: %d\n", url, code);
    http.end();
  }
}

void task(void *) {
  char url[160];
  snprintf(url, sizeof url, "http://%s:%d/v1/summary", DAEMON_HOST, DAEMON_PORT);
  char auth[160];
  snprintf(auth, sizeof auth, "Bearer %s", DAEMON_TOKEN);
  int failures = 0;
  bool everOnline = false;
  HTTPClient http;
  for (;;) {
    uint32_t start = millis();
    if (WiFi.status() != WL_CONNECTED) {
      current = everOnline ? LOST : CONNECTING;
      delay(500);
      continue;
    }
    http.setTimeout(TIMEOUT_MS);
    http.setReuse(true);
    tellSeen(http, auth);
    bool ok = false;
    if (http.begin(url)) {
      if (DAEMON_TOKEN[0]) http.addHeader("Authorization", auth);
      int code = http.GET();
      if (code == 200) {
        String body = http.getString();
        if (body.length() && body.length() < MAX_BODY) {
          xSemaphoreTake(lock, portMAX_DELAY);
          memcpy(latest, body.c_str(), body.length() + 1);
          fresh = true;
          xSemaphoreGive(lock);
          ok = true;
        }
      } else {
        Serial.printf("net: GET %s: %d\n", url, code);
      }
      http.end();
    }
    if (ok) {
      failures = 0;
      everOnline = true;
      current = ONLINE;
    } else if (++failures >= LOST_AFTER) {
      current = everOnline ? LOST : CONNECTING;
    }
    uint32_t spent = millis() - start;
    if (spent < POLL_MS) delay(POLL_MS - spent);
  }
}

}  // namespace

void begin() {
  latest = (char *)ps_malloc(MAX_BODY);
  lock = xSemaphoreCreateMutex();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  xTaskCreatePinnedToCore(task, "net", 8192, nullptr, 1, nullptr, 0);
}

State state() { return current; }

bool take(char *out, size_t len) {
  if (!fresh) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  strncpy(out, latest, len - 1);
  out[len - 1] = 0;
  fresh = false;
  xSemaphoreGive(lock);
  return true;
}

void seen(const char *key, int64_t endedAt) {
  xSemaphoreTake(lock, portMAX_DELAY);
  int next = (seenTail + 1) % 4;
  if (next != seenHead) {
    strncpy(seens[seenTail].key, key, sizeof seens[seenTail].key - 1);
    seens[seenTail].key[sizeof seens[seenTail].key - 1] = 0;
    seens[seenTail].endedAt = endedAt;
    seenTail = next;
  }
  xSemaphoreGive(lock);
}

}  // namespace net
