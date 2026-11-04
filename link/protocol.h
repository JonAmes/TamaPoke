// F27 — nucleo del protocolo P2P (trade + transfer), logica pura: enmarcado
// [2B tipo][2B len][payload][2B CRC16], codificador de registros de mascota y
// maquinas de estado de canje y traslado. Todo host-testeable; el transporte
// real (BLE en P4 item 4) solo mueve tramas. Diseno: DOCS/plan.md §5c/§5d.
#ifndef LINK_PROTOCOL_H
#define LINK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "../pet.h"  // PetRecord + DevState (ambos structs puros)

#define LINK_PROTO_VER   1
#define LINK_ID_LEN      8     // DEVICE_ID: 8B persistentes (aleatorio 1er boot)
#define LINK_NAME_MAX    16    // DEVICE_NAME <=16 chars
#define LINK_MAX_PAYLOAD 128   // registro completo (88B) con margen: 1 MTU BLE
#define LINK_TIMEOUT_MS  30000u

// tipos de trama
enum {
  LT_HELLO          = 0x0001,  // ver + ID + nombre
  LT_HELLO_ACK      = 0x0002,
  LT_TRADE_OFFER    = 0x0010,  // registro completo + nonce + crc
  LT_TRADE_RESP     = 0x0011,  // 1B acepta + 4B nonce + mi registro (swap)
  LT_TRADE_COMMIT   = 0x0012,  // crcA(2)+crcB(2)+nonce(4)
  LT_TRANSFER_OFFER = 0x0020,  // registro + subset dev + nonce + crc (~88B)
  LT_TRANSFER_RESP  = 0x0021,  // 1B acepta + 4B nonce
  LT_TRANSFER_COMMIT= 0x0022,  // crc(2)+nonce(4)
  LT_CANCEL         = 0x0030,  // cancelacion/abandono por error o timeout
  LT_REJECT         = 0x0031,  // "Rechazar" tocado en el prompt de B
};

// ---- enmarcado (host-test) ----
uint16_t linkCrc16(const uint8_t *b, size_t n);  // CRC-16/CCITT-FALSE

typedef struct {
  uint16_t type;
  const uint8_t *pay;
  uint16_t len;
} LinkMsg;

// escribe la trama completa; devuelve tamano total o -1 si no cabe/invalida
int linkFrame(uint16_t type, const uint8_t *pay, uint16_t len, uint8_t *out, uint16_t cap);
// 0 = trama valida (CRC verificado), -1 = truncada/corrupta/incoherente
int linkParse(const uint8_t *buf, uint16_t total, LinkMsg *m);

// ---- codificador de registro de mascota (~100B del plan; 38B en seco) ----
#define LINK_PET_BYTES 38
void lkPetEncode(const PetRecord &r, uint8_t *out);
// nick siempre queda NUL-terminado y used=true al decodificar
bool lkPetDecode(const uint8_t *in, size_t n, PetRecord *r);
uint16_t lkPetCrc(const PetRecord &r);  // CRC16 del registro codificado

// subset de DevState que viaja en una TRANSFERENCIA (migracion de placa):
// dex, dex shiny, racha y ultima hora RTC. 44B.
#define LINK_DEV_BYTES 44
void lkDevEncode(const DevState &d, uint8_t *out);
bool lkDevDecode(const uint8_t *in, size_t n, DevState *d);

// ---- DEX_SNAP (lectura GATT para la app companera): 19B bitmap + conteos ----
#define LINK_DEXSNAP_BYTES 23
void lkDexSnap(const uint8_t dexReg[19], uint16_t shiniesOwned, uint8_t *out);
uint16_t lkBmpCount(const uint8_t bmp[19]);  // 151 bits poblados

// ---- maquina de estado de sesion (un canje O un traslado a la vez) ----
enum LkState { LK_IDLE = 0, LK_HELLO, LK_PROMPT, LK_RESP, LK_COMMIT };
enum LkKind { LK_NONE = 0, LK_TRADE, LK_TRANSFER };
enum LkEvent {
  LKE_NONE = 0,
  LKE_PEER,        // handshake hecho (HELLO verificado)
  LKE_OFFER_IN,    // oferta recibida y CRC-ok: la UI debe preguntar (2-way)
  LKE_REJECTED,    // el otro lado rechazo
  LKE_DONE,        // ambos aceptaron y commitearon: aplica recibida/libera dada
  LKE_PEER_CANCEL,
  LKE_TIMEOUT,     // 30 s sin paso -> CANCEL enviado
  LKE_BAD,         // CRC/nonce/estado invalido: protocolo abortado
};

typedef void (*LkSendFn)(void *ud, const uint8_t *frame, uint16_t len);

class LinkSM {
public:
  void begin(const uint8_t id[LINK_ID_LEN], const char *name, LkSendFn send, void *ud);

  // roles (BLE ESP32 es central XOR periferico): quien inicia escanea el tag
  // del peer; quien espera anuncia. nonce = entropia de sesion del iniciador.
  bool startTrade(uint32_t nonce, uint32_t nowMs);      // iniciador
  bool startWaiting(uint32_t nowMs);                     // periferico
  bool startTransfer(uint32_t nonce, uint32_t nowMs);    // iniciador (unidireccional)

  // que pongo sobre la mesa (trade) o envio (transfer); en transfer pasar
  // `dev` tambien si es migracion completa de placa.
  void setGivePet(const PetRecord *r, const DevState *dev = nullptr);

  // alimentacion: trama cruda recibida del transporte + reloj ms monotono
  LkEvent feed(const uint8_t *frame, uint16_t len, uint32_t nowMs);
  LkEvent tick(uint32_t nowMs);  // deadline de 30 s por paso

  // decisiones de la UI tras LKE_OFFER_IN (aceptar = 2-way obligatorio;
  // party lleno en B -> rechazar, nunca sobrescribir)
  bool accept(uint32_t nowMs);
  void reject(uint32_t nowMs);

  // resultados para la capa de aplicacion (commit = escribir esto + dex)
  const PetRecord &received() const { return m_recv; }
  const DevState &receivedDev() const { return m_rdev; }  // solo transfer
  bool receivedHasDev() const { return m_rdevOk; }
  const uint8_t *peerId() const { return m_peerId; }
  const char *peerName() const { return m_peerName; }  // para el prompt
  LkState state() const { return m_st; }
  LkKind kind() const { return m_kind; }

private:
  void sendFrame(uint16_t type, const uint8_t *pay, uint16_t len);
  void helloPayload(uint8_t *out) const;
  bool handleHello(const LinkMsg &m, bool ack);
  void abort();  // envia CANCEL y vuelve a idle

  LkSendFn m_send = nullptr;
  void *m_ud = nullptr;
  uint8_t m_id[LINK_ID_LEN] = {0};
  char m_name[LINK_NAME_MAX + 1] = {0};

  LkState m_st = LK_IDLE;
  LkKind m_kind = LK_NONE;
  bool m_init = false;
  uint32_t m_nonce = 0;
  uint32_t m_deadline = 0;
  uint8_t m_peerId[LINK_ID_LEN] = {0};
  char m_peerName[LINK_NAME_MAX + 1] = {0};

  bool m_haveGive = false;
  PetRecord m_give;
  bool m_giveDev = false;
  DevState m_gdev;
  uint16_t m_giveCrc = 0;

  PetRecord m_recv;
  DevState m_rdev;
  bool m_rdevOk = false;
  uint16_t m_recvCrc = 0;
};

#endif
