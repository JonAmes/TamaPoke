// Implementacion del nucleo de protocolo P2P (ver link/protocol.h). Logica
// pura: sin BLE, sin NVS, sin millis(): el reloj y el transporte entran por
// parametro, asi que todo esto se prueba determinista en host.
#include "link/protocol.h"
#include "dex.h"  // DEX_COUNT para validar registros recibidos
#include <string.h>

// ---------- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, sin reflejar) ----------
uint16_t linkCrc16(const uint8_t *b, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)b[i] << 8;
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// ---------- enmarcado: [2B tipo LE][2B len LE][payload][2B CRC16 LE] ----------
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int linkFrame(uint16_t type, const uint8_t *pay, uint16_t len, uint8_t *out, uint16_t cap) {
  if (len > LINK_MAX_PAYLOAD || (pay == nullptr) != (len == 0)) return -1;
  uint16_t total = 4 + len + 2;
  if (cap < total) return -1;
  wr16(out, type);
  wr16(out + 2, len);
  if (len) memcpy(out + 4, pay, len);
  wr16(out + 4 + len, linkCrc16(out, 4 + len));
  return total;
}

int linkParse(const uint8_t *buf, uint16_t total, LinkMsg *m) {
  if (total < 6) return -1;
  uint16_t len = rd16(buf + 2);
  if (len > LINK_MAX_PAYLOAD || (uint32_t)len + 6 != (uint32_t)total) return -1;
  if (rd16(buf + 4 + len) != linkCrc16(buf, (size_t)4 + len)) return -1;
  m->type = rd16(buf);
  m->pay = buf + 4;
  m->len = len;
  return 0;
}

// ---------- registro de mascota (38B fijos, poco endian) ----------
void lkPetEncode(const PetRecord &r, uint8_t *out) {
  out[0] = r.fullness; out[1] = r.joy; out[2] = r.energy; out[3] = r.hygiene;
  out[4] = r.poops; out[5] = r.weight;
  out[6] = r.geneAtk; out[7] = r.geneDef; out[8] = r.geneSpe;
  out[9] = r.trAtk; out[10] = r.trDef; out[11] = r.trSpe;
  out[12] = r.careMistakes; out[13] = r.bond; out[14] = r.eggTaps;
  out[15] = (uint8_t)((r.berryKnown ? 1 : 0) | (r.shiny ? 2 : 0) | (r.sleeping ? 4 : 0) |
                      (r.eggShiny ? 8 : 0) | (r.starterPick ? 16 : 0));
  memset(out + 16, 0, 12);
  memcpy(out + 16, r.nick, 12);  // nick[] es de 12 y el origen puede ir sin NUL
  wr16(out + 28, (uint16_t)r.speciesId);
  wr16(out + 30, (uint16_t)r.eggTarget);
  wr16(out + 32, r.medals);
  wr32(out + 34, r.ageMinutes);
}

bool lkPetDecode(const uint8_t *in, size_t n, PetRecord *r) {
  if (n < LINK_PET_BYTES || r == nullptr) return false;
  PetRecord out;
  out.used = true;
  out.fullness = in[0]; out.joy = in[1]; out.energy = in[2]; out.hygiene = in[3];
  out.poops = in[4]; out.weight = in[5];
  out.geneAtk = in[6]; out.geneDef = in[7]; out.geneSpe = in[8];
  out.trAtk = in[9]; out.trDef = in[10]; out.trSpe = in[11];
  out.careMistakes = in[12]; out.bond = in[13]; out.eggTaps = in[14];
  out.berryKnown = (in[15] & 1) != 0; out.shiny = (in[15] & 2) != 0;
  out.sleeping = (in[15] & 4) != 0; out.eggShiny = (in[15] & 8) != 0;
  out.starterPick = (in[15] & 16) != 0;
  memcpy(out.nick, in + 16, 12);
  out.nick[11] = '\0';  // sea cual sea el origen, siempre acaba en NUL
  out.speciesId = (int16_t)rd16(in + 28);
  out.eggTarget = (int16_t)rd16(in + 30);
  out.medals = rd16(in + 32);
  out.ageMinutes = rd32(in + 34);
  if (out.speciesId < -1 || out.speciesId > DEX_COUNT) return false;
  if (out.eggTarget < 1 || out.eggTarget > DEX_COUNT) return false;
  *r = out;
  return true;
}

uint16_t lkPetCrc(const PetRecord &r) {
  uint8_t buf[LINK_PET_BYTES];
  lkPetEncode(r, buf);
  return linkCrc16(buf, sizeof buf);
}

