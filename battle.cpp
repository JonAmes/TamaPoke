// Implementacion en la RAIZ: Arduino solo compila .cpp de la raiz del sketch.
#include "battle.h"
#include "dex.h"

uint16_t battleHpMax(uint8_t baseHp, uint16_t level) {
  uint32_t hp = 2u * baseHp + 3u * level;
  return hp > 0xffff ? 0xffff : (uint16_t)hp;
}

BattleSide battleWildSide(uint16_t dex, uint16_t level, bool shiny) {
  BattleSide s;
  if (dex < 1 || dex > DEX_COUNT) dex = 1;
  const DexEntry &e = DEX_TBL[dex];
  s.species = dex;
  s.level = level;
  s.shiny = shiny;
  s.atk = (uint16_t)(e.bAtk + level);
  s.def = (uint16_t)(e.bDef + level);
  s.spe = (uint16_t)(e.bSpe + level);
  s.hpMax = battleHpMax(e.bHp, level);
  s.hp = s.hpMax;
  return s;
}

uint8_t battleCatchBase(uint8_t rarity) {
  switch (rarity) {
    case R_COMUN: return 56;
    case R_RARO: return 30;
    case R_LEGENDARIO: return 8;
    default: return 36;  // R_EVO: capturable pero menos comun
  }
}

void Battle::setSeed(uint64_t seed) { m_rng = seed ? seed : 1; }

void Battle::start(const BattleSide &player, const BattleSide &wild) {
  m_pl = player;
  m_wd = wild;
  m_rounds = 0;
  m_res = B_LIVE;
}

uint32_t Battle::rnd() {  // splitmix64: determinista con semilla conocida
  m_rng += 0x9e3779b97f4a7c15ULL;
  uint64_t z = m_rng;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return (uint32_t)(z ^ (z >> 31));
}

uint16_t Battle::rollDamage(const BattleSide &a, const BattleSide &d) {
  uint32_t base = 8u + (uint32_t)a.atk * 220u / ((uint32_t)d.def + 100u);
  // varianza +/-12%: multiplo entero 88..112 sobre 100
  uint32_t pct = 88u + rnd() % 25u;
  uint32_t dmg = base * pct / 100u;
  return dmg ? (uint16_t)dmg : 1u;
}

bool Battle::rollRun() {
  // 60% base, +/-2 puntos por nivel de velocidad de diferencia, [20%, 95%]
  int sp = (int)m_pl.spe - (int)m_wd.spe;
  if (sp > 20) sp = 20;
  if (sp < -20) sp = -20;
  int pct = 60 + sp * 2;
  if (pct < 20) pct = 20;
  if (pct > 95) pct = 95;
  return rnd() % 100u < (uint32_t)pct;
}

uint16_t Battle::captureOddsX1000() const {
  if (m_wd.hpMax == 0) return 0;
  uint8_t rarity = DEX_TBL[m_wd.species <= DEX_COUNT ? m_wd.species : 1].rarity;
  uint32_t odds = battleCatchBase(rarity);
  // PV restantes del salvaje: con poca vida hasta x1.3, a tope x0.5
  uint32_t hpFracX100 = (uint32_t)m_wd.hp * 100u / m_wd.hpMax;  // 0..100
  uint32_t hpMulX100 = 130u - hpFracX100 * 80u / 100u;          // 50..130
  odds = odds * hpMulX100 / 100u;
  // nivel: +2% relativo por nivel de ventaja, -2% por desventaja (tope +-30)
  int lv = (int)m_pl.level - (int)m_wd.level;
  if (lv > 30) lv = 30;
  if (lv < -30) lv = -30;
  odds = odds * (uint32_t)(100 + lv * 2) / 100u;
  if (m_wd.shiny && odds > 1000) odds = 1000;  // los shiny no se vuelven faciles
  uint32_t cap = rarity == R_LEGENDARIO ? 350u : 920u;
  if (odds > cap) odds = cap;
  if (odds < 10) odds = 10;
  return (uint16_t)odds;
}

uint8_t Battle::act(uint8_t action) {
  if (m_res != B_LIVE) return m_res;
  m_rounds++;
  bool plFirst = m_pl.spe >= m_wd.spe;

  if (action == B_ACT_RUN) {
    if (rollRun()) {
      m_res = B_RAN;
      return m_res;
    }
    // no pudo huir: el salvaje pega
    uint16_t dmg = rollDamage(m_wd, m_pl);
    m_pl.hp = (uint16_t)(m_pl.hp > dmg ? m_pl.hp - dmg : 0);
    if (m_pl.hp == 0) m_res = B_PLAYER_FAINTED;
    return m_res;
  }

  if (action == B_ACT_CAPTURE) {
    if (rnd() % 1000u < captureOddsX1000()) {
      m_res = B_CAUGHT;
      return m_res;
    }
    // fallo de captura: aviso del turno, pero el combate SIGUE (m_res no
    // se toca, o el proximo act() se creeria terminado)
    if (plFirst) {
      uint16_t dmg = rollDamage(m_wd, m_pl);
      m_pl.hp = (uint16_t)(m_pl.hp > dmg ? m_pl.hp - dmg : 0);
      if (m_pl.hp == 0) m_res = B_PLAYER_FAINTED;
    }
    return m_res == B_LIVE ? (uint8_t)B_CAPTURE_FAILED : m_res;
  }

  // B_ACT_ATTACK: orden por velocidad
  if (plFirst) {
    uint16_t dmg = rollDamage(m_pl, m_wd);
    m_wd.hp = (uint16_t)(m_wd.hp > dmg ? m_wd.hp - dmg : 0);
    if (m_wd.hp == 0) {
      m_res = B_WILD_FAINTED;
      return m_res;
    }
    uint16_t back = rollDamage(m_wd, m_pl);
    m_pl.hp = (uint16_t)(m_pl.hp > back ? m_pl.hp - back : 0);
    if (m_pl.hp == 0) m_res = B_PLAYER_FAINTED;
    return m_res;
  }
  uint16_t first = rollDamage(m_wd, m_pl);
  m_pl.hp = (uint16_t)(m_pl.hp > first ? m_pl.hp - first : 0);
  if (m_pl.hp == 0) {
    m_res = B_PLAYER_FAINTED;
    return m_res;
  }
  uint16_t back = rollDamage(m_pl, m_wd);
  m_wd.hp = (uint16_t)(m_wd.hp > back ? m_wd.hp - back : 0);
  if (m_wd.hp == 0) m_res = B_WILD_FAINTED;
  return m_res;
}
