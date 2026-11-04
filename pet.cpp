#include "pet.h"
#include "dex.h"
#include "audio.h"

void Pet::begin() {
  prefs.begin("tamapoke", false);
  if (!prefs.getBool("init", false)) {
    prefs.putBool("init", true);
    newEgg();
  } else {
    load();
  }
  lastTick = millis();
}

void Pet::newEgg() {
  ceremony = CER_NONE;
  neglectTicks = 0;
  weight = 0;
  speciesId = -1;
  prevSpeciesId = -1;
  eggTarget = pickEggSpecies();  // especie oculta segun rareza y pokedex
  starterPick = (registeredCount() == 0);  // primera partida: el jugador elige inicial
  // sorteo shiny: 1/48 base, mejor con despedida y con racha/vinculo altos
  int shinyBase = (dev.lastEnd == CER_FAREWELL ? 24 : 48) - careBonus();
  if (shinyBase < 8) shinyBase = 8;
  eggShiny = (random(shinyBase) == 0);
  eggTaps = 0;
  fullness = 80;
  joy = 80;
  energy = 80;
  hygiene = 100;
  poops = 0;
  ageMinutes = 0;
  careMistakes = 0;
  mistakeCooldown = 0;
  sleeping = false;
  save();
}

// progresion offline: el tiempo paso aunque estuviera apagado, pero con
// piedad — las barras bajan con suelo en 15 (vuelve hambriento, no muerto),
// sin descuidos ni escapadas en ausencia
static uint8_t dropTo(uint8_t v, uint8_t d, uint8_t fl) {
  if (v <= fl) return v;
  return (v - fl > d) ? v - d : fl;
}

void Pet::setClock(uint32_t nowEpoch) {
  dev.lastSeenEpoch = nowEpoch;
  if (nowEpoch) save();  // persiste ya: un corte de luz no pierde la referencia
}

void Pet::syncClock(uint32_t nowEpoch) {
  uint32_t seen = prefs.getUInt("seen", 0);
  dev.lastSeenEpoch = nowEpoch;
  if (nowEpoch == 0) return;
  uint32_t mins = (seen && nowEpoch > seen) ? (nowEpoch - seen) / 60 : 0;
  if (mins < 2 || ceremony != CER_NONE || starterPick) {
    save();  // primera vez, sin tiempo que aplicar o aun eligiendo inicial
    return;
  }
  if (mins > 14UL * 24 * 60) mins = 14UL * 24 * 60;  // tope: 2 semanas

  for (uint32_t i = 0; i < mins; i++) {
    ageMinutes++;
    if (isEgg()) {
      if (ageMinutes >= 3) hatch();  // eclosiona en tu ausencia
      continue;
    }
    if (sleeping) {  // descanso: baja lento y con suelo, igual que en vivo
      energy = clamp100(energy + 6);
      if (ageMinutes % 2 == 0) {
        fullness = dropTo(fullness, 1, 30);
        joy = dropTo(joy, 1, 35);
      }
      if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
      continue;
    }
    fullness = dropTo(fullness, 2, 15);
    energy = dropTo(energy, 1, 15);
    hygiene = dropTo(hygiene, 1, 15);
    joy = dropTo(joy, 1, 15);
  }
  if (!isEgg()) {
    if (!sleeping) {  // durmiendo no ensucia
      uint8_t p = poops + mins / 240;
      poops = p > 3 ? 3 : p;
    }
    // la evolucion NO se aplica offline: queda lista y la dispara el usuario
    // tocando al bicho cuando vuelve (para que vea la transformacion)
  }
  Serial.printf("offline: %u min aplicados (nv.%u)\n", mins, level());
  save();
}

void Pet::update(uint32_t nowMs) {
  // fin de ceremonia: la criatura se va y queda un huevo nuevo
  if (ceremony != CER_NONE && !timeLeft(ceremonyUntil)) {
    newEgg();
    return;
  }
  while (nowMs - lastTick >= PET_TICK_MS) {
    lastTick += PET_TICK_MS;
    tick();
  }
}

