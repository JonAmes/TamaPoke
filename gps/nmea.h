// Nucleo GPS puro (B2 / plan.md §5b): parser NMEA linea-a-linea -> FixState.
// Sin Serial ni hardware: el glue (esp_gps.cpp) alimenta bytes y el host
// alimenta lineas enlatadas en los tests. NMEA 0183: GNRMC + GNGGA.
#pragma once
#include <stdint.h>
#include <stddef.h>

// Estado de fijacion al estilo del plan: has_fix + posicion + velocidad +
// calidad. lat/lon en grados con signo (N+/E-). velocidad en m/s.
struct FixState {
  bool hasFix = false;
  double latDeg = 0.0;
  double lonDeg = 0.0;
  float speedMs = 0.0f;   // sobre suelo
  uint8_t quality = 0;    // GGA 0..2: sin fix / fix / differential
  uint8_t sats = 0;
  uint8_t hdopX10 = 0;    // HDOP x10 (0.9 -> 9), cap 255
  uint32_t lastFixMs = 0; // reloj monotono inyectado por el caller
};

// Utilidades expuestas para tests:
// "4123.456" + 'N' -> 41.3909...; hemisferio S/W = negativo. false si mala.
bool nmeaParseCoord(const char *field, char hemi, double *out);
// valida el checksum *HH (XOR entre '$' y '*'); sin '*' se rechaza.
bool nmeaCheckSum(const char *line);

// Parser de estado. Una linea = una frase completa SIN \r ni \n.
class NmeaParser {
 public:
  // devuelve true si la frase era reconocida Y de checksum valido
  bool feedLine(const char *line, uint32_t nowMs);
  // acumula bytes y procesa en '\n'; devuelve true si proceso una frase
  bool feedByte(uint8_t b, uint32_t nowMs);
  const FixState &fix() const { return m_fix; }
  void reset();

 private:
  FixState m_fix;
  char m_buf[128];
  uint16_t m_n = 0;
  bool m_rmcOk = false;  // ultima RMC traia 'A'
  bool m_ggaSeen = false;  // si el receptor emite GGA (exigimos calidad>=1)
  double m_rlat = 0.0, m_rlon = 0.0;  // RMC es la que manda en posicion
  double m_glat = 0.0, m_glon = 0.0;  // respaldo GGA
};