// ---------- subset de DevState para migracion (44B) ----------
void lkDevEncode(const DevState &d, uint8_t *out) {
  memcpy(out, d.dexReg, 19);
  memcpy(out + 19, d.dexShinyReg, 19);
  wr16(out + 38, d.streak);
  wr32(out + 40, d.lastSeenEpoch);
}

bool lkDevDecode(const uint8_t *in, size_t n, DevState *d) {
  if (n < LINK_DEV_BYTES || d == nullptr) return false;
  memcpy(d->dexReg, in, 19);
  memcpy(d->dexShinyReg, in + 19, 19);
  d->streak = rd16(in + 38);
  d->lastSeenEpoch = rd32(in + 40);
  return true;
}

// ---------- DEX_SNAP ----------
uint16_t lkBmpCount(const uint8_t bmp[19]) {
  uint16_t n = 0;
  for (int i = 0; i < 19; i++)
    for (uint8_t b = bmp[i]; b; b = (uint8_t)(b >> 1)) n += (uint8_t)(b & 1);
  return n;
}

void lkDexSnap(const uint8_t dexReg[19], uint16_t shiniesOwned, uint8_t *out) {
  memcpy(out, dexReg, 19);
  wr16(out + 19, lkBmpCount(dexReg));
  wr16(out + 21, shiniesOwned);
}

// ---------- maquina de estado ----------
void LinkSM::begin(const uint8_t id[LINK_ID_LEN], const char *name, LkSendFn send, void *ud) {
  m_send = send;
  m_ud = ud;
  memcpy(m_id, id, LINK_ID_LEN);
  strncpy(m_name, name, LINK_NAME_MAX);
  m_name[LINK_NAME_MAX] = '\0';
}

void LinkSM::sendFrame(uint16_t type, const uint8_t *pay, uint16_t len) {
  uint8_t buf[4 + LINK_MAX_PAYLOAD + 2];
  int n = linkFrame(type, pay, len, buf, sizeof buf);
  if (n > 0 && m_send) m_send(m_ud, buf, (uint16_t)n);
}

void LinkSM::helloPayload(uint8_t *out) const {
  out[0] = LINK_PROTO_VER;
  memcpy(out + 1, m_id, LINK_ID_LEN);
  memset(out + 1 + LINK_ID_LEN, 0, LINK_NAME_MAX);
  memcpy(out + 1 + LINK_ID_LEN, m_name, strlen(m_name));
}

bool LinkSM::startTrade(uint32_t nonce, uint32_t nowMs) {
  if (m_st != LK_IDLE) return false;
  m_kind = LK_TRADE; m_init = true; m_nonce = nonce;
  m_st = LK_HELLO; m_deadline = nowMs + LINK_TIMEOUT_MS;
  uint8_t p[1 + LINK_ID_LEN + LINK_NAME_MAX];
  helloPayload(p);
  sendFrame(LT_HELLO, p, sizeof p);
  return true;
}

bool LinkSM::startTransfer(uint32_t nonce, uint32_t nowMs) {
  if (m_st != LK_IDLE) return false;
  m_kind = LK_TRANSFER; m_init = true; m_nonce = nonce;
  m_st = LK_HELLO; m_deadline = nowMs + LINK_TIMEOUT_MS;
  uint8_t p[1 + LINK_ID_LEN + LINK_NAME_MAX];
  helloPayload(p);
  sendFrame(LT_HELLO, p, sizeof p);
  return true;
}

bool LinkSM::startWaiting(uint32_t nowMs) {
  if (m_st != LK_IDLE) return false;
  m_init = false;
  m_st = LK_HELLO; m_deadline = nowMs + LINK_TIMEOUT_MS;
  return true;
}

void LinkSM::setGivePet(const PetRecord *r, const DevState *dev) {
  if (r == nullptr) { m_haveGive = false; return; }
  m_give = *r;
  m_haveGive = true;
  m_giveCrc = lkPetCrc(m_give);
  m_giveDev = (dev != nullptr);
  if (dev) m_gdev = *dev;
}

void LinkSM::abort() {
  if (m_st != LK_IDLE) sendFrame(LT_CANCEL, nullptr, 0);
  m_st = LK_IDLE; m_kind = LK_NONE; m_haveGive = false; m_rdevOk = false;
}

