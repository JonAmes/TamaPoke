// Tablas de spawn y motor determinista (gps/spawn.h). Gen-1 nada mas: los
// IDs viven dentro de DEX_COUNT=151. Celdas nombradas = ciudades donde se
// espera jugar; el resto del planeta cae en pools climaticos.
#include "gps/spawn.h"
#include <string.h>
#include <math.h>

#define K(lat, lon) (((int32_t)(lat) + 90) * 360 + ((int32_t)(lon) + 180))

int32_t spawnCellKey(double latDeg, double lonDeg) {
  int32_t la = (int32_t)floor(latDeg);
  int32_t lo = (int32_t)floor(lonDeg);
  if (la < -90) la = -90;
  if (la > 89) la = 89;
  if (lo < -180) lo = -180;
  if (lo > 179) lo = 179;
  return la * 360 + lo + 90 * 360 + 180;
}

// ---- pools ----
static const SpawnEntry P_BARÇA[] = {
  {19, 5, 0}, {41, 4, 0}, {74, 3, 0}, {58, 2, 1}, {81, 1, 2},  // Rattata Zubat Geodude Growlithe Magnemite
};
static const SpawnEntry P_MADRID[] = {
  {19, 5, 0}, {27, 4, 0}, {74, 3, 0}, {104, 2, 1}, {111, 1, 2},  // Rattata Sandshrew Geodede... Cubone Rhyhorn
};
static const SpawnEntry P_PARIS[] = {
  {16, 5, 0}, {41, 4, 0}, {39, 2, 1}, {122, 1, 2},  // Pidgey Zubat Jigglypuff MrMime
};
static const SpawnEntry P_LONDON[] = {
  {16, 5, 0}, {41, 4, 0}, {90, 2, 1}, {143, 1, 2},  // Pidgey Zubat Shellder Snorlax
};
static const SpawnEntry P_NY[] = {
  {19, 5, 0}, {41, 4, 0}, {25, 2, 1}, {88, 1, 2},   // Rattata Zubat Pikachu Muk
};
static const SpawnEntry P_TOKYO[] = {
  {23, 5, 0}, {29, 4, 0}, {25, 2, 1}, {131, 1, 2},  // Ekans NidoranF Pikachu Eevee
};
static const SpawnEntry P_SYDNEY[] = {
  {21, 5, 0}, {102, 3, 0}, {115, 2, 1}, {127, 1, 2},  // Spearow Exeggcute Kanga Pinsir
};
static const SpawnEntry P_TROPIC[] = {
  {10, 4, 0}, {23, 4, 0}, {56, 3, 0}, {100, 2, 1}, {129, 1, 2},  // Caterpie Ekans Mankey Voltorb Magikarp
};
static const SpawnEntry P_TEMPERATE[] = {
  {19, 4, 0}, {16, 4, 0}, {41, 3, 0}, {58, 2, 1}, {113, 1, 2},   // Rattata Pidgey Zubat Growlithe Chansey
};
static const SpawnEntry P_POLAR[] = {
  {60, 4, 0}, {86, 4, 0}, {87, 2, 1}, {124, 1, 2},  // Poliwag Seel Dewgong Jynx
};
static const SpawnEntry P_NOFIX[] = {
  {19, 5, 0}, {16, 4, 0}, {41, 4, 0}, {10, 3, 0}, {133, 1, 2},   // sin fijacion: los de siempre + Eevee
};

struct CellRow { int32_t key; const char *name; const SpawnEntry *e; uint8_t n; };
static const CellRow CELLS[] = {
  {K(41, 2),   "BARCELONA", P_BARÇA,   5},
  {K(40, -4),  "MADRID",    P_MADRID,  5},
  {K(48, 2),   "PARIS",     P_PARIS,   4},
  {K(51, -1),  "LONDON",    P_LONDON,  4},
  {K(40, -75), "NEWYORK",   P_NY,      4},
  {K(35, 139), "TOKYO",     P_TOKYO,   4},
  {K(-34, 151),"SYDNEY",    P_SYDNEY,  4},
};
#define N_CELLS ((int)(sizeof CELLS / sizeof CELLS[0]))

int spawnTableCount() { return N_CELLS; }

static int32_t s_keys[N_CELLS];
const int32_t *spawnTableKeys() {
  for (int i = 0; i < N_CELLS; i++) s_keys[i] = CELLS[i].key;
  return s_keys;
}

const char *spawnTableName(int32_t cellKey) {
  for (int i = 0; i < N_CELLS; i++)
    if (CELLS[i].key == cellKey) return CELLS[i].name;
  return nullptr;
}

const SpawnEntry *EncounterEngine::poolFor(int32_t cellKey, uint8_t *n) {
  for (int i = 0; i < N_CELLS; i++)
    if (CELLS[i].key == cellKey) { *n = CELLS[i].n; return CELLS[i].e; }
  if (cellKey == SPAWN_CELL_NONE) { *n = 5; return P_NOFIX; }
  int32_t la = cellKey / 360 - 90;  // banda climatica por |latitud|
  if (la < 0) la = -la;             // espejo sur: mismo pool
  if (la <= 23) { *n = 5; return P_TROPIC; }
  if (la < 55) { *n = 5; return P_TEMPERATE; }
  *n = 4; return P_POLAR;
}

uint8_t EncounterEngine::rarityTier(float distM) {
  if (distM >= 1000.0f) return 2;
  if (distM >= 100.0f) return 1;
  return 0;
}

// hash determinista splitmix64: misma semilla+celda+contador -> mismo bicho
static uint64_t mix(int32_t key, uint32_t seed, uint32_t ctr) {
  uint64_t x = (uint64_t)(uint32_t)key * 0x9E3779B97F4A7C15ull +
               ((uint64_t)seed << 32) + ctr + 0x123ABCDull;
  x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 27; x *= 0x94D049BB133111EBull;
  x ^= x >> 31;
  return x;
}

WildSpawn EncounterEngine::roll(const FixState &fx, float distM, uint32_t nowMs) {
  (void)nowMs;  // reloj reservado para eventos "go capture" (capa 2)
  int32_t key = fx.hasFix ? spawnCellKey(fx.latDeg, fx.lonDeg) : SPAWN_CELL_NONE;
  uint8_t n = 0;
  const SpawnEntry *pool = poolFor(key, &n);
  uint8_t tier = rarityTier(distM);
  uint16_t total = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint16_t w = pool[i].weight;
    if (pool[i].tier && tier) w = (uint16_t)(w * (1 + pool[i].tier * tier * 2));
    total += w;
  }
  uint64_t h = mix(key, m_seed, m_ctr++);
  uint16_t r = (uint16_t)(h % total);
  WildSpawn out;
  for (uint8_t i = 0; i < n; i++) {
    uint16_t w = pool[i].weight;
    if (pool[i].tier && tier) w = (uint16_t)(w * (1 + pool[i].tier * tier * 2));
    if (r < w) { out.species = pool[i].species; break; }
    r = (uint16_t)(r - w);
  }
  out.level = (uint8_t)(2 + ((h >> 32) % 18));   // 2..19
  out.shiny = ((h >> 17) & 63) == 0;             // ~1/64
  return out;
}
