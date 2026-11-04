// Contrato telefono->reloj de pasos (F25/F26): capa pura, sin plataforma.
#include "framework.h"
#include "../net/step_link.h"

TEST(steplink, delta_int32_little_endian) {
  const uint8_t w[4] = {0x2c, 0x01, 0x00, 0x00};  // 300
  int32_t d = 0;
  CHECK(slParseDelta(w, 4, &d));
  CHECK_EQ(d, (int32_t)300);
}

TEST(steplink, rechaza_longitudes_raras) {
  const uint8_t w[4] = {1, 0, 0, 0};
  int32_t d = 0;
  CHECK_MSG(!slParseDelta(w, 3, &d), "3 bytes no es int32");
  CHECK_MSG(!slParseDelta(w, 5, &d), "5 bytes tampoco");
  CHECK_MSG(!slParseDelta(nullptr, 4, &d), "null no");
}

TEST(steplink, la_politica_acepta_solo_deltas_sanos) {
  CHECK_EQ(slCheckDelta(1), SL_OK);
  CHECK_EQ(slCheckDelta(SL_MAX_DELTA), SL_OK);
  CHECK_EQ(slCheckDelta(0), SL_BAD_DELTA);
  CHECK_EQ(slCheckDelta(-5), SL_BAD_DELTA);  // el movil no manda negativos
  CHECK_EQ(slCheckDelta(SL_MAX_DELTA + 1), SL_BAD_DELTA);
}

TEST(steplink, total_se_satura_a_int32_y_es_le) {
  uint8_t t[4];
  slWriteTotal(0x01020304, t);
  CHECK_EQ(t[0], (uint8_t)0x04);
  CHECK_EQ(t[3], (uint8_t)0x01);
  slWriteTotal(0xffffffff, t);  // vitalicios absurdos: tope 0x7fffffff
  CHECK_EQ(t[0], (uint8_t)0xff);
  CHECK_EQ(t[3], (uint8_t)0x7f);
}

TEST(steplink, ack_lleva_codigo_mas_total) {
  uint8_t a[5];
  slBuildAck(SL_OK, 1000, a);
  CHECK_EQ(a[0], (uint8_t)SL_OK);
  CHECK_EQ(a[1], (uint8_t)0xe8);  // 1000 LE
  CHECK_EQ(a[2], (uint8_t)0x03);
}

TEST(steplink, cuerpo_http_delta_igual_o_json) {
  int32_t d = 0;
  CHECK(slParseHttpBody("delta=123", 9, &d));
  CHECK_EQ(d, (int32_t)123);
  CHECK(slParseHttpBody("{\"delta\": 456}", 14, &d));
  CHECK_EQ(d, (int32_t)456);
  CHECK(slParseHttpBody("delta=-7", 8, &d));
  CHECK_EQ(d, (int32_t)-7);  // parsea; la politica lo rechaza luego
  CHECK_MSG(!slParseHttpBody("pasos=3", 8, &d), "sin clave delta");
  CHECK_MSG(!slParseHttpBody("delta=", 6, &d), "sin numero");
  CHECK_MSG(!slParseHttpBody("delta=99999999999999", 18, &d), "fuera de int32");
  CHECK_EQ(slCheckDelta(-7), SL_BAD_DELTA);
}
