#include "net/step_link.h"

bool slParseDelta(const uint8_t *data, size_t len, int32_t *delta) {
  if (!data || len != 4) return false;  // int32 LE pelado, nada mas
  uint32_t u = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
               ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
  *delta = (int32_t)u;
  return true;
}

bool slParseHttpBody(const char *body, size_t len, int32_t *delta) {
  if (!body) return false;
  const char *p = nullptr, *end = body + len;
  for (const char *q = body; q + 5 <= end; q++)
    if (q[0] == 'd' && q[1] == 'e' && q[2] == 'l' && q[3] == 't' && q[4] == 'a') { p = q + 5; break; }
  if (!p) return false;
  while (p < end && (*p == '=' || *p == ':' || *p == '"' || *p == ' ')) p++;
  bool neg = false;
  if (p < end && *p == '-') { neg = true; p++; }
  if (p >= end || *p < '0' || *p > '9') return false;
  int64_t v = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    if (v > INT32_MAX) return false;  // fuera de int32: cuerpo malo
    p++;
  }
  *delta = neg ? (int32_t)-v : (int32_t)v;
  return true;
}

int slCheckDelta(int32_t delta) {
  if (delta <= 0 || delta > SL_MAX_DELTA) return SL_BAD_DELTA;
  return SL_OK;
}

void slWriteTotal(uint32_t total, uint8_t out[4]) {
  uint32_t t = total > 0x7fffffffUL ? 0x7fffffffUL : total;  // saturar a int32
  out[0] = (uint8_t)t; out[1] = (uint8_t)(t >> 8);
  out[2] = (uint8_t)(t >> 16); out[3] = (uint8_t)(t >> 24);
}

void slBuildAck(int ack, uint32_t total, uint8_t out[5]) {
  out[0] = (uint8_t)ack;
  slWriteTotal(total, out + 1);
}