void Pet::tick() {
  if (ceremony != CER_NONE) return;  // el tiempo se detiene en la despedida
  if (starterPick) return;  // la partida no empieza hasta elegir inicial: si el
                            // tiempo corriera aqui, el huevo eclosionaria solo a
                            // los 3 min con la especie sorteada y se perderia la
                            // eleccion del jugador
  ageMinutes++;

  if (isEgg()) {
    if (ageMinutes >= 3) hatch();  // si no lo tocas, eclosiona solo a los 3 min
    return;
  }

  // el sueño es descanso: la energia se recupera y las necesidades bajan MUCHO
  // mas lento que despierto y con suelo (amanece pidiendo algo de mimo, no a
  // cero, sin descuidos ni escapadas). despierto: comida -2/min, hig/joy -1/min.
  // El peso aun se quema; la racha de buen cuidado (goodTicks) queda en pausa.
  if (sleeping) {
    energy = clamp100(energy + 6);
    if (weight > 0 && ageMinutes % 3 == 0) weight--;
    if (ageMinutes % 2 == 0) {                 // ~4x mas lento que despierto
      fullness = dropTo(fullness, 1, 30);
      joy = dropTo(joy, 1, 35);
    }
    if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
    checkMedals();  // aun puede cruzar un nivel por edad mientras duerme
    if (++ticksSinceSave >= 5) pendingSave = true;
    return;
  }

  if (ageMinutes % MINUTES_PER_LEVEL == 0) sfxPlay(SFX_LEVEL);  // subio de nivel (despierto)

  fullness = clamp100(fullness - 2);
  energy = clamp100(energy - 1);
  if (fullness > 40 && poops < 3 && random(100) < 15) poops++;

  hygiene = clamp100(hygiene - 1 - 4 * poops);
  // el sobrepeso da pereza: la energia cae el doble
  if (weight > 50) energy = clamp100(energy - 1);
  if (weight > 0 && ageMinutes % 3 == 0) weight--;

  // la disciplina forja la defensa: 12 h seguidas bien cuidado = +1 DEF
  if (lowestStat() >= 40) {
    if (++goodTicks >= 720) {
      goodTicks = 0;
      if (trDef < 100) trDef++;
    }
  } else {
    goodTicks = 0;
  }

  int dJoy = -1;
  if (fullness < 30) dJoy -= 2;
  if (hygiene < 30) dJoy -= 2;
  joy = clamp100(joy + dJoy);

  // Descuido: dejar una estadistica por los suelos cuenta como error de
  // cuidado (con enfriamiento para no contar el mismo descuido cada minuto)
  if (mistakeCooldown > 0) mistakeCooldown--;
  if (lowestStat() <= 10 && mistakeCooldown == 0) {
    careMistakes++;
    mistakeCooldown = 60;
    if (bond > 1) bond--;  // el descuido enfria el vinculo, pero sin arrasarlo:
                           // a -3 cada 30 min se perdia mucho mas de lo que se
                           // podia ganar en todo un dia y el vinculo se atascaba
  }

  checkMedals();  // la evolucion la dispara el usuario (canEvolveNow + tap), no el tick

  // abandono total: con TODO a cero durante una hora queda lista para escaparse;
  // NO se va sola, la dispara el usuario con el boton (final triste, lo presencia)
  if (fullness == 0 && joy == 0 && energy == 0 && hygiene == 0) {
    if (neglectTicks < RUNAWAY_TICKS) neglectTicks++;
  } else {
    neglectTicks = 0;  // un solo cuidado la salva
  }

  // ciclo completo (forma final + 7 dias): la despedida NO salta sola; queda
  // lista (canFarewellNow) y la dispara el usuario con el boton, para que la vea

  // autoguardado periodico: NO escribir a flash aqui (corre dentro del loop,
  // mientras se anima); solo marcar y dejar que el loop lo vuelque al atenuar
  if (++ticksSinceSave >= 5) pendingSave = true;
}

// vuelca el guardado periodico pendiente (lo llama el loop en un momento sin
// animacion para que el paron de la escritura a flash no se vea)
void Pet::flushSave() {
  if (pendingSave) save();
}

// quedan miembros sin registrar en la linea evolutiva de esta base?
bool Pet::lineHasUnregistered(int16_t base) const {
  int16_t cur = base;
  for (int guard = 0; cur >= 1 && cur <= 151 && guard < 6; guard++) {
    if (!isRegistered(cur)) return true;
    if (cur == DEX_EEVEE) {
      for (int16_t b = 134; b <= 136; b++)
        if (!isRegistered(b)) return true;
      return false;
    }
    cur = DEX_TBL[cur].evolvesTo;
  }
  return false;
}

uint8_t Pet::eggRarity() const {
  return (eggTarget >= 1 && eggTarget <= 151) ? DEX_TBL[eggTarget].rarity : R_COMUN;
}

// elige la especie del huevo: tirada de rareza (mejorada por una despedida
// completa, castigada por una escapada) y sesgo hacia lineas incompletas
int16_t Pet::pickEggSpecies() {
  // primera partida: inicial clasico
  if (registeredCount() == 0) {
    return CLASSIC_DEX[random(NUM_CLASSIC_DEX)];
  }

  uint8_t tier = R_COMUN;
  if (dev.lastEnd != CER_RUNAWAY) {
    bool blessed = (dev.lastEnd == CER_FAREWELL);
    int rare = (blessed ? 45 : 27) + careBonus();
    int leg = (registeredCount() >= 25) ? (blessed ? 10 : 3) + careBonus() / 3 : 0;
    int r = random(100);
    if (r < leg) tier = R_LEGENDARIO;
    else if (r < leg + rare) tier = R_RARO;
  }

  // candidatos del tier con linea incompleta; si no hay, baja de tier;
  // si la pokedex del tier esta completa, vale cualquiera del tier
  for (int pass = 0; pass < 2; pass++) {
    for (int t = tier; t >= R_COMUN; t--) {
      int16_t cand[80];
      int n = 0;
      for (int16_t d = 1; d <= 151 && n < 80; d++) {
        if (DEX_TBL[d].rarity != t) continue;
        if (pass == 0 && !lineHasUnregistered(d)) continue;
        cand[n++] = d;
      }
      if (n > 0) return cand[random(n)];
    }
  }
  return CLASSIC_DEX[random(NUM_CLASSIC_DEX)];  // inalcanzable, por si acaso
}

