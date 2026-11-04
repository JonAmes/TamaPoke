#pragma once
#include <stdint.h>

// Nucleo puro de combate (B1): turnos, danios, captura. Sin Pet, sin NVS,
// sin millis: el glue rellena BattleSide desde Pet/DEX_TBL y aqui todo es
// determinista con semilla splitmix64, igual que el motor de encuentros.

struct BattleSide {
  uint16_t species = 0;  // num de dex (0 = sin sprite)
  uint16_t level = 1;
  uint16_t atk = 10, def = 10, spe = 10;
  uint16_t hpMax = 10, hp = 10;
  bool shiny = false;
};

// PV de combate: 2 x base gen-1 + 3 x nivel (los PV no se persisten: la
// mascota virtual nunca "muere", solo queda noqueada y hunde la energia)
uint16_t battleHpMax(uint8_t baseHp, uint16_t level);

// salvaje a partir de su ficha de encuentro: genes 100, sin entrenamiento
BattleSide battleWildSide(uint16_t dex, uint16_t level, bool shiny);

enum : uint8_t { B_ACT_ATTACK = 0, B_ACT_CAPTURE = 1, B_ACT_RUN = 2 };

enum : uint8_t {
  B_LIVE = 0,        // sigue el combate
  B_WILD_FAINTED,    // el salvaje cayo
  B_PLAYER_FAINTED,  // el companero cayo (huida automatica)
  B_CAUGHT,          // captura exitosa
  B_RAN,             // huida consumada
  B_CAPTURE_FAILED   // la captura fallo (un turno mas de combate)
};

// Probabilidad base de captura por rareza de DEX_TBL (x100)
uint8_t battleCatchBase(uint8_t rarity);

class Battle {
 public:
  void setSeed(uint64_t seed);
  void start(const BattleSide &player, const BattleSide &wild);

  // una accion del jugador + contraataque del salvaje si sigue en pie.
  // Devuelve el estado tras el turno (B_LIVE mientras dure).
  uint8_t act(uint8_t action);

  uint8_t result() const { return m_res; }
  uint8_t rounds() const { return m_rounds; }
  const BattleSide &player() const { return m_pl; }
  const BattleSide &wild() const { return m_wd; }

  // probabilidad de captura (x1000) con el estado actual: rareza x PV
  // restantes del salvaje x diferencia de nivel; topada para que nunca
  // sea segura (leyendas al menos 3%) ni desesperante (min 1%)
  uint16_t captureOddsX1000() const;

 private:
  uint16_t rollDamage(const BattleSide &a, const BattleSide &d);
  bool rollRun();
  uint32_t rnd();

  BattleSide m_pl, m_wd;
  uint64_t m_rng = 1;
  uint8_t m_rounds = 0;
  uint8_t m_res = B_LIVE;
};
