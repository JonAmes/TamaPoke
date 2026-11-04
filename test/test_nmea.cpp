// Tests del parser NMEA puro (gps/nmea.h). Las frases se arman aqui mismo con
// su checksum real: si editamos una fixture no se desincroniza el *XX.
#include "framework.h"
#include <cmath>
#include <cstdio>
#include <string>
#include "../gps/nmea.h"

static std::string mk(const char *body) {  // body sin '$' ni '*XX'
  unsigned char cs = 0;
  for (const char *p = body; *p; p++) cs ^= (unsigned char)*p;
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X", cs);
  return std::string("$") + body + tail;
}

static bool near_(double a, double b, double eps) { return fabs(a - b) < eps; }

TEST(nmea, coordenadas_ddmm_mmmm_y_hemisferios) {
  double v;
  CHECK(nmeaParseCoord("4123.4000", 'N', &v));
  CHECK(near_(v, 41 + 23.4 / 60, 1e-9));
  CHECK(nmeaParseCoord("4123.4000", 'S', &v));
  CHECK(near_(v, -(41 + 23.4 / 60), 1e-9));
  CHECK(nmeaParseCoord("00209.8460", 'E', &v));
  CHECK(near_(v, 2 + 9.846 / 60, 1e-9));
  CHECK(nmeaParseCoord("00209.8460", 'W', &v));
  CHECK(v < 0);
  CHECK(!nmeaParseCoord("4123.4", 'X', &v));
  CHECK(!nmeaParseCoord("41.38", 'N', &v));  // formato decimal no NMEA
  CHECK(!nmeaParseCoord("", 'N', &v));
}

TEST(nmea, checksum) {
  std::string line = mk("GPRMC,121219.0,A,4123.3980,N,00209.8460,E,0.0,0.0,010126,,,");
  CHECK(nmeaCheckSum(line.c_str()));
  std::string bad = line;
  bad[3] = 'X';  // corrompe el cuerpo sin tocar el *XX
  CHECK(!nmeaCheckSum(bad.c_str()));
  CHECK(!nmeaCheckSum("GPRMC,no dollar"));
  CHECK(!nmeaCheckSum("$GPRMC,no star"));
}

TEST(nmea, rmc_gga_montan_el_fix_completo) {
  NmeaParser p;
  std::string rmc = mk("GNRMC,121219.0,A,4123.3980,N,00209.8460,E,4.86,0.0,010126,,,");
  std::string gga = mk("GNGGA,121219.0,4123.3980,N,00209.8460,E,1,09,0.9,12.0,M,50.0,M,,");
  CHECK(p.feedLine(rmc.c_str(), 4000));
  CHECK(p.feedLine(gga.c_str(), 5000));
  const FixState &f = p.fix();
  CHECK(f.hasFix);
  CHECK(near_(f.latDeg, 41.38997, 2e-4));  // 4123.3980"N = 41.38997
  CHECK(near_(f.lonDeg, 2.1641, 2e-4));
  CHECK(near_(f.speedMs, 4.86 * 0.514444, 1e-3));  // nudos -> m/s
  CHECK_EQ(f.quality, 1);
  CHECK_EQ(f.sats, 9);
  CHECK_EQ(f.hdopX10, 9);
  CHECK_EQ(f.lastFixMs, 5000u);
}

TEST(nmea, gga_antes_que_rmc_tambien_fija_y_rmc_v_suelta_el_fix) {
  NmeaParser p;
  std::string gga = mk("GNGGA,121219.0,4123.3980,N,00209.8460,E,1,09,0.9,12.0,M,50.0,M,,");
  std::string rmcA = mk("GNRMC,121220.0,A,4123.3980,N,00209.8460,E,0.0,0.0,010126,,,");
  std::string rmcV = mk("GNRMC,121221.0,V,,,,,,,010126,,,");
  CHECK(p.feedLine(gga.c_str(), 1000));
  CHECK(!p.fix().hasFix);  // GGA solo no basta: falta RMC 'A'
  CHECK(p.feedLine(rmcA.c_str(), 2000));
  CHECK(p.fix().hasFix);
  CHECK_EQ(p.fix().lastFixMs, 2000u);
  CHECK(p.feedLine(rmcV.c_str(), 3000));
  CHECK(!p.fix().hasFix);
}

TEST(nmea, gga_calidad_0_sin_fix_y_frases_ajenas_ignoradas) {
  NmeaParser p;
  std::string rmc = mk("GNRMC,121219.0,A,4123.3980,N,00209.8460,E,0.0,0.0,010126,,,");
  std::string gga0 = mk("GNGGA,121219.0,,,,,0,00,99.9,,,,,");
  std::string vtg = mk("GNVTG,,,,,,,,,N");
  CHECK(p.feedLine(rmc.c_str(), 1000));
  CHECK(p.feedLine(gga0.c_str(), 2000));
  CHECK(!p.fix().hasFix);
  CHECK_EQ(p.fix().quality, 0);
  CHECK(!p.feedLine(vtg.c_str(), 3000));  // no es GGA ni RMC
}

TEST(nmea, feedByte_reensambla_y_re_sincroniza) {
  NmeaParser p;
  std::string rmc = mk("GNRMC,121219.0,A,4123.3980,N,00209.8460,E,0.0,0.0,010126,,,");
  std::string gga = mk("GNGGA,121219.0,4123.3980,N,00209.8460,E,1,08,1.0,12.0,M,50.0,M,,");
  std::string soup = "basura antes\r\n" + rmc + "\r\n" + gga + "\r\n";
  for (char c : soup) p.feedByte((uint8_t)c, 9000);
  CHECK(p.fix().hasFix);
  // corte a mitad y reencuadre con '$': la frase limpia siguiente sigue valiendo
  const char *half = "$GNGGA,121219.0,4123.3";
  for (const char *q = half; *q; q++) p.feedByte((uint8_t)*q, 9500);
  CHECK(p.feedLine(rmc.c_str(), 9600));
  CHECK_EQ(p.fix().lastFixMs, 9600u);
}

TEST(nmea, linea_corrupta_no_toca_el_estado) {
  NmeaParser p;
  std::string rmc = mk("GNRMC,121219.0,A,4123.3980,N,00209.8460,E,0.0,0.0,010126,,,");
  std::string gga = mk("GNGGA,121219.0,4123.3980,N,00209.8460,E,1,08,1.0,12.0,M,50.0,M,,");
  CHECK(p.feedLine(rmc.c_str(), 1000));
  CHECK(p.feedLine(gga.c_str(), 2000));
  double latBefore = p.fix().latDeg;
  uint32_t tBefore = p.fix().lastFixMs;
  std::string bad = gga;
  bad[bad.find("4123")] = 'X';  // cuerpo roto, checksum viejo
  CHECK(!p.feedLine(bad.c_str(), 3000));
  CHECK(!p.feedLine("GNRMC sin dollar ni star", 3100));
  CHECK(near_(p.fix().latDeg, latBefore, 1e-12));
  CHECK_EQ(p.fix().lastFixMs, tBefore);
}