void Pet::registerSpecies(int16_t dex) {
  if (dex < 1 || dex > 151) return;
  dev.dexReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  if (shiny) dev.dexShinyReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
}

// la racha y el vinculo mejoran el sorteo del huevo (0..~14)
int Pet::careBonus() const {
  int s = dev.streak > 30 ? 30 : dev.streak;
  return s / 3 + bond / 25;
}

// primer cuidado del dia: avanza la racha y afianza el vinculo
void Pet::registerCare() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint32_t d = today();
  if (d == 0 || d == dev.lastCareDay) return;  // sin reloj, o ya conto hoy
  if (dev.lastCareDay == 0 || d == dev.lastCareDay + 1) {
    dev.streak++;
  } else {
    dev.streak = 1;        // hubo un hueco de dias
    dev.lastMilestone = 0;
  }
  dev.lastCareDay = d;
  bondToday = 0;
  if (dev.streak > dev.bestStreak) dev.bestStreak = dev.streak;
  bond = clamp100(bond + 4);
  uint16_t ms = (dev.streak >= 100) ? 100 : (dev.streak >= 30) ? 30
              : (dev.streak >= 7)   ? 7   : (dev.streak >= 3)  ? 3 : 0;
  if (ms > dev.lastMilestone) {
    dev.lastMilestone = ms;
    milestoneUntil = millis() + 4500;
  }
  checkMedals();
  save();
}

void Pet::addBond(uint8_t amt) {
  if (bondToday >= 20) return;  // tope diario: el vinculo no se farmea
  bond = clamp100(bond + amt);
  bondToday += amt;
}

void Pet::checkMedals() {
  if (isEgg()) return;
  uint16_t before = medals;
  if (level() >= 10) medals |= MED_LV10;
  if (level() >= 25) medals |= MED_LV25;
  if (level() >= 50) medals |= MED_LV50;
  if (berryKnown) medals |= MED_BERRY;
  if (dev.streak >= 7) medals |= MED_STREAK7;
  if (bond >= 100) medals |= MED_BOND;
  if (DEX_TBL[speciesId].evolvesTo == 0) medals |= MED_FINAL;
  if (weight == 0 && level() >= 5 && careMistakes == 0) medals |= MED_FIT;
  if (dev.goalDone) medals |= MED_WALK;
  uint16_t gained = medals & ~before;
  if (gained) {
    for (uint16_t m = gained; m; m &= (m - 1)) dev.totalMedals++;
    newMedal = gained;
    medalUntil = millis() + 4000;
    if (!sleeping) sfxPlay(SFX_MEDAL);
    save();
  }
}

void Pet::rename(const char *name) {
  strncpy(nick, name, sizeof(nick) - 1);
  nick[sizeof(nick) - 1] = 0;
  save();
}

static uint16_t calcStat(uint8_t base, uint8_t gene, uint16_t lvl, uint8_t tr) {
  return (uint16_t)base * gene / 100 + lvl + tr;
}

uint16_t Pet::atkStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bAtk, geneAtk, level(), trAtk);
}
uint16_t Pet::defStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bDef, geneDef, level(), trDef);
}
uint16_t Pet::speStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bSpe, geneSpe, level(), trSpe);
}

uint16_t Pet::registeredCount() const {
  uint16_t n = 0;
  for (int i = 1; i <= 151; i++)
    if (isRegistered(i)) n++;
  return n;
}

// forma final que ya cumplio su ciclo (7 dias): lista para despedirse. La
// despedida la dispara el usuario con el boton (no salta sola, para que la vea)
bool Pet::canFarewellNow() const {
  return !isEgg() && !sleeping && ceremony == CER_NONE &&
         DEX_TBL[speciesId].evolvesTo == 0 && ageMinutes >= FAREWELL_AGE_MIN;
}

// abandono total durante 1h: lista para escaparse. La dispara el usuario con el
// boton (final triste); cuidarla un solo tick la salva (neglectTicks se resetea)
bool Pet::canRunawayNow() const {
  return !isEgg() && !sleeping && ceremony == CER_NONE && neglectTicks >= RUNAWAY_TICKS;
}

void Pet::startFarewell() {
  if (isEgg() || ceremony != CER_NONE) return;
  dev.lastEnd = CER_FAREWELL;
  ceremony = CER_FAREWELL;
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;  // corazones durante toda la despedida
  sfxPlay(SFX_BYE);
  save();
}