LkEvent LinkSM::feed(const uint8_t *frame, uint16_t len, uint32_t nowMs) {
  LinkMsg m;
  if (linkParse(frame, len, &m) != 0) {  // trama corrupta: no hay negotiation
    if (m_st != LK_IDLE) abort();
    return LKE_BAD;
  }
  m_deadline = nowMs + LINK_TIMEOUT_MS;  // cada paso valido reinicia los 30 s

  if (m.type == LT_CANCEL) {
    m_st = LK_IDLE; m_kind = LK_NONE;
    return LKE_PEER_CANCEL;
  }
  if (m.type == LT_REJECT) {  // "Rechazar" tocado en B mientras A espera
    if (m_st == LK_RESP && m_init) {
      m_st = LK_IDLE; m_kind = LK_NONE; m_haveGive = false;
      return LKE_REJECTED;
    }
    return LKE_NONE;
  }

  switch (m_st) {
  case LK_HELLO: {
    if (m_init) {
      if (m.type != LT_HELLO_ACK) return LKE_NONE;
      if (!handleHello(m, true)) { abort(); return LKE_BAD; }
      m_st = LK_RESP;  // a la espera de la respuesta a nuestra oferta
      // oferta ahora si hay mascota preparada
      if (m_haveGive) {
        uint8_t p[LINK_PET_BYTES + LINK_DEV_BYTES + 4 + 2];
        uint16_t n = 0;
        lkPetEncode(m_give, p); n = LINK_PET_BYTES;
        if (m_kind == LK_TRANSFER && m_giveDev) { lkDevEncode(m_gdev, p + n); n = (uint16_t)(n + LINK_DEV_BYTES); }
        wr32(p + n, m_nonce); n = (uint16_t)(n + 4);
        wr16(p + n, linkCrc16(p, n)); n = (uint16_t)(n + 2);  // CRC sobre registro(+dev)+nonce
        sendFrame(m_kind == LK_TRANSFER ? LT_TRANSFER_OFFER : LT_TRADE_OFFER, p, n);
      }
      return LKE_PEER;
    }
    if (m.type != LT_HELLO) return LKE_NONE;
    if (!handleHello(m, false)) { abort(); return LKE_BAD; }
    m_nonce = 0;  // el nonce de sesion lo pone el iniciador en su OFFER
    uint8_t p[1 + LINK_ID_LEN + LINK_NAME_MAX];
    helloPayload(p);
    sendFrame(LT_HELLO_ACK, p, sizeof p);
    m_st = LK_RESP;  // esperamos OFFER
    return LKE_PEER;
  }

  case LK_RESP: {
    if (m_init) {
      // A: espera TRADE_RESP{acepta, nonce, mi registro} o REJECT; luego
      // manda TRADE_COMMIT con ambos CRC. En transfer espera RESP y COMMIT.
      if (m.type == LT_TRADE_RESP && m_kind == LK_TRADE) {
        if (m.len != 1 + 4 + LINK_PET_BYTES) { abort(); return LKE_BAD; }
        if (rd32(m.pay + 1) != m_nonce) { abort(); return LKE_BAD; }
        if (!m.pay[0]) { m_st = LK_IDLE; m_kind = LK_NONE; return LKE_REJECTED; }
        if (!lkPetDecode(m.pay + 5, LINK_PET_BYTES, &m_recv)) { abort(); return LKE_BAD; }
        m_recvCrc = lkPetCrc(m_recv);
        uint8_t c[8];
        wr16(c, m_giveCrc); wr16(c + 2, m_recvCrc); wr32(c + 4, m_nonce);
        sendFrame(LT_TRADE_COMMIT, c, 8);
        m_st = LK_COMMIT;
        return LKE_NONE;
      }
      if (m.type == LT_TRANSFER_RESP && m_kind == LK_TRANSFER) {
        if (m.len != 5 || rd32(m.pay + 1) != m_nonce) { abort(); return LKE_BAD; }
        if (!m.pay[0]) { m_st = LK_IDLE; m_kind = LK_NONE; return LKE_REJECTED; }
        m_st = LK_COMMIT;  // B manda el COMMIT; A libera al recibirlo
        return LKE_NONE;
      }
      return LKE_NONE;
    }
    // B (periferico): recibe la OFFER; el tipo de sesion lo fija la propia
    // trama (B nunca eligio trade vs transfer). CRC obliga, luego la UI.
    if (m.type == LT_TRADE_OFFER) m_kind = LK_TRADE;
    else if (m.type == LT_TRANSFER_OFFER) m_kind = LK_TRANSFER;
    else return LKE_NONE;
    size_t recSpan = LINK_PET_BYTES + (m_kind == LK_TRANSFER ? LINK_DEV_BYTES : 0);
    if (m.len != (uint16_t)(recSpan + 4 + 2)) { abort(); return LKE_BAD; }
    if (rd16(m.pay + recSpan + 4) != linkCrc16(m.pay, recSpan + 4)) { abort(); return LKE_BAD; }
    m_nonce = rd32(m.pay + recSpan);  // el nonce de sesion viene en la OFFER
    if (!lkPetDecode(m.pay, LINK_PET_BYTES, &m_recv)) { abort(); return LKE_BAD; }
    m_recvCrc = lkPetCrc(m_recv);
    m_rdevOk = false;
    if (m_kind == LK_TRANSFER) m_rdevOk = lkDevDecode(m.pay + LINK_PET_BYTES, LINK_DEV_BYTES, &m_rdev);
    m_st = LK_PROMPT;
    return LKE_OFFER_IN;
  }

  case LK_PROMPT: {  // B espera la segunda confirmacion (la nuestra) o timeout
    return LKE_NONE;
  }

  case LK_COMMIT: {
    if (m_kind == LK_TRADE && m.type == LT_TRADE_COMMIT) {
      // ambos lados mandan el mismo COMMIT{crcA,crcB,nonce}: cada uno lo
      // verifica contra lo suyo y al recibir el del otro declara DONE
      if (m.len != 8 || rd32(m.pay + 4) != m_nonce) { abort(); return LKE_BAD; }
      if (m_init) {
        if (rd16(m.pay) != m_giveCrc || rd16(m.pay + 2) != m_recvCrc) { abort(); return LKE_BAD; }
      } else {
        if (rd16(m.pay) != m_recvCrc || rd16(m.pay + 2) != m_giveCrc) { abort(); return LKE_BAD; }
      }
      m_st = LK_IDLE; m_kind = LK_NONE; m_haveGive = false;
      return LKE_DONE;
    }
    if (m_init && m_kind == LK_TRANSFER && m.type == LT_TRANSFER_COMMIT) {
      // A en transfer: COMMIT de B = ok mutuo, libero mi copia
      if (m.len != 6 || rd16(m.pay) != m_giveCrc || rd32(m.pay + 2) != m_nonce) { abort(); return LKE_BAD; }
      m_st = LK_IDLE; m_kind = LK_NONE; m_haveGive = false;
      return LKE_DONE;
    }
    return LKE_NONE;
  }

  default:
    return LKE_NONE;
  }
}

