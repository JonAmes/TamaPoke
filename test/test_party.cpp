// Tests del modelo de party (F34) sobre los shims del host.
#include "framework.h"
#include "shim/Arduino.h"
#include "../pet.h"
#include "../dex.h"
#include <string.h>

// ---------------------------------------------------------------- helpers

// party arrancado con un bicho ya eclosionado en el slot 0 (NVS limpia)
static void bootParty(Party &q, int16_t dex) {
  mockNvsReset();
  mockSetMillis(0);
  q.begin();
  if (q.pet.awaitingStarter()) q.pet.chooseStarter(dex);
  q.pet.eggTap();
  q.pet.eggTap();
  q.pet.eggTap();
}

static void advanceParty(Party &q, uint32_t minutes) {
  for (uint32_t i = 0; i < minutes; i++) {
    mockAdvanceMillis(PET_TICK_MS);
    q.update(millis());
  }
}

static PetRecord fakeRecord(int16_t dex, uint8_t full = 80) {
  PetRecord r;
  r.used = true;
  r.speciesId = dex;
  r.fullness = full;
  r.joy = 80;
  r.energy = 80;
  r.hygiene = 90;
  r.geneAtk = r.geneDef = r.geneSpe = 100;
  r.ageMinutes = 1440;  // 1 dia: ya tiene nivel, no es huevo
  return r;
}

// ---------------------------------------------------------------- ciclo basico

TEST(party, nace_con_un_solo_slot) {
  Party q;
  bootParty(q, 4);
  CHECK(q.slotUsed(0));
  CHECK(!q.slotUsed(1));
  CHECK_EQ(q.filled(), 1);
  CHECK_EQ(q.activeSlot(), 0);
}

TEST(party, focus_cambia_el_companero) {
  Party q;
  bootParty(q, 4);  // slot 0: VENEUSAUR (dex 4)
  CHECK(q.add(fakeRecord(25)));   // slot 1: PIKACHU
  CHECK(q.add(fakeRecord(150)));  // slot 2: MEWTWO
  CHECK_EQ(q.filled(), 3);
  CHECK(q.focus(2));
  CHECK_EQ(q.activeSlot(), 2);
  CHECK_EQ(q.pet.speciesId, (int16_t)150);
  CHECK(q.focus(1));
  CHECK_EQ(q.pet.speciesId, (int16_t)25);
  CHECK(!q.focus(1));  // ya esta enfocado
  CHECK(!q.focus(3));  // slot vacio
  CHECK(!q.focus(9));  // fuera de rango
  CHECK_EQ(q.pet.speciesId, (int16_t)25);  // nada de lo anterior cambió el foco
}

TEST(party, el_enfoque_sobrevive_al_reinicio) {
  Party q;
  bootParty(q, 4);
  q.add(fakeRecord(25));
  q.add(fakeRecord(150));
  q.pet.fullness = 61;  // senala al activo de slot 0
  CHECK(q.focus(2));

  Party r;
  r.begin();  // mismo "NVS": reboot
  CHECK_EQ(r.activeSlot(), 2);
  CHECK_EQ(r.pet.speciesId, (int16_t)150);
  CHECK_EQ(r.slots[0].fullness, (uint8_t)61);  // el export del saliente persistio
  CHECK_EQ(r.filled(), 3);
  CHECK(r.slotUsed(0) && r.slotUsed(1));
  CHECK(!r.slotUsed(3));
  CHECK(r.focus(0));  // tras reiniciar se puede volver al primero
  CHECK_EQ(r.pet.speciesId, (int16_t)4);
  CHECK_EQ(r.pet.fullness, (uint8_t)61);  // y recupera su estado exacto
}

TEST(party, party_lleno_rechaza_sin_sobrescribir) {
  Party q;
  bootParty(q, 4);
  CHECK(q.add(fakeRecord(2)));
  CHECK(q.add(fakeRecord(3)));
  CHECK(q.add(fakeRecord(4)));
  CHECK_EQ(q.filled(), PARTY_MAX);
  CHECK_EQ(q.firstFree(), -1);
  CHECK(!q.add(fakeRecord(150)));  // lleno: rechazo limpio
  CHECK_EQ(q.slots[1].speciesId, (int16_t)2);  // nada sobrescrito
  CHECK_EQ(q.slots[3].speciesId, (int16_t)4);
}

TEST(party, quitar_slot_y_queda_hueco) {
  Party q;
  bootParty(q, 4);
  q.add(fakeRecord(25));
  q.add(fakeRecord(150));
  CHECK(!q.removeSlot(0));  // jamas el activo
  CHECK(q.removeSlot(1));
  CHECK(!q.slotUsed(1));
  CHECK_EQ(q.filled(), 2);
  CHECK(q.add(fakeRecord(133)));  // reutiliza el hueco
  CHECK_EQ(q.activeSlot(), 0);
  CHECK_EQ(q.slots[1].speciesId, (int16_t)133);

  Party r;
  r.begin();
  CHECK(r.slotUsed(0) && r.slotUsed(1) && r.slotUsed(2));
  CHECK(!r.slotUsed(3));
}

// ---------------------------------------------------------------- reservas

