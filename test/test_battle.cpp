#include "framework.h"
#include "../battle.h"
#include "../dex.h"

static BattleSide mkSide(uint16_t dex, uint16_t lv, uint16_t atk, uint16_t def,
                         uint16_t spe, uint16_t hp) {
  BattleSide s;
  s.species = dex; s.level = lv; s.atk = atk; s.def = def; s.spe = spe;
  s.hpMax = hp; s.hp = hp;
  return s;
}

TEST(battle, ficha_salvaje) {
  BattleSide w = battleWildSide(1, 10, false);  // Bulbasaur: 45/49/49/45
  CHECK_EQ(w.hpMax, 2 * 45 + 3 * 10);
  CHECK_EQ(w.atk, 59);
  CHECK_EQ(w.def, 59);
  CHECK_EQ(w.spe, 55);
  CHECK_EQ(w.hp, w.hpMax);
  // dex fuera de rango cae a 1 sin explotar
  CHECK_EQ(battleWildSide(0, 1, false).species, 1);
  CHECK_EQ(battleWildSide(999, 1, false).species, 1);
}

TEST(battle, prob_captura_monotona) {
  Battle b;
  b.start(mkSide(1, 20, 30, 30, 30, 100), battleWildSide(1, 10, false));
  uint16_t full = b.captureOddsX1000();
  BattleSide w = battleWildSide(1, 10, false);
  w.hp = 1;
  b.start(mkSide(1, 20, 30, 30, 30, 100), w);
  uint16_t low = b.captureOddsX1000();
  CHECK(low > full);

  // legendaria mucho mas dura que comun al mismo estado
  BattleSide leg = battleWildSide(150, 10, false);  // Mewtwo, R_LEGENDARIO
  b.start(mkSide(1, 20, 30, 30, 30, 100), leg);
  CHECK(b.captureOddsX1000() < full);

  // topes: nunca segura, nunca imposible; leyendas <= 35%
  CHECK(full <= 920 && full >= 10);
  b.start(mkSide(1, 999, 30, 30, 30, 100), leg);
  CHECK(b.captureOddsX1000() <= 350);
}

TEST(battle, determinismo) {
  uint8_t r1[8], r2[8];
  uint16_t hp1[8], hp2[8];
  for (int pass = 0; pass < 2; pass++) {
    Battle b;
    b.setSeed(4242);
    b.start(mkSide(25, 15, 40, 35, 30, 120), battleWildSide(16, 15, false));
    for (int i = 0; i < 8; i++) {
      uint8_t r = b.act(B_ACT_ATTACK);
      if (pass == 0) r1[i] = r; else r2[i] = r;
      if (pass == 0) hp1[i] = b.wild().hp; else hp2[i] = b.wild().hp;
    }
  }
  for (int i = 0; i < 8; i++) { CHECK_EQ(r1[i], r2[i]); CHECK_EQ(hp1[i], hp2[i]); }

  // semillas distintas -> trayectorias distintas
  Battle a, c;
  a.setSeed(1); c.setSeed(2);
  a.start(mkSide(25, 15, 40, 35, 30, 120), battleWildSide(16, 15, false));
  c.start(mkSide(25, 15, 40, 35, 30, 120), battleWildSide(16, 15, false));
  a.act(B_ACT_ATTACK); c.act(B_ACT_ATTACK);
  CHECK(a.wild().hp != c.wild().hp);
}

TEST(battle, danios_dentro_de_varianza) {
  // base = 8 + 60*220/200 = 74 -> todo golpe en [74*0.88, 74*1.12]
  Battle b;
  b.setSeed(7);
  b.start(mkSide(1, 1, 60, 100, 10, 60000), mkSide(1, 1, 0, 100, 10, 60000));
  uint16_t prev = b.wild().hp;
  for (int i = 0; i < 60; i++) {
    b.act(B_ACT_ATTACK);  // atk 0 rival no hace dano... base del rival 8: si
    uint16_t d = (uint16_t)(prev - b.wild().hp);
    CHECK(d >= 65 && d <= 83);
    prev = b.wild().hp;
  }
}

TEST(battle, termina_y_bloquea) {
  Battle b;
  b.setSeed(99);
  b.start(mkSide(1, 30, 90, 30, 40, 200), battleWildSide(10, 5, false));
  uint8_t r = B_LIVE;
  int guard = 0;
  while (r == B_LIVE && guard++ < 50) r = b.act(B_ACT_ATTACK);
  CHECK(r == B_WILD_FAINTED || r == B_PLAYER_FAINTED);
  uint16_t whp = b.wild().hp, phpc = b.player().hp;
  // acciones despues del final: mismo resultado, nada cambia
  for (int i = 0; i < 3; i++) {
    CHECK_EQ(b.act(B_ACT_ATTACK), r);
    CHECK_EQ(b.act(B_ACT_CAPTURE), r);
  }
  CHECK_EQ(b.wild().hp, whp);
  CHECK_EQ(b.player().hp, phpc);
}

TEST(battle, captura_exitosa_congeles) {
  bool caught = false;
  uint16_t before = 0;
  for (uint64_t seed = 1; seed < 200 && !caught; seed++) {
    Battle b;
    b.setSeed(seed);
    BattleSide w = battleWildSide(10, 3, false);  // Weedle lvl 3, muy blando
    w.hp = 1;
    b.start(mkSide(1, 30, 30, 30, 30, 100), w);
    before = b.wild().hp;
    if (b.act(B_ACT_CAPTURE) == B_CAUGHT) {
      caught = true;
      CHECK_EQ(b.wild().hp, before);  // capturar no dana
      CHECK_EQ(b.act(B_ACT_ATTACK), B_CAUGHT);
    }
  }
  CHECK(caught);
}

TEST(battle, mas_lento_recibe_primero) {
  // rival rapidisimo y letal: el jugador cae antes de atacar nunca
  Battle b;
  b.setSeed(5);
  b.start(mkSide(1, 5, 10, 10, 5, 20), mkSide(6, 50, 90, 90, 200, 400));
  uint8_t r = b.act(B_ACT_ATTACK);
  CHECK_EQ(r, B_PLAYER_FAINTED);
  CHECK_EQ(b.wild().hp, 400);  // ni lo rozamos: golpeo el rapido primero
}

TEST(battle, huir_probable_con_ventaja) {
  int ran = 0;
  for (uint64_t seed = 1; seed <= 50; seed++) {
    Battle b;
    b.setSeed(seed);
    b.start(mkSide(1, 30, 10, 10, 200, 100), battleWildSide(10, 5, false));
    if (b.act(B_ACT_RUN) == B_RAN) ran++;
  }
  CHECK(ran >= 40);  // tope 95%: casi siempre
  // sin ventaja tambien hay oportunidad (>=20%)
  int ran2 = 0;
  for (uint64_t seed = 1; seed <= 50; seed++) {
    Battle b;
    b.setSeed(seed);
    b.start(mkSide(1, 5, 10, 10, 10, 100), mkSide(1, 5, 10, 10, 200, 100));
    if (b.act(B_ACT_RUN) == B_RAN) ran2++;
  }
  CHECK(ran2 >= 5);
}
