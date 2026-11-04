// Glue Wi-Fi/HTTP del vinculo de pasos (F26). Solo ESP32 y solo si la
// consola lo enciende; la validacion del contrato es la misma pura de
// step_link.cpp.
#if defined(ESP32)
#include "net/wifi_steps.h"

#include <WebServer.h>
#include <WiFi.h>

#include "net/step_link.h"

static WebServer *s_srv = nullptr;
static void (*s_apply)(int32_t) = nullptr;
static uint32_t (*s_total)() = nullptr;

void wifiStepsStart(const char *ssid, const char *pass,
                    void (*apply)(int32_t delta), uint32_t (*total)()) {
  s_apply = apply;
  s_total = total;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  if (!s_srv) s_srv = new WebServer(80);
  s_srv->on("/steps", HTTP_POST, []() {
    String b = s_srv->arg("plain");
    int32_t d = 0;
    int ack = slParseHttpBody(b.c_str(), b.length(), &d) ? slCheckDelta(d)
                                                         : SL_BAD_FORMAT;
    if (ack == SL_OK) s_apply(d);
    char msg[24];
    snprintf(msg, sizeof msg, "%s %u", ack == SL_OK ? "OK" : "REJECTED",
             s_total());
    s_srv->send(ack == SL_OK ? 200 : 400, "text/plain", msg);
  });
  s_srv->on("/steps", HTTP_GET, []() {
    s_srv->send(200, "text/plain", String(s_total()));
  });
  s_srv->begin();
}

void wifiStepsLoop() {
  if (s_srv) s_srv->handleClient();
}

bool wifiStepsOn() { return s_srv != nullptr; }

#endif  // ESP32