bool LinkSM::handleHello(const LinkMsg &m, bool ack) {
  (void)ack;
  if (m.len != 1 + LINK_ID_LEN + LINK_NAME_MAX || m.pay[0] != LINK_PROTO_VER) return false;
  memcpy(m_peerId, m.pay + 1, LINK_ID_LEN);
  memcpy(m_peerName, m.pay + 1 + LINK_ID_LEN, LINK_NAME_MAX);
  m_peerName[LINK_NAME_MAX] = '\0';
  if (memcmp(m_peerId, m_id, LINK_ID_LEN) == 0) return false;  // conversacion contra mi mismo
  return true;
}

bool LinkSM::accept(uint32_t nowMs) {
  if (m_st != LK_PROMPT) return false;
  if (m_kind == LK_TRADE) {
    if (!m_haveGive) return false;  // B tambien pone mascota en el canje
    uint8_t p[1 + 4 + LINK_PET_BYTES];
    p[0] = 1;
    wr32(p + 1, m_nonce);  // eco del nonce del iniciador
    lkPetEncode(m_give, p + 5);
    sendFrame(LT_TRADE_RESP, p, sizeof p);
    uint8_t c[8];
    wr16(c, m_recvCrc); wr16(c + 2, m_giveCrc); wr32(c + 4, m_nonce);
    sendFrame(LT_TRADE_COMMIT, c, 8);
    m_st = LK_COMMIT;
    m_deadline = nowMs + LINK_TIMEOUT_MS;
    return true;
  }
  // transfer: B acepta, manda RESP + COMMIT (es su copia la que se guarda)
  uint8_t r[5];
  r[0] = 1;
  wr32(r + 1, m_nonce);
  sendFrame(LT_TRANSFER_RESP, r, 5);
  uint8_t c[6];
  wr16(c, m_recvCrc); wr32(c + 2, m_nonce);
  sendFrame(LT_TRANSFER_COMMIT, c, 6);
  m_st = LK_IDLE; m_kind = LK_NONE;
  return true;
}

void LinkSM::reject(uint32_t nowMs) {
  (void)nowMs;
  if (m_st != LK_PROMPT) return;
  sendFrame(LT_REJECT, nullptr, 0);
  m_st = LK_IDLE; m_kind = LK_NONE; m_haveGive = false;
}

LkEvent LinkSM::tick(uint32_t nowMs) {
  if (m_st == LK_IDLE) return LKE_NONE;
  if ((int32_t)(nowMs - m_deadline) < 0) return LKE_NONE;
  abort();
  return LKE_TIMEOUT;
}