TEST(party, reservas_envejecen_lento_y_con_suelo) {
  Party q;
  bootParty(q, 4);  // bicho despierto y activo
  q.add(fakeRecord(25));
  uint32_t ageBefore = q.slots[1].ageMinutes;

  advanceParty(q, 400);

  // el activo envejece igual pero decae ciclo completo (2/min, sin suelo)
  CHECK(q.pet.ageMinutes >= 400);
  CHECK_EQ(q.pet.fullness, (uint8_t)0);
  // la reserva envejece la misma edad pero decae 4x mas lento y para en el suelo
  CHECK_EQ(q.slots[1].ageMinutes, ageBefore + 400);
  CHECK(q.slots[1].fullness >= RESERVE_FLOOR);
  CHECK(q.slots[1].joy >= RESERVE_FLOOR);
  CHECK(q.slots[1].energy >= RESERVE_FLOOR);
  CHECK(q.pet.fullness < q.slots[1].fullness);
}

TEST(party, las_acciones_solo_afectan_al_activo) {
  Party q;
  bootParty(q, 4);
  q.add(fakeRecord(25));
  PetRecord antes = q.slots[1];

  q.pet.trainStrength(40);
  q.pet.playResult(10);
  q.pet.feedCandy();
  q.pet.caress();

  CHECK(q.pet.trAtk > 0);      // el activo si entreno
  CHECK(q.pet.trSpe > 0);
  CHECK_EQ(q.slots[1].trAtk, antes.trAtk);    // la reserva intacta
  CHECK_EQ(q.slots[1].trSpe, antes.trSpe);
  CHECK_EQ(q.slots[1].fullness, antes.fullness);
  CHECK_EQ(q.slots[1].bond, antes.bond);
  CHECK_EQ(q.slots[1].speciesId, antes.speciesId);  // pasos/XP = solo activo
}

TEST(party, offline_progresion_completa_y_reservas_lentas) {
  Party q;
  bootParty(q, 4);
  q.add(fakeRecord(25));
  q.setClock(1000000);

  Party r;
  r.begin();
  uint32_t ageA = r.pet.ageMinutes, ageB = r.slots[1].ageMinutes;
  r.syncClock(1000000 + 3 * 86400);  // 3 dias apagado

  CHECK_EQ(r.pet.ageMinutes, ageA + 3 * 1440);   // el activo envejece entero
  CHECK_EQ(r.slots[1].ageMinutes, ageB + 3 * 1440);  // las reservas igual de edad
  // offline: el activo baja con suelo 15, la reserva con suelo 40
  CHECK(r.pet.fullness < q.slots[1].fullness);
  CHECK(r.slots[1].fullness >= RESERVE_FLOOR);
  CHECK(r.pet.fullness < r.slots[1].fullness);
  CHECK_EQ(r.pet.poops, (uint8_t)3);   // el activo si ensucia offline
  CHECK_EQ(r.slots[1].poops, (uint8_t)0);    // en reserva no se ensucia jamas
  CHECK_EQ(r.slots[1].careMistakes, (uint8_t)0);  // ni descuidos
}

TEST(party, offline_topado_a_dos_semanas) {
  Party q;
  bootParty(q, 4);
  q.add(fakeRecord(25));
  q.setClock(1000000);

  Party r;
  r.begin();
  uint32_t ageA = r.pet.ageMinutes, ageB = r.slots[1].ageMinutes;
  r.syncClock(1000000 + 90 * 86400);  // 90 dias
  // ambas edades avanzaron exactamente el tope de 2 semanas
  CHECK_EQ(r.pet.ageMinutes, ageA + 14 * 24 * 60);
  CHECK_EQ(r.slots[1].ageMinutes, ageB + 14 * 24 * 60);
}

// ---------------------------------------------------------------- migracion

TEST(party, migracion_del_guardado_de_mascota_unica) {
  // guardado del formato antiguo (Pet standalone, sin meta de party)
  Pet p;
  extern void makePetLegacy(Pet &, int16_t);
  makePetLegacy(p, 25);

  Party q;
  q.begin();  // debe migrar al slot 0 sin tocar las claves
  CHECK_EQ(q.filled(), 1);
  CHECK_EQ(q.activeSlot(), 0);
  CHECK_EQ(q.pet.speciesId, (int16_t)25);
  CHECK(q.pet.isRegistered(25));
  CHECK(q.focus(0) == false);  // unico slot: ya activo

  q.add(fakeRecord(150));
  Party r;
  r.begin();
  CHECK_EQ(r.filled(), 2);
  CHECK_EQ(r.slots[1].speciesId, (int16_t)150);
}

// makePet para el test de migracion: usa Pet standalone a proposito (el
// formato antiguo nunca escribia pact/pmask)
void makePetLegacy(Pet &p, int16_t dex) {
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(dex);
  p.eggTap();
  p.eggTap();
  p.eggTap();
}

TEST(party, la_meta_de_party_sobrevive_a_la_migracion) {
  Pet p;
  extern void makePetLegacy(Pet &, int16_t);
  makePetLegacy(p, 1);

  Party q;
  q.begin();
  q.add(fakeRecord(2));
  CHECK(q.focus(1));

  Party r;
  r.begin();
  CHECK_EQ(r.activeSlot(), 1);  // pact persistio
  CHECK_EQ(r.pet.speciesId, (int16_t)2);
}