void Pet::startRunaway() {
  if (isEgg() || ceremony != CER_NONE) return;
  dev.lastEnd = CER_RUNAWAY;
  ceremony = CER_RUNAWAY;
  ceremonyUntil = millis() + CEREMONY_MS;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::release() {
  if (isEgg() || ceremony != CER_NONE) return;
  dev.lastEnd = CER_RELEASE;
  ceremony = CER_RELEASE;
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::hatch() {
  speciesId = eggTarget;
  shiny = eggShiny;
  // genes del individuo: 90-110% por stat (cada crianza es unica)
  geneAtk = 90 + random(21);
  geneDef = 90 + random(21);
  geneSpe = 90 + random(21);
  trAtk = trDef = trSpe = 0;
  berryKnown = false;
  bond = 0;          // vinculo, medallas y nombre son del individuo
  bondToday = 0;
  medals = 0;
  newMedal = 0;
  nick[0] = 0;
  registerSpecies(speciesId);  // criado = registrado en la pokedex
  checkMedals();     // por si nace ya en forma final (legendario)
  sfxPlay(SFX_HATCH);
  save();
}

// ¿se dan ya las condiciones para evolucionar? Cada descuido retrasa la
// evolucion 1 nivel, y ademas tiene que estar bien cuidado en ese momento
// (ninguna estadistica por debajo de 40). NO evoluciona sola: la dispara el
// usuario tocando al bicho (evolve()), para que vea la transformacion.
bool Pet::canEvolveNow() const {
  if (isEgg() || sleeping || ceremony != CER_NONE) return false;
  const DexEntry &d = DEX_TBL[speciesId];
  if (d.evolvesTo == 0) return false;
  return level() >= (uint16_t)d.evolveLevel + careMistakes && lowestStat() >= 40;
}

void Pet::evolve() {
  if (!canEvolveNow()) return;
  const DexEntry &d = DEX_TBL[speciesId];
  prevSpeciesId = speciesId;
  int16_t next = d.evolvesTo;
  if (speciesId == DEX_EEVEE) {
    // rama de Eevee: prefiere la evolucion que falte en la pokedex
    int16_t opts[3];
    int n = 0;
    for (int16_t b = 134; b <= 136; b++)
      if (!isRegistered(b)) opts[n++] = b;
    next = n > 0 ? opts[random(n)] : (int16_t)(134 + random(3));
  }
  speciesId = next;
  registerSpecies(speciesId);
  sfxPlay(SFX_EVOLVE);
  evolveUntil = millis() + EVOLVE_ANIM_MS;
  save();
}

void Pet::feed() {
  feedBerry(0);
}

void Pet::feedBerry(uint8_t color) {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  if (lovesBerry(color)) {
    fullness = clamp100(fullness + 35);
    joy = clamp100(joy + 10);
    heartUntil = millis() + HEART_MS;  // "le encanta!"
    berryKnown = true;                 // descubierto: se muestra en la ficha
    addBond(2);
  } else {
    fullness = clamp100(fullness + 25);
  }
  eatUntil = millis() + EAT_ANIM_MS;
  registerCare();
  save();
}

void Pet::feedCandy() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  fullness = clamp100(fullness + 10);
  joy = clamp100(joy + 12);
  weight = clamp100(weight + 12);  // las chuches pasan factura
  eatUntil = millis() + EAT_ANIM_MS;
  registerCare();
  save();
}

void Pet::playResult(uint8_t score) {
  if (ceremony != CER_NONE || isEgg()) return;
  uint8_t v = trSpe + score / 5;  // jugar entrena la velocidad
  trSpe = v > 100 ? 100 : v;
  joy = clamp100(joy + 5 + (score > 15 ? 30 : score * 2));
  energy = dropTo(energy, 10 + score / 2, 5);
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - score * 2;  // el ejercicio quema peso
  weight = burn > 0 ? burn : 0;
  if (score >= 5) heartUntil = millis() + HEART_MS;
  if (score > dev.gameHi) dev.gameHi = score;  // nuevo record
  addBond(2);
  registerCare();
  save();
}

void Pet::addSteps(uint32_t delta) {
  if (!delta) return;
  uint32_t d = today();
  if (d != dev.stepsDay) {  // cambio de dia: contador y bonus arrancan de 0
    dev.stepsDay = d;
    dev.stepsToday = 0;
    dev.walkStepsSpent = 0;
    dev.walkPtsToday = 0;
    dev.goalDone = 0;
  }
  dev.stepsToday += delta;
  dev.stepsTotal += delta;
  // la marcha entrena la VEL de quien este en foco (reservas: nada, F34),
  // 1 punto cada STEPS_PER_PT pasos con tope diario; el huevo no entrena
  if (!isEgg() && ceremony == CER_NONE) {
    uint8_t room = WALK_PT_DAY_CAP - dev.walkPtsToday;
    uint32_t free_ = dev.stepsToday - dev.walkStepsSpent;
    uint32_t g = free_ / STEPS_PER_PT;
    uint8_t gain = g > room ? room : (uint8_t)g;
    if (gain) {
      dev.walkStepsSpent += (uint32_t)gain * STEPS_PER_PT;
      dev.walkPtsToday += gain;
      uint8_t v = trSpe + gain;
      trSpe = v > 100 ? 100 : v;
      int burn = (int)weight - (int)gain * 2;  // andar tambien quema peso
      weight = burn > 0 ? burn : 0;
    }
  }
  if (!dev.goalDone && dev.stepsToday >= STEP_GOAL) {
    dev.goalDone = 1;
    joy = clamp100(joy + 10);
    addBond(3);
    heartUntil = millis() + HEART_MS;
    checkMedals();  // MED_WALK con la celebracion y sonido de siempre
  }
  pendingSave = true;  // el flush periodico persiste; el chip guarda el dia
}

// saco de entrenamiento: los golpes entrenan la fuerza. Devuelve la subida.
uint8_t Pet::trainStrength(uint16_t hits) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = hits / 4;          // ~4 golpes = 1 punto de entrenamiento
  if (gain > 18) gain = 18;         // tope por sesion: la FUE se forja a fuego lento
  uint8_t antes = trAtk;
  uint16_t v = (uint16_t)trAtk + gain;
  trAtk = v > 100 ? 100 : (uint8_t)v;
  gain = trAtk - antes;             // lo que de verdad entro: al topar en 100 la
                                    // pantalla anunciaba +18 aunque cupieran menos
  energy = dropTo(energy, 12, 5);   // cansa
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - hits / 3;  // tambien quema peso
  weight = burn > 0 ? burn : 0;
  joy = clamp100(joy + 6);
  if (hits >= 20) heartUntil = millis() + HEART_MS;
  if (hits > dev.strHi) dev.strHi = hits;   // record de golpes
  addBond(2);
  registerCare();
  save();
  return gain;
}

