// Implementacion pura del parser NMEA (gps/nmea.h). Sin Serial: puros bytes.
#include "gps/nmea.h"
#include <stdlib.h>
#include <string.h>

bool nmeaCheckSum(const char *line) {
  if (line[0] == '$') line++;
  const char *star = strchr(line, '*');
  if (!star) return false;
  uint8_t x = 0;
  for (const char *p = line; p < star; p++) x ^= (uint8_t)*p;
  char hex[3] = {star[1], star[2], 0};
  if (!hex[1]) return false;
  return x == (uint8_t)strtol(hex, nullptr, 16);
}

bool nmeaParseCoord(const char *f, char hemi, double *out) {
  const char *dot = strchr(f, '.');
  if (!dot || dot - f < 4) return false;  // al menos ddmm
  if (hemi != 'N' && hemi != 'S' && hemi != 'E' && hemi != 'W' &&
      hemi != 'A' && hemi != 'M') return false;
  // grados = digitos antes de los minutos (2 para lat, 3 para lon: igual rito,
  // los minutos son SIEMPRE los ultimos 2 enteros + decimales)
  size_t intLen = (size_t)(dot - f);
  double deg = strtod(f, nullptr);           // ddmm.mmmm
  double mins = deg - (double)(long)(deg / 100.0) * 100.0;
  double whole = (double)(long)(deg / 100.0);
  double v = whole + mins / 60.0;
  if (hemi == 'S' || hemi == 'W') v = -v;
  if (v < -180.0 || v > 180.0) return false;
  *out = v;
  (void)intLen;
  return true;
}

void NmeaParser::reset() {
  m_fix = FixState();
  m_n = 0;
  m_rmcOk = false;
  m_ggaSeen = false;
  m_rlat = m_rlon = m_glat = m_glon = 0.0;
}

bool NmeaParser::feedLine(const char *line, uint32_t nowMs) {
  if (!line || line[0] == '\0') return false;
  if (!nmeaCheckSum(line)) return false;  // frase ruidosa: ignorar entera

  // separar campos sobre una copia editable (frases <=127)
  char work[128];
  size_t len = strlen(line);
  if (len >= sizeof work) return false;
  strcpy(work, line);
  if (work[0] == '$') work[0] = ' ';
  char *fields[24];
  int nf = 0;
  fields[nf++] = work;
  for (char *p = work; *p && nf < 24; p++)
    if (*p == ',') { *p = '\0'; fields[nf++] = p + 1; }

  // id: [sp]XXRMC / XXGGA  (talker GP/GN/GL/BD/GP combinados)
  const char *id = fields[0];
  bool isRmc = strlen(id) >= 5 && strcmp(id + strlen(id) - 3, "RMC") == 0;
  bool isGga = strlen(id) >= 5 && strcmp(id + strlen(id) - 3, "GGA") == 0;
  if (!isRmc && !isGga) return false;

  if (isRmc) {
    if (nf < 8) return false;
    m_rmcOk = (fields[2][0] == 'A');  // [0] id, [1] hora, [2] estado
    if (!m_rmcOk) { m_fix.hasFix = false; m_fix.speedMs = 0.0f; }  // RMC 'V': perdimos el fix
    if (m_rmcOk) {
      double la, lo;
      if (!nmeaParseCoord(fields[3], fields[4][0], &la)) { m_rmcOk = false; return false; }
      if (!nmeaParseCoord(fields[5], fields[6][0], &lo)) { m_rmcOk = false; return false; }
      m_rlat = la; m_rlon = lo;
      m_fix.speedMs = (float)(strtod(fields[7], nullptr) * 0.514444);  // nudos
      // si GGA ya paso (o no se emite nunca), RMC 'A' fija la posicion ya
      if (!m_ggaSeen || m_fix.quality >= 1) {
        m_fix.hasFix = true;
        m_fix.latDeg = m_rlat; m_fix.lonDeg = m_rlon;
        m_fix.lastFixMs = nowMs;
      }
    }
    return true;
  }

  // GGA
  if (nf < 9) return false;
  m_ggaSeen = true;
  uint8_t qual = (uint8_t)atoi(fields[6]);
  m_fix.quality = qual;
  m_fix.sats = (uint8_t)atoi(fields[7]);
  double hd = strtod(fields[8], nullptr);
  int h10 = (int)(hd * 10.0 + 0.5);
  m_fix.hdopX10 = (uint8_t)(h10 > 255 ? 255 : h10);
  double la, lo;
  if (nmeaParseCoord(fields[2], fields[3][0], &la) &&
      nmeaParseCoord(fields[4], fields[5][0], &lo)) { m_glat = la; m_glon = lo; }

  // fijacion final: RMC 'A' y calidad GGA>=1; posicion manda RMC, GGA salva
  if (m_rmcOk && qual >= 1) {
    m_fix.hasFix = true;
    m_fix.latDeg = m_rlat ? m_rlat : m_glat;
    m_fix.lonDeg = m_rlon ? m_rlon : m_glon;
    m_fix.lastFixMs = nowMs;
  } else if (qual == 0) {
    m_fix.hasFix = false;
    m_fix.speedMs = 0.0f;
  }
  return true;
}

bool NmeaParser::feedByte(uint8_t b, uint32_t nowMs) {
  if (b == '\n') {
    m_buf[m_n] = '\0';
    m_n = 0;
    if (m_buf[0] == '\r') memmove(m_buf, m_buf + 1, strlen(m_buf));  // quita \r
    return feedLine(m_buf, nowMs);
  }
  if (b == '$') { m_n = 0; m_buf[m_n++] = (char)b; return false; }
  if (m_n < sizeof m_buf - 1) m_buf[m_n++] = (char)b;
  else m_n = 0;  // linea larga: descartar (resincroniza en el proximo '$')
  return false;
}
