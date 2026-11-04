// Implementacion del glue GPS (gps/esp_gps.h). Solo se enlaza en placas con
// GNSS; en Guition las funciones existen pero no hacen nada (degradado).
#include "gps/esp_gps.h"
#include "pin_config.h"  // HAS_GPS segun placa (board/board.h)
#include <Arduino.h>

#if HAS_GPS
#include <HardwareSerial.h>

static NmeaParser s_parser;
static uint32_t s_lastPumpMs = 0;
static float s_travelM = 0.0f;
static bool s_hadFix = false;
static float s_lastSpeed = 0.0f;

void gpsStart() {
  Serial0.begin(9600, SERIAL_8N1, 43, 44);  // RX=43 TX=44: pines por defecto
}

void gpsPump(uint32_t nowMs) {
  while (Serial0.available() > 0) {
    int b = Serial0.read();
    if (b < 0) break;
    s_parser.feedByte((uint8_t)b, nowMs);
  }
  // integra distancia: velocidad x dt solo mientras hubo fijacion (capa 3)
  uint32_t dt = nowMs - s_lastPumpMs;
  s_lastPumpMs = nowMs;
  const FixState &f = s_parser.fix();
  if (f.hasFix && (s_hadFix || f.speedMs > 0.0f)) {
    s_travelM += f.speedMs * ((float)dt / 1000.0f);
    s_lastSpeed = f.speedMs;
  }
  s_hadFix = f.hasFix;
}

const FixState &gpsFix() { return s_parser.fix(); }
float gpsTravelledM() { return s_travelM; }
void gpsTravelledReset() { s_travelM = 0.0f; }
bool gpsInjectLine(const char *line, uint32_t nowMs) {
  return s_parser.feedLine(line, nowMs);
}

#else  // Guition: sin receptor — API identica, todo "no fix"

void gpsStart() {}
void gpsPump(uint32_t) {}
static FixState s_noFix;
const FixState &gpsFix() { return s_noFix; }
float gpsTravelledM() { return 0.0f; }
void gpsTravelledReset() {}
bool gpsInjectLine(const char *line, uint32_t nowMs) {
  static NmeaParser p;  // util para probar el parser por consola igualmente
  return p.feedLine(line, nowMs);
}

#endif