void Pet::play() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 25);
  energy = clamp100(energy - 10);
  fullness = clamp100(fullness - 5);
  heartUntil = millis() + HEART_MS;
  addBond(2);
  registerCare();
  save();
}

void Pet::toggleLight() {
  if (ceremony != CER_NONE) return;
  if (isEgg()) return;
  sleeping = !sleeping;
  save();
}

void Pet::clean() {
  if (ceremony != CER_NONE) return;
  poops = 0;
  hygiene = 100;
  addBond(1);
  registerCare();
  save();
}

void Pet::caress() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 5);
  heartUntil = millis() + HEART_MS;
  addBond(1);
  registerCare();
}

void Pet::eggTap() {
  if (!isEgg()) return;
  if (++eggTaps >= 3) hatch();
  else save();
}

PetMood Pet::mood() const {
  if (sleeping) return MOOD_SLEEPING;
  if (eating()) return MOOD_EATING;
  if (lowestStat() < 25) return MOOD_SAD;
  return MOOD_HAPPY;
}

// --- guardado por slots (F34) ---
// clave con prefijo de slot: el slot 0 usa la clave sin prefijo, asi los
// guardados de mascota unica son literalmente el slot 0 (migracion gratis)
static const char *slotKey(char (&buf)[16], int slot, const char *base) {
  if (slot <= 0) return base;
  snprintf(buf, sizeof(buf), "s%d.%s", slot, base);
  return buf;
}

// el registro completo de un slot viaja en bloque: el activo lo escribe
// Pet::save() y las reservas Party::saveAll() con el mismo par de funciones
static void wrRec(Preferences &p, int slot, const PetRecord &r) {
  char b[16];
  p.putBool(slotKey(b, slot, "used"), r.used);
  p.putUChar(slotKey(b, slot, "full"), r.fullness);
  p.putUChar(slotKey(b, slot, "joy"), r.joy);
  p.putUChar(slotKey(b, slot, "ene"), r.energy);
  p.putUChar(slotKey(b, slot, "hyg"), r.hygiene);
  p.putUChar(slotKey(b, slot, "poop"), r.poops);
  p.putUChar(slotKey(b, slot, "wgt"), r.weight);
  p.putUChar(slotKey(b, slot, "gatk"), r.geneAtk);
  p.putUChar(slotKey(b, slot, "gdef"), r.geneDef);
  p.putUChar(slotKey(b, slot, "gspe"), r.geneSpe);
  p.putUChar(slotKey(b, slot, "tatk"), r.trAtk);
  p.putUChar(slotKey(b, slot, "tdef"), r.trDef);
  p.putUChar(slotKey(b, slot, "tspe"), r.trSpe);
  p.putBool(slotKey(b, slot, "bk"), r.berryKnown);
  p.putBool(slotKey(b, slot, "shy"), r.shiny);
  p.putBool(slotKey(b, slot, "eshy"), r.eggShiny);
  p.putBool(slotKey(b, slot, "stpk"), r.starterPick);
  p.putUInt(slotKey(b, slot, "age"), r.ageMinutes);
  p.putShort(slotKey(b, slot, "dexn"), r.speciesId);
  p.putShort(slotKey(b, slot, "eggT2"), r.eggTarget);
  p.putUChar(slotKey(b, slot, "crack"), r.eggTaps);
  p.putUChar(slotKey(b, slot, "mist"), r.careMistakes);
  p.putBool(slotKey(b, slot, "sleep"), r.sleeping);
  p.putUChar(slotKey(b, slot, "bond"), r.bond);
  p.putUShort(slotKey(b, slot, "medal"), r.medals);
  p.putString(slotKey(b, slot, "nick"), r.nick);
}

