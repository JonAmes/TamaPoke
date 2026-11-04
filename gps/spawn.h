// Nucleo puro de encuentros salvajes (B1/B2 capa 1-3, plan.md §5b.1-3).
// Tabla de celdas de rejilla lat/lon -> lista de especies ponderada, y un
// motor de sorteo determinista (hash, sin rand()). Todo host-testeable.
#pragma once
#include <stdint.h>
#include "nmea.h"

// Celda de rejilla: 1 grado. key = (floor(lat)+90)*360 + floor(lon)+180.
// Celdas fuera de la tabla usan el pool climatico (tropico/templado/polar).
int32_t spawnCellKey(double latDeg, double lonDeg);
#define SPAWN_CELL_NONE 0x7fffffff  // key de reserva: sin fijacion

struct SpawnEntry {
  uint16_t species;  // Pokedex #1..151
  uint8_t weight;    // ponderacion local dentro de la celda/pool
  uint8_t tier;      // 0 comun, 1 infrecuente, 2 rara (la distancia las sube)
};

// Numero de pools/celdas y acceso para tests:
int spawnTableCount();                         // celdas nombradas en la tabla
const int32_t *spawnTableKeys();               // claves ordenadas
const char *spawnTableName(int32_t cellKey);   // "BARCELONA" o nullptr

struct WildSpawn {
  uint16_t species = 0;
  uint8_t level = 0;
  bool shiny = false;
};

class EncounterEngine {
 public:
  void setSeed(uint32_t s) { m_seed = s; }
  // distM: metros recorridos desde el ultimo encuentro (GPS integra
  // velocidad x dt; sin fix el glue convierte pasos*0.75). tier = premio de
  // rareza: <100m tier0, 100-999m tier1, >=1000m tier2.
  WildSpawn roll(const FixState &fx, float distM, uint32_t nowMs);
  static uint8_t rarityTier(float distM);
  // pool activo para una celda: 0 nombrada, 1 tropico, 2 templado, 3 polar,
  // 4 sin-fijacion. Para tests:
  static const SpawnEntry *poolFor(int32_t cellKey, uint8_t *n);

 private:
  uint32_t m_seed = 0xC0FFEEu;
  uint32_t m_ctr = 0;
};
