// Tests de celdas y motor de encuentros (gps/spawn.h + spawn_table.cpp).
#include "framework.h"
#include <cstdio>
#include <cstring>
#include "../gps/spawn.h"

TEST(spawn, clave_de_celda_de_1_grado) {
  CHECK_EQ(spawnCellKey(41.3866, 2.1641), 131 * 360 + 182);  // floor(41), floor(2)
  CHECK_EQ(spawnCellKey(41.9999, 2.0), spawnCellKey(41.0, 2.0));  // misma celda
  CHECK(spawnCellKey(42.0, 2.0) != spawnCellKey(41.9999, 2.0));
  CHECK_EQ(spawnCellKey(-33.86, 151.2), 56 * 360 + 331);  // floor(-33.86) = -34
  CHECK_EQ(spawnCellKey(89.9, 179.9), 179 * 360 + 359);
  CHECK_EQ(spawnCellKey(-90.0, -180.0), 0);
}

TEST(spawn, celdas_nombradas_cubiertas_por_la_tabla) {
  for (int i = 0; i < spawnTableCount(); i++) {
    const char *n = spawnTableName(spawnTableKeys()[i]);
    CHECK(n != nullptr);
    CHECK(strlen(n) > 0);
  }
  CHECK(spawnTableName(spawnCellKey(41.38, 2.16)) != nullptr);    // BARCELONA
  CHECK(spawnTableName(spawnCellKey(-33.86, 151.2)) != nullptr);  // SYDNEY
  CHECK(spawnTableName(spawnCellKey(0.0, 0.0)) == nullptr);       // golfo: climatica
}

static FixState fixAt(double lat, double lon) {
  FixState f;
  f.hasFix = true; f.latDeg = lat; f.lonDeg = lon; f.quality = 1;
  return f;
}

TEST(spawn, determinismo_misma_semilla_misma_ruta) {
  EncounterEngine a, b;
  a.setSeed(1234); b.setSeed(1234);
  FixState bcn = fixAt(41.3866, 2.1641);
  for (int i = 0; i < 5; i++) {
    WildSpawn x = a.roll(bcn, 20.0f, (uint32_t)(i * 1000));
    WildSpawn y = b.roll(bcn, 20.0f, (uint32_t)(i * 1000));
    CHECK_EQ(x.species, y.species);
    CHECK_EQ(x.level, y.level);
    CHECK_EQ(x.shiny, y.shiny);
  }
  // semilla distinta debe cambiar la ruta (para no repetir el dia entre sesiones)
  EncounterEngine c, d;
  c.setSeed(9999); d.setSeed(1234);
  bool differs = false;
  for (int i = 0; i < 10; i++) {
    WildSpawn x = c.roll(bcn, 20.0f, (uint32_t)(i * 1000));
    WildSpawn y = d.roll(bcn, 20.0f, (uint32_t)(i * 1000));
    if (x.species != y.species || x.level != y.level) { differs = true; break; }
  }
  CHECK_MSG(differs, "semillas distintas dieron la misma ruta de 10 tiradas");
}

TEST(spawn, especies_validas_y_niveles_en_rango) {
  EncounterEngine e;
  e.setSeed(7);
  double locs[][2] = {{41.38, 2.16}, {40.4, -3.7}, {0.0, 0.0}, {-70.0, -70.0},
                      {10.0, 10.0}, {-33.86, 151.2}};
  for (auto &lo : locs) {
    FixState f = fixAt(lo[0], lo[1]);
    for (int i = 0; i < 200; i++) {
      WildSpawn w = e.roll(f, (float)(i * 30), (uint32_t)i);
      CHECK_RANGE(w.species, 1, 151);  // gen-1: dentro de DEX_COUNT
      CHECK_RANGE(w.level, 2, 19);
    }
  }
  // sin fijacion (degradado Guition) tambien produce
  FixState none;
  for (int i = 0; i < 200; i++) {
    WildSpawn w = e.roll(none, (float)(i * 40), (uint32_t)i);
    CHECK_RANGE(w.species, 1, 151);
  }
}

TEST(spawn, tier_por_distancia_sube_el_peso_de_los_raros) {
  // misma semilla, dos distancias: con >=1000 m la fraccion de tier2 sube
  int rare_near = 0, rare_far = 0, rare_mid = 0;
  const int N = 4000;
  EncounterEngine a, b, c;
  a.setSeed(555); b.setSeed(555); c.setSeed(555);
  FixState ocean = fixAt(20.0, 0.0);  // celda climatica tropical (sin nombre)
  for (int i = 0; i < N; i++) {
    if (a.roll(ocean, 5.0f, i).species == 129) rare_near++;    // Magikarp tier2
    if (b.roll(ocean, 5000.0f, i).species == 129) rare_far++;  // mismo pool
    if (c.roll(ocean, 150.0f, i).species == 129) rare_mid++;
  }
  char msg[96];
  snprintf(msg, sizeof(msg), "near=%d mid=%d far=%d", rare_near, rare_mid, rare_far);
  CHECK_MSG(rare_far > rare_near, msg);
  CHECK_MSG(rare_mid > rare_near, msg);
}

TEST(spawn, umbrales_de_tier_y_brillo_estable) {
  CHECK_EQ(EncounterEngine::rarityTier(0.0f), 0);
  CHECK_EQ(EncounterEngine::rarityTier(99.9f), 0);
  CHECK_EQ(EncounterEngine::rarityTier(100.0f), 1);
  CHECK_EQ(EncounterEngine::rarityTier(999.0f), 1);
  CHECK_EQ(EncounterEngine::rarityTier(1000.0f), 2);
  // shiny ~1/64: en 4096 tiradas, entre 20 y 110
  EncounterEngine e; e.setSeed(31337);
  int shiny = 0;
  FixState f = fixAt(48.85, 2.35);
  for (int i = 0; i < 4096; i++) if (e.roll(f, 10.0f, i).shiny) shiny++;
  char msg[48];
  snprintf(msg, sizeof(msg), "shiny=%d fuera de banda esperada", shiny);
  CHECK_MSG(shiny > 20 && shiny < 110, msg);
}