static void rdRec(Preferences &p, int slot, PetRecord &r) {
  char b[16];
  r.used = p.getBool(slotKey(b, slot, "used"), false);
  r.fullness = p.getUChar(slotKey(b, slot, "full"), 80);
  r.joy = p.getUChar(slotKey(b, slot, "joy"), 80);
  r.energy = p.getUChar(slotKey(b, slot, "ene"), 80);
  r.hygiene = p.getUChar(slotKey(b, slot, "hyg"), 100);
  r.poops = p.getUChar(slotKey(b, slot, "poop"), 0);
  r.weight = p.getUChar(slotKey(b, slot, "wgt"), 0);
  r.geneAtk = p.getUChar(slotKey(b, slot, "gatk"), 0);
  r.geneDef = p.getUChar(slotKey(b, slot, "gdef"), 0);
  r.geneSpe = p.getUChar(slotKey(b, slot, "gspe"), 0);
  r.trAtk = p.getUChar(slotKey(b, slot, "tatk"), 0);
  r.trDef = p.getUChar(slotKey(b, slot, "tdef"), 0);
  r.trSpe = p.getUChar(slotKey(b, slot, "tspe"), 0);
  r.berryKnown = p.getBool(slotKey(b, slot, "bk"), false);
  r.shiny = p.getBool(slotKey(b, slot, "shy"), false);
  r.eggShiny = p.getBool(slotKey(b, slot, "eshy"), false);
  r.starterPick = p.getBool(slotKey(b, slot, "stpk"), false);
  r.ageMinutes = p.getUInt(slotKey(b, slot, "age"), 0);
  if (p.isKey(slotKey(b, slot, "dexn"))) {
    r.speciesId = p.getShort(slotKey(b, slot, "dexn"), -1);
    r.eggTarget = p.getShort(slotKey(b, slot, "eggT2"), 4);
  } else {
    // migracion desde la version con indices de flash (0-8): solo claves sin
    // prefijo, o sea el slot 0
    static const uint8_t OLD2DEX[9] = { 4, 5, 6, 1, 2, 3, 7, 8, 9 };
    int8_t old = p.getChar("spec", -1);
    r.speciesId = (old >= 0 && old < 9) ? OLD2DEX[old] : -1;
    int8_t oldT = p.getChar("eggT", 0);
    r.eggTarget = (oldT >= 0 && oldT < 9) ? OLD2DEX[oldT] : 4;
  }
  r.eggTaps = p.getUChar(slotKey(b, slot, "crack"), 0);
  r.careMistakes = p.getUChar(slotKey(b, slot, "mist"), 0);
  r.sleeping = p.getBool(slotKey(b, slot, "sleep"), false);
  r.bond = p.getUChar(slotKey(b, slot, "bond"), 0);
  r.medals = p.getUShort(slotKey(b, slot, "medal"), 0);
  p.getString(slotKey(b, slot, "nick"), r.nick, sizeof(r.nick));
}

void Pet::save() {
  ticksSinceSave = 0;
  pendingSave = false;
  // estado del dispositivo: claves sin prefijo, comun a todo el party
  prefs.putBytes("dexreg", dev.dexReg, sizeof(dev.dexReg));
  prefs.putBytes("dexsh", dev.dexShinyReg, sizeof(dev.dexShinyReg));
  prefs.putUShort("strk", dev.streak);
  prefs.putUShort("bstrk", dev.bestStreak);
  prefs.putUInt("cday", dev.lastCareDay);
  prefs.putUChar("lend", dev.lastEnd);
  prefs.putUShort("tmedal", dev.totalMedals);
  prefs.putUShort("mstone", dev.lastMilestone);
  prefs.putUShort("ghi", dev.gameHi);
  prefs.putUShort("shi", dev.strHi);
  if (dev.lastSeenEpoch) prefs.putUInt("seen", dev.lastSeenEpoch);
  prefs.putUInt("stcnt", dev.stepsToday);
  prefs.putUInt("sttot", dev.stepsTotal);
  prefs.putUInt("stday", dev.stepsDay);
  prefs.putUInt("stspnt", dev.walkStepsSpent);
  prefs.putUChar("stpts", dev.walkPtsToday);
  prefs.putUChar("stgoal", dev.goalDone);
  // registro del slot enfocado
  PetRecord r;
  toRecord(r);
  wrRec(prefs, slot, r);
}

