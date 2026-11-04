// Nucleo de protocolo P2P (F27): enmarcado/CRC, codificador de registro y
// maquinas de estado de canje y traslado con dos LinkSM enfrentados por un
// transporte falso. DOCS/plan.md §5c/§5d.
#include "framework.h"
#include "../link/protocol.h"
#include <string.h>

// ---------- transporte falso: cola de tramas de A->B o B->A ----------
#define QN 8
struct Wire {
  uint8_t buf[QN][4 + LINK_MAX_PAYLOAD + 2];
  uint16_t len[QN];
  int n = 0;
};
static void wireSend(void *ud, const uint8_t *f, uint16_t n) {
  Wire *w = (Wire *)ud;
  if (w->n < QN) {
    memcpy(w->buf[w->n], f, n);
    w->len[w->n] = n;
    w->n++;
  }
}
static LkEvent pump(Wire &w, LinkSM &sm, uint32_t now) {
  LkEvent last = LKE_NONE;
  for (int i = 0; i < w.n; i++) last = sm.feed(w.buf[i], w.len[i], now);
  w.n = 0;
  return last;
}

static PetRecord mkPet(int species, const char *nick, uint8_t bond) {
  PetRecord r;
  r.used = true;
  r.speciesId = (int16_t)species;
  r.eggTarget = (int16_t)species;
  strncpy(r.nick, nick, 11);
  r.nick[11] = '\0';
  r.bond = bond;
  r.fullness = 55; r.joy = 66; r.energy = 77; r.hygiene = 88;
  r.poops = 2; r.weight = 33;
  r.geneAtk = 95; r.geneDef = 105; r.geneSpe = 100;
  r.trAtk = 12; r.trDef = 34; r.trSpe = 56;
  r.shiny = true; r.berryKnown = true;
  r.ageMinutes = 987654u;
  r.medals = 0x123;
  return r;
}

// ---------- enmarcado ----------
TEST(linkframe, crc16_ccitt_check_oficial) {
  CHECK_EQ(linkCrc16((const uint8_t *)"123456789", 9), (uint16_t)0x29B1);
}