void Pet::load() {
  prefs.getBytes("dexreg", dev.dexReg, sizeof(dev.dexReg));
  prefs.getBytes("dexsh", dev.dexShinyReg, sizeof(dev.dexShinyReg));
  dev.streak = prefs.getUShort("strk", 0);
  dev.bestStreak = prefs.getUShort("bstrk", 0);
  dev.lastCareDay = prefs.getUInt("cday", 0);
  dev.lastEnd = prefs.getUChar("lend", CER_NONE);
  dev.totalMedals = prefs.getUShort("tmedal", 0);
  dev.lastMilestone = prefs.getUShort("mstone", 0);
  dev.gameHi = prefs.getUShort("ghi", 0);
  dev.strHi = prefs.getUShort("shi", 0);
  dev.lastSeenEpoch = prefs.getUInt("seen", 0);
  dev.stepsToday = prefs.getUInt("stcnt", 0);
  dev.stepsTotal = prefs.getUInt("sttot", 0);
  dev.stepsDay = prefs.getUInt("stday", 0);
  dev.walkStepsSpent = prefs.getUInt("stspnt", 0);
  dev.walkPtsToday = prefs.getUChar("stpts", 0);
  dev.goalDone = prefs.getUChar("stgoal", 0);
  PetRecord r;
  rdRec(prefs, slot, r);
  if (r.geneAtk == 0) {  // mascota anterior a los genes: tirada unica ahora
    r.geneAtk = 90 + random(21);
    r.geneDef = 90 + random(21);
    r.geneSpe = 90 + random(21);
  }
  fromRecord(r);
  // siembra: la mascota actual cuenta como criada (guardados antiguos)
  if (speciesId >= 1) registerSpecies(speciesId);
}

// --- party (F34) ---

void Pet::toRecord(PetRecord &r) const {
  r.used = true;
  r.fullness = fullness;
  r.joy = joy;
  r.energy = energy;
  r.hygiene = hygiene;
  r.poops = poops;
  r.weight = weight;
  r.geneAtk = geneAtk;
  r.geneDef = geneDef;
  r.geneSpe = geneSpe;
  r.trAtk = trAtk;
  r.trDef = trDef;
  r.trSpe = trSpe;
  r.berryKnown = berryKnown;
  r.shiny = shiny;
  r.eggTarget = eggTarget;
  r.eggShiny = eggShiny;
  r.eggTaps = eggTaps;
  r.starterPick = starterPick;
  r.ageMinutes = ageMinutes;
  r.speciesId = speciesId;
  r.careMistakes = careMistakes;
  r.sleeping = sleeping;
  r.bond = bond;
  r.medals = medals;
  strncpy(r.nick, nick, sizeof(r.nick) - 1);
  r.nick[sizeof(r.nick) - 1] = 0;
}

void Pet::fromRecord(const PetRecord &r) {
  fullness = r.fullness;
  joy = r.joy;
  energy = r.energy;
  hygiene = r.hygiene;
  poops = r.poops;
  weight = r.weight;
  geneAtk = r.geneAtk;
  geneDef = r.geneDef;
  geneSpe = r.geneSpe;
  trAtk = r.trAtk;
  trDef = r.trDef;
  trSpe = r.trSpe;
  berryKnown = r.berryKnown;
  shiny = r.shiny;
  eggTarget = r.eggTarget;
  eggShiny = r.eggShiny;
  eggTaps = r.eggTaps;
  starterPick = r.starterPick;
  ageMinutes = r.ageMinutes;
  speciesId = r.speciesId;
  careMistakes = r.careMistakes;
  sleeping = r.sleeping;
  bond = r.bond;
  medals = r.medals;
  strncpy(nick, r.nick, sizeof(nick) - 1);
  nick[sizeof(nick) - 1] = 0;
  // transitorios: al cambiar de foco no se arrastra nada visual ni temporales
  prevSpeciesId = -1;
  ceremony = CER_NONE;
  ceremonyUntil = eatUntil = heartUntil = evolveUntil = 0;
  medalUntil = milestoneUntil = 0;
  newMedal = 0;
  neglectTicks = mistakeCooldown = goodTicks = bondToday = 0;
  evoDeclinedLv = 0;
  farDeclinedAge = 0;
  ticksSinceSave = 0;
  pendingSave = false;
  lastTick = millis();
}

// envejecimiento de reserva: same clock, slower care. Un companiero no
// enfocado envejece su edad, pero decae 4x mas lento, con suelo
// RESERVE_FLOOR, sin cacas, sin descuidos, sin escaparse y sin eclosionar:
// al recuperarlo siempre queda algo de mimo que hacer, y los pasos/XP siguen
// siendo solo del activo
void Pet::applyIdleMinutes(PetRecord &r, uint32_t mins) {
  if (mins > 14UL * 24 * 60) mins = 14UL * 24 * 60;  // mismo tope que offline
  for (uint32_t i = 0; i < mins; i++) {
    r.ageMinutes++;
    if (r.speciesId < 0) continue;  // huevo: solo envejece, no eclosiona en reserva
    if (i % 4 == 0) {
      r.fullness = dropTo(r.fullness, 1, RESERVE_FLOOR);
      r.joy = dropTo(r.joy, 1, RESERVE_FLOOR);
      r.energy = dropTo(r.energy, 1, RESERVE_FLOOR);
    }
    if (i % 8 == 0) r.hygiene = dropTo(r.hygiene, 1, RESERVE_FLOOR);
    if (i % 3 == 0 && r.weight > 0) r.weight--;
  }
}