TEST(linkframe, ida_y_vuelta_y_rechazos) {
  const uint8_t pay[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  uint8_t f[64];
  LinkMsg m;
  int n = linkFrame(LT_TRADE_OFFER, pay, 10, f, sizeof f);
  CHECK_EQ(n, 16);
  CHECK_EQ(linkParse(f, (uint16_t)n, &m), 0);
  CHECK_EQ(m.type, (uint16_t)LT_TRADE_OFFER);
  CHECK_EQ(m.len, (uint16_t)10);
  CHECK(memcmp(m.pay, pay, 10) == 0);
  CHECK_MSG(linkParse(f, (uint16_t)(n - 1), &m) == -1, "trunca +1");
  f[5] ^= 0x01;
  CHECK_MSG(linkParse(f, (uint16_t)n, &m) == -1, "corrupta");
  CHECK_EQ(linkFrame(LT_HELLO, nullptr, 0, f, sizeof f), 6);  // payload vacio
  CHECK_MSG(linkFrame(LT_HELLO, pay, 129, f, sizeof f) == -1, "payload >128");
  CHECK_MSG(linkFrame(LT_HELLO, pay, 10, f, 15) == -1, "no cabe en cap");
}

// ---------- registro de mascota ----------
TEST(linkpet, ida_y_vuelta_fiel) {
  PetRecord a = mkPet(25, "PIKACHU", 42);
  uint8_t buf[LINK_PET_BYTES];
  lkPetEncode(a, buf);
  PetRecord b;
  CHECK(lkPetDecode(buf, sizeof buf, &b));
  CHECK_EQ(b.speciesId, (int16_t)25);
  CHECK(strcmp(b.nick, "PIKACHU") == 0);
  CHECK_EQ(b.bond, (uint8_t)42);
  CHECK_EQ(b.ageMinutes, (uint32_t)987654);
  CHECK_EQ(b.medals, (uint16_t)0x123);
  CHECK(b.shiny && b.berryKnown);
  CHECK_EQ(lkPetCrc(a), lkPetCrc(b));  // el crc sobrevivio al viaje
  CHECK(!lkPetDecode(buf, LINK_PET_BYTES - 1, &b));
}

TEST(linkpet, valores_raros_rechazados) {
  PetRecord a = mkPet(151, "", 0);  // bordes: Mew y nickname vacio
  uint8_t buf[LINK_PET_BYTES];
  lkPetEncode(a, buf);
  PetRecord b;
  CHECK(lkPetDecode(buf, sizeof buf, &b));
  CHECK_EQ(b.speciesId, (int16_t)151);
  CHECK(strcmp(b.nick, "") == 0);
  CHECK(b.used);
  buf[28] = 200;  // Pokedex #200 no existe
  CHECK_MSG(!lkPetDecode(buf, sizeof buf, &b), "fuera de dex");
}

// ---------- DEX_SNAP ----------
TEST(linkdex, bitmap_y_conteos) {
  uint8_t bmp[19] = {0};
  bmp[0] = 0x01;              // especie 1
  bmp[18] = 0x40;             // especie 151 (bit 150)
  CHECK_EQ(lkBmpCount(bmp), (uint16_t)2);
  uint8_t snap[LINK_DEXSNAP_BYTES];
  lkDexSnap(bmp, 1, snap);
  CHECK(memcmp(snap, bmp, 19) == 0);
  CHECK_EQ(snap[19], (uint8_t)2);  // criados LE
  CHECK_EQ(snap[21], (uint8_t)1);  // shinies LE
}

// ---------- maquina de canje (A inicia, B espera) ----------
static void makePair(LinkSM &A, LinkSM &B, Wire &a2b, Wire &b2a) {
  static const uint8_t idA[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  static const uint8_t idB[8] = {2, 2, 2, 2, 2, 2, 2, 2};
  a2b.n = b2a.n = 0;
  A.begin(idA, "POK-ROJO", wireSend, &a2b);
  B.begin(idB, "POK-AZUL", wireSend, &b2a);
}

TEST(linktrade, canje_completo_A_B) {
  LinkSM A, B;
  Wire a2b, b2a;
  makePair(A, B, a2b, b2a);
  PetRecord ga = mkPet(1, "BULBA", 30), gb = mkPet(4, "CHAR", 40);
  A.setGivePet(&ga);
  B.setGivePet(&gb);
  CHECK(A.startTrade(0xDEADBEEF, 1000));
  CHECK(B.startWaiting(1000));
  pump(a2b, B, 1000);                    // HELLO -> B
  CHECK_MSG(pump(b2a, A, 1000) == LKE_PEER, "A handshake");  // ACK + OFFER auto
  CHECK_MSG(pump(a2b, B, 1100) == LKE_OFFER_IN, "B ve la oferta");
  CHECK_EQ(B.received().speciesId, (int16_t)1);  // prompt: "BULBA de POK-ROJO"
  CHECK(strcmp(B.peerName(), "POK-ROJO") == 0);
  CHECK(B.accept(1200));                 // toque en Aceptar (2-way)
  CHECK_MSG(pump(b2a, A, 1200) == LKE_DONE, "A cierra");
  CHECK_MSG(pump(a2b, B, 1200) == LKE_DONE, "B cierra");
  CHECK_EQ(A.received().speciesId, (int16_t)4);   // A recibo lo de B
  CHECK_EQ(B.received().speciesId, (int16_t)1);   // B recibo lo de A
  CHECK_EQ((int)A.state(), (int)LK_IDLE);
  CHECK_EQ((int)B.state(), (int)LK_IDLE);
}

TEST(linktrade, rechazo_limpio) {
  LinkSM A, B;
  Wire a2b, b2a;
  makePair(A, B, a2b, b2a);
  PetRecord ga = mkPet(7, "SQUIRT", 10), gb = mkPet(9, "BLAST", 11);
  A.setGivePet(&ga);
  B.setGivePet(&gb);
  A.startTrade(0x42, 1000);
  B.startWaiting(1000);
  pump(a2b, B, 1000);
  pump(b2a, A, 1000);
  CHECK_MSG(pump(a2b, B, 1000) == LKE_OFFER_IN, "llego la oferta");
  B.reject(1100);                        // el usuario de B pulsa Rechazar
  CHECK_MSG(pump(b2a, A, 1100) == LKE_REJECTED, "A ve el rechazo");
  CHECK_EQ((int)A.state(), (int)LK_IDLE);
  CHECK_EQ((int)B.state(), (int)LK_IDLE);
}

TEST(linktrade, timeout_de_30s_cancela) {
  LinkSM A, B;
  Wire a2b, b2a;
  makePair(A, B, a2b, b2a);
  PetRecord ga = mkPet(16, "NIDO", 1);
  A.setGivePet(&ga);
  A.startTrade(0x99, 0);
  CHECK_EQ((int)A.tick(LINK_TIMEOUT_MS - 1), (int)LKE_NONE);
  CHECK_MSG(A.tick(LINK_TIMEOUT_MS) == LKE_TIMEOUT, "30s sin respuesta");
  CHECK_EQ((int)A.state(), (int)LK_IDLE);
  B.startWaiting(0);                     // B estaba esperando: ve el CANCEL
  CHECK_MSG(pump(a2b, B, LINK_TIMEOUT_MS) == LKE_PEER_CANCEL, "B cancelado");
  CHECK_EQ((int)B.state(), (int)LK_IDLE);
}

TEST(linktrade, oferta_corrupta_aborta) {
  LinkSM A, B;
  Wire a2b, b2a;
  makePair(A, B, a2b, b2a);
  PetRecord ga = mkPet(25, "PIKA", 1);
  A.setGivePet(&ga);
  A.startTrade(0x77, 0);
  B.startWaiting(0);
  pump(a2b, B, 0);
  pump(b2a, A, 0);                       // OFFER en a2b
  int li = a2b.n - 1;                    // ultima trama = TRADE_OFFER
  a2b.buf[li][7] ^= 0x80;                // corromper el registro
  CHECK_MSG(B.feed(a2b.buf[li], a2b.len[li], 0) == LKE_BAD, "CRC no cuadra");
  CHECK_EQ((int)B.state(), (int)LK_IDLE);
  CHECK_MSG(pump(a2b, A, 0) == LKE_PEER_CANCEL, "A ve el CANCEL");
}

TEST(linktrade, version_incompatible_rechazada) {
  LinkSM B;
  Wire b2a;
  b2a.n = 0;
  static const uint8_t idB[8] = {2, 2, 2, 2, 2, 2, 2, 2};
  B.begin(idB, "POK-AZUL", wireSend, &b2a);
  uint8_t pay[25] = {0};
  pay[0] = LINK_PROTO_VER + 8;           // version rara
  pay[1] = 9;                            // id del "peer"
  uint8_t f[64];
  int n = linkFrame(LT_HELLO, pay, 25, f, sizeof f);
  CHECK(n > 0);
  B.startWaiting(0);
  CHECK_MSG(B.feed(f, (uint16_t)n, 0) == LKE_BAD, "version distinta = fuera");
}

// ---------- maquina de traslado (A regala/migra, B recibe) ----------
TEST(linktransfer, traslado_con_migracion_completa) {
  LinkSM A, B;
  Wire a2b, b2a;
  makePair(A, B, a2b, b2a);
  PetRecord ga = mkPet(150, "M2", 55);
  DevState dev;
  dev.streak = 42;
  dev.lastSeenEpoch = 1700000000u;
  dev.dexReg[0] = 0x01;
  dev.dexShinyReg[18] = 0x40;
  A.setGivePet(&ga, &dev);               // migracion: registro + dex del bicho...
  CHECK(A.startTransfer(0x1234, 0));
  CHECK(B.startWaiting(0));
  pump(a2b, B, 0);
  pump(b2a, A, 0);                       // A suelta el OFFER completo
  CHECK_MSG(pump(a2b, B, 0) == LKE_OFFER_IN, "oferta de traslado");
  CHECK_EQ(B.received().speciesId, (int16_t)150);
  CHECK(B.receivedHasDev());             // ...mas la meta del dispositivo
  CHECK(B.accept(0));                    // B acepta (el host haria Party::add
  CHECK_MSG(pump(b2a, A, 0) == LKE_DONE, "A libera tras el COMMIT de B");
  CHECK_EQ((int)A.state(), (int)LK_IDLE);
  CHECK_EQ(B.receivedDev().streak, (uint16_t)42);
  CHECK_EQ(B.receivedDev().lastSeenEpoch, (uint32_t)1700000000);
  CHECK_EQ(B.receivedDev().dexReg[0], (uint8_t)0x01);
  CHECK_EQ(B.receivedDev().dexShinyReg[18], (uint8_t)0x40);
  CHECK_EQ(B.received().bond, (uint8_t)55);
}

TEST(linktransfer, sin_dados_no_hay_transfer_ok) {
  // A sin mascota preparada: no hay OFFER; la sesion muere por timeout, no
  // se pierde nada (A no ha liberado copia hasta ver el COMMIT de B)
  LinkSM A, B;
  Wire a2b, b2a;
  makePair(A, B, a2b, b2a);
  A.startTransfer(0x55, 0);
  B.startWaiting(0);
  pump(a2b, B, 0);
  pump(b2a, A, 0);                       // handshake ok pero A no tiene give
  CHECK_EQ((int)B.state(), (int)LK_RESP);  // B esperando oferta que no llega
  CHECK(A.tick(LINK_TIMEOUT_MS) == LKE_TIMEOUT);
}