void Party::begin() {
  pet.prefs.begin("tamapoke", false);
  // 0xFF = sin meta de party: guardado de la epoca de mascota unica
  uint8_t pact = pet.prefs.getUChar("pact", 0xFF);
  if (!pet.prefs.getBool("init", false)) {
    pet.prefs.putBool("init", true);
    pet.slot = 0;
    pet.newEgg();  // guarda solo (slot 0)
  } else if (pact != 0xFF) {
    uint8_t mask = pet.prefs.getUChar("pmask", 1);
    if (pact >= PARTY_MAX || !(mask & (1u << pact))) pact = 0;
    for (int i = 0; i < PARTY_MAX; i++) {
      slots[i].used = (mask >> i) & 1;
      if (!slots[i].used) continue;
      if (i == (int)pact) {
        pet.slot = (int8_t)i;
        pet.load();
      } else {
        rdRec(pet.prefs, i, slots[i]);
      }
    }
    if (!slots[pact].used) {  // meta corrupta: slot activo sin registro
      slots[pact].used = true;
      pet.slot = (int8_t)pact;
      pet.newEgg();
    }
  } else {
    // guardado antiguo de mascota unica: es literalmente el slot 0
    // (mismas claves sin prefijo), la migracion no toca nada
    pet.slot = 0;
    pet.load();
    slots[0].used = true;
  }
  syncSlotsFromPet();
  saveAll();
  pet.lastTick = millis();
  lastAge = pet.ageMinutes;
}

void Party::syncSlotsFromPet() { pet.toRecord(slots[pet.slot]); }

void Party::saveAll() {
  pet.save();  // activo (registro + dispositivo)
  uint8_t mask = 0;
  for (int i = 0; i < PARTY_MAX; i++) {
    if (!slots[i].used) continue;
    mask |= (1u << i);
    if (i != (int)pet.slot) wrRec(pet.prefs, i, slots[i]);
  }
  pet.prefs.putUChar("pmask", mask);
  pet.prefs.putUChar("pact", (uint8_t)pet.slot);
}

bool Party::focus(int i) {
  if (!canFocus(i)) return false;
  pet.toRecord(slots[pet.slot]);  // exportar antes de cambiar de slot
  PetRecord r = slots[i];
  pet.slot = (int8_t)i;
  pet.fromRecord(r);
  saveAll();
  lastAge = pet.ageMinutes;
  return true;
}

int Party::firstFree() const {
  for (int i = 0; i < PARTY_MAX; i++)
    if (!slots[i].used) return i;
  return -1;
}

int Party::filled() const {
  int n = 0;
  for (int i = 0; i < PARTY_MAX; i++)
    if (slots[i].used) n++;
  return n;
}

bool Party::add(const PetRecord &rec) {
  int i = firstFree();
  if (i < 0) return false;  // party lleno: rechazo limpio, sin sobrescribir
  slots[i] = rec;
  slots[i].used = true;
  saveAll();
  return true;
}

bool Party::removeSlot(int i) {
  if (!slotUsed(i) || i == (int)pet.slot) return false;
  slots[i].used = false;
  saveAll();
  return true;
}

void Party::update(uint32_t nowMs) {
  // bomba de minutos: lo que envejece el activo es lo que envejecen LENTO
  // las reservas (en una ceremonia el activo no envejece: las reservas
  // tampoco, coherente con el tiempo congelado)
  uint32_t before = pet.ageMinutes;
  pet.update(nowMs);
  uint32_t gained = (pet.ageMinutes >= before) ? pet.ageMinutes - before : 0;
  if (gained > 0 && gained < 100000) {  // ageMinutes reset = huevo nuevo
    for (int i = 0; i < PARTY_MAX; i++)
      if (i != (int)pet.slot && slots[i].used) Pet::applyIdleMinutes(slots[i], gained);
  }
  lastAge = pet.ageMinutes;
}

void Party::flushSave() {
  if (!pet.savePending()) return;
  syncSlotsFromPet();
  saveAll();  // save() del activo limpia el pendientey se escriben reservas+meta
}

void Party::setClock(uint32_t nowEpoch) { pet.setClock(nowEpoch); }

void Party::syncClock(uint32_t nowEpoch) {
  uint32_t seen = pet.savedEpoch();
  uint32_t mins = (seen && nowEpoch > seen) ? (nowEpoch - seen) / 60 : 0;
  if (mins > 14UL * 24 * 60) mins = 14UL * 24 * 60;
  pet.syncClock(nowEpoch);
  // las reservas solo envejecen lento si al activo le aplica la progresion
  if (mins >= 2 && seen && nowEpoch && pet.ceremony == CER_NONE && !pet.starterPick) {
    for (int i = 0; i < PARTY_MAX; i++)
      if (i != (int)pet.slot && slots[i].used) Pet::applyIdleMinutes(slots[i], mins);
  }
  syncSlotsFromPet();
  saveAll();  // el offline toca el reloj de todas las reservas: volcar ya
}

