#pragma once
#include <Arduino.h>
#include <Preferences.h>

// 1 tick = 1 minuto de juego. Baja este valor para probar mas rapido
// (p. ej. 5000UL = las estadisticas caen 12x mas rapido).
#define PET_TICK_MS 60000UL
// Minutos de juego por nivel. Con 60, CHARMANDER evoluciona a las ~16 h
// de juego con cuidado perfecto. Baja a 1 para ver evoluciones al momento.
#define MINUTES_PER_LEVEL 60
#define EAT_ANIM_MS 2500UL
#define HEART_MS 1500UL
#define EVOLVE_ANIM_MS 5200UL              // animacion de evolucion (mas larga = mas epica)
#define CEREMONY_MS 10000UL                // duracion de la despedida en pantalla
#define FAREWELL_AGE_MIN (3UL * 24 * 60)   // se despide a los 3 dias de juego (en forma final)
#define RUNAWAY_TICKS 60                   // se escapa tras 1 h con TODO a cero
// companeros conviviendo en un dispositivo (F34). Con PARTY_MAX=1 el bucle
// monomascota de siempre queda identico: no hay reservas que envejecer.
#define PARTY_MAX 4
// suelo de las reservas: un companero en reserva nunca pasa hambre extrema
// (decae lento y se detiene aqui; el ciclo de cuidados completo solo aplica
// al companiero activo)
#define RESERVE_FLOOR 40
// pasos reales -> juego (F14/F16): la marcha entrena la VEL del companero
// ACTIVO solo. cada STEPS_PER_PT pasos = 1 punto de entrenamiento con tope
// diario WALK_PT_DAY_CAP (sin pasos el juego no avisa: los pasos regalan).
// STEP_GOAL pasos en el dia celebran bonus (F16; 8000 por defecto, plan.md)
#define STEP_GOAL 8000
#define STEPS_PER_PT 200
#define WALK_PT_DAY_CAP 10

// milisegundos que faltan hasta `deadline` (0 si ya paso, o si deadline==0:
// "temporizador inactivo"). Todos los temporizadores del juego (dialogos,
// animaciones) se guardan como un instante absoluto "deadline = millis() +
// duracion" y se comparaban con millis() < deadline / millis() > deadline;
// eso se rompe cuando millis() da la vuelta a los ~49,7 dias de uptime. La
// resta sin signo, reinterpretada como con signo, es segura frente a esa
// vuelta mientras el intervalo real sea muy inferior a ~24,8 dias (aqui el
// temporizador mas largo son ~12 s), sin cambiar como se guarda el deadline.
static inline uint32_t timeLeft(uint32_t deadline) {
  if (!deadline) return 0;
  int32_t left = (int32_t)(deadline - millis());
  return left > 0 ? (uint32_t)left : 0;
}

// ceremonias de fin de ciclo
enum : uint8_t { CER_NONE = 0, CER_FAREWELL, CER_RUNAWAY, CER_RELEASE };

enum PetMood : uint8_t { MOOD_HAPPY, MOOD_SAD, MOOD_EATING, MOOD_SLEEPING };

// medallas del individuo (bitmask)
enum : uint16_t {
  MED_LV10 = 1 << 0, MED_LV25 = 1 << 1, MED_LV50 = 1 << 2,
  MED_BERRY = 1 << 3, MED_STREAK7 = 1 << 4, MED_BOND = 1 << 5,
  MED_FINAL = 1 << 6, MED_FIT = 1 << 7, MED_WALK = 1 << 8,
};
#define MED_COUNT 9

// Estado del DISPOSITIVO (no del individuo): lo que sobrevive a cualquier
// crianza y es comun a todos los companeros. La pokedex es la union sobre
// todos los companeros: recibir una mascota suma su especie aunque otra ya
// la tenga.
struct DevState {
  uint8_t dexReg[19] = { 0 };       // pokedex de criados (bitmap 151 bits)
  uint8_t dexShinyReg[19] = { 0 };  // criados en version shiny
  // racha de cuidado diario (del jugador: persiste entre crianzas)
  uint16_t streak = 0, bestStreak = 0;
  uint32_t lastCareDay = 0;
  uint8_t lastEnd = CER_NONE;  // como acabo la anterior (afecta al huevo)
  uint16_t totalMedals = 0;    // contador acumulado entre todas las crianzas
  uint16_t lastMilestone = 0;  // hito de racha ya celebrado
  uint16_t gameHi = 0;         // record del minijuego (del jugador)
  uint16_t strHi = 0;          // record de golpes al saco
  uint32_t lastSeenEpoch = 0;  // ultima hora RTC vista (progresion offline)
  // pasos del dia (F14/F16): contador del dispositivo; el entrenamiento que
  // producen lo recibe quien este en foco cuando llegan (activo only, F34)
  uint32_t stepsToday = 0, stepsTotal = 0;
  uint32_t stepsDay = 0;         // serial de dia del contador actual
  uint32_t walkStepsSpent = 0;   // pasos ya convertidos en puntos hoy
  uint8_t walkPtsToday = 0;      // puntos de VEL regalados por marcha hoy
  uint8_t goalDone = 0;          // STEP_GOAL ya celebrado hoy
};

// Estado COMPLETO de un companero. Es el snapshot persistido por slot y,
// mas adelante, el payload de transferencia entre dispositivos (§5d):
// ~100 B, cabe en un MTU de BLE.
struct PetRecord {
  bool used = false;  // slot ocupado
  uint8_t fullness = 80, joy = 80, energy = 80, hygiene = 100;
  uint8_t poops = 0, weight = 0;
  uint8_t geneAtk = 100, geneDef = 100, geneSpe = 100;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  bool berryKnown = false, shiny = false;
  uint32_t ageMinutes = 0;
  int16_t speciesId = -1;  // numero de Pokedex (1-151), -1 = huevo
  uint8_t careMistakes = 0;
  bool sleeping = false;
  int16_t eggTarget = 1;  // dex oculto que saldra del huevo
  bool eggShiny = false;
  uint8_t eggTaps = 0;
  bool starterPick = false;  // esperando eleccion de inicial
  uint8_t bond = 0;          // vinculo (del bicho, no del jugador)
  uint16_t medals = 0;       // medallas del individuo
  char nick[12] = "";        // apodo (vacio = nombre de especie)
};

class Pet {
public:
  // Estadisticas 0..100
  uint8_t fullness = 80;  // comida
  uint8_t joy = 80;       // felicidad
  uint8_t energy = 80;    // energia
  uint8_t hygiene = 100;  // limpieza
  uint8_t poops = 0;      // cacas en pantalla (max 3)
  uint8_t weight = 0;     // 0-100: las chuches engordan, el minijuego quema
  // genes (90-110%, se tiran al eclosionar) y entrenamiento (0-100)
  uint8_t geneAtk = 100, geneDef = 100, geneSpe = 100;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  bool berryKnown = false;  // ya descubrio su baya favorita
  bool shiny = false;       // variante de color rara (se sortea en el huevo)
  uint32_t ageMinutes = 0;
  int16_t speciesId = -1;      // numero de Pokedex (1-151), -1 = huevo
  int16_t prevSpeciesId = -1;  // para la animacion de evolucion (no se persiste)
  uint8_t careMistakes = 0;   // descuidos: cada uno retrasa la evolucion 1 nivel
  bool sleeping = false;
  uint8_t ceremony = CER_NONE;  // despedida/escapada/liberacion en curso
  // racha / vinculo / medallas / nombre
  uint8_t bond = 0;
  char nick[12] = "";
  uint16_t medals = 0;
  uint16_t newMedal = 0;   // recien conseguida(s), para celebrar
  // estado del dispositivo (pokedex, rachas, records): comun a todos los
  // companeros; quien lo vive en vivo es el companiero activo
  DevState dev;
  // slot que ocupa en el party (0 = el de los guardados antiguos, claves sin
  // prefijo; slot>0 añade el prefijo "s<n>." a las claves del individuo)
  int8_t slot = 0;

  void begin();                 // carga estado de NVS (o crea el primer huevo)
  void update(uint32_t nowMs);  // llamar en cada loop()

  // Acciones (botones tactiles)
  void feed();              // baya roja (compatibilidad)
  void feedBerry(uint8_t color);  // 0 roja, 1 azul, 2 verde
  void feedCandy();
  bool lovesBerry(uint8_t color) const {
    return !isEgg() && (speciesId % 3) == color;  // gusto oculto por especie
  }
  void playResult(uint8_t score);  // recompensa del minijuego (entrena VEL)
  uint8_t trainStrength(uint16_t hits);  // saco de entrenamiento (entrena FUE)
  // pasos reales (F14/F16): suman al contador del dispositivo y la marcha
  // entrena la VEL del companiero en foco, 1 pt cada STEPS_PER_PT pasos con
  // tope diario; al llegar a STEP_GOAL se celebra el bonus del dia
  void addSteps(uint32_t delta);
  uint32_t stepsDayCount() const { return dev.stepsToday; }
  uint32_t stepsLifetime() const { return dev.stepsTotal; }
  bool goalDoneToday() const { return dev.goalDone != 0; }

  // stats de combate: base real de gen 1 x genes + nivel + entrenamiento
  uint16_t atkStat() const;
  uint16_t defStat() const;
  uint16_t speStat() const;
  void play();
  void toggleLight();  // dormir / despertar
  void clean();
  void caress();  // tocar al bicho
  void eggTap();  // tocar el huevo: 3 toques y eclosiona
  void newEgg();   // empezar de cero con un inicial aleatorio
  void release();  // soltar (pulsacion larga + confirmar)
  void syncClock(uint32_t nowEpoch);  // aplica el tiempo transcurrido apagado
  void setClock(uint32_t nowEpoch);   // fija la hora sin aplicar progresion
  void startFarewell();  // tambien usable desde la consola serie (BYE)
  void startRunaway();   // tambien usable desde la consola serie (RUN)

  bool isEgg() const { return speciesId < 0; }
  uint8_t eggCracks() const { return eggTaps; }
  bool eating() const { return timeLeft(eatUntil) > 0; }
  bool showHeart() const { return timeLeft(heartUntil) > 0; }
  bool evolving() const { return timeLeft(evolveUntil) > 0; }
  float evolveT() const {     // progreso de la animacion de evolucion 0..1
    return 1.0f - (float)timeLeft(evolveUntil) / (float)EVOLVE_ANIM_MS;
  }
  bool canEvolveNow() const;  // condiciones de evolucion cumplidas (lista)
  void evolve();              // dispara la transformacion (la llama un toque del usuario)
  bool canFarewellNow() const;  // forma final + 7 dias: lista para despedirse (boton)
  bool canRunawayNow() const;   // abandono total 1h: lista para escaparse (boton triste)
  // el usuario decide en un dialogo; "mantener/quedaros" pospone y re-ofrece luego
  bool wantEvolveButton() const { return canEvolveNow() && level() > evoDeclinedLv; }
  bool wantFarewellButton() const { return canFarewellNow() && ageMinutes >= farDeclinedAge; }
  void declineEvolve() { evoDeclinedLv = level(); }              // re-ofrece al subir de nivel
  void declineFarewell() { farDeclinedAge = ageMinutes + 1440; } // re-ofrece dentro de 1 dia
  // primera partida: el jugador elige inicial (Bulbasaur/Charmander/Squirtle)
  bool awaitingStarter() const { return starterPick; }
  // alta en la Pokedex al capturar un salvaje (B1); hatch/evolve
  // tambien la usan por dentro
  void registerSpecies(int16_t dex);
  void chooseStarter(int16_t dex) { eggTarget = dex; starterPick = false; save(); }
  void factoryReset() { prefs.clear(); }  // borra la NVS (test: comando serie WIPE)
  void dbgRunawayReady() { fullness = joy = energy = hygiene = 0; neglectTicks = RUNAWAY_TICKS; }  // test
  // uint16_t, no uint8_t: con MINUTES_PER_LEVEL=60 el nivel son las horas, asi
  // que un uint8_t da la vuelta a 0 en el nivel 256, a los ~10,6 dias. Y eso se
  // alcanza jugando normal, porque "quedaros juntos" permite posponer la
  // despedida indefinidamente. Se topa en 999 en vez de desbordar.
  uint16_t level() const {
    uint32_t lv = 1 + ageMinutes / MINUTES_PER_LEVEL;
    return lv > 999 ? 999 : (uint16_t)lv;
  }
  bool isRegistered(int16_t dex) const {
    return dex >= 1 && dex <= 151 && (dev.dexReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
  }
  bool isShinyRegistered(int16_t dex) const {
    return dex >= 1 && dex <= 151 && (dev.dexShinyReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
  }
  uint16_t registeredCount() const;
  bool lineHasUnregistered(int16_t base) const;
  uint8_t eggRarity() const;       // rareza del huevo actual (sin revelar especie)
  int16_t pickEggSpecies();        // publica para poder simular tiradas (EGGS)
  uint8_t lowestStat() const { return min(min(fullness, joy), min(energy, hygiene)); }
  PetMood mood() const;
  // progreso de la ceremonia de despedida/escapada, 0..1 (para animarla)
  float ceremonyT() const {
    if (ceremony == CER_NONE) return 0.0f;
    return 1.0f - (float)timeLeft(ceremonyUntil) / (float)CEREMONY_MS;
  }

  bool hasMedal(uint16_t m) const { return medals & m; }
  void rename(const char *name);  // apodo del companiero (lo pone el jugador)
  bool showMedal() const { return timeLeft(medalUntil) > 0; }
  bool showMilestone() const { return timeLeft(milestoneUntil) > 0; }
  int careBonus() const;  // mejora del huevo por racha + vinculo

  // guardado periodico diferido: tick() marca pendiente y el loop lo vuelca
  // cuando la pantalla esta atenuada/apagada (la escritura a flash congela
  // ~1s ambos cores: asi no se ve ni corta el tactil)
  bool savePending() const { return pendingSave; }
  // ultima hora real persistida; sirve para resembrar un RTC que perdio la hora
  uint32_t savedEpoch() { return prefs.getUInt("seen", 0); }
  void flushSave();

  // --- party (F34) ---
  // volcar/restaurar el estado del individuo (lo que vive en un PetRecord).
  // fromRecord ademas resetea las animaciones y temporizadores transitorios:
  // al cambiar de foco no hay que arrastrar estados visuales del saliente.
  void toRecord(PetRecord &r) const;
  void fromRecord(const PetRecord &r);
  // envejecimiento de reserva: un companiero no enfocado envejece su edad,
  // pero sus stats decaen LENTO y con suelo RESERVE_FLOOR: sin descuidos,
  // sin cacas, sin escapadas y sin eclosionar. Host-testeable.
  static void applyIdleMinutes(PetRecord &r, uint32_t mins);

private:
  Preferences prefs;
  uint32_t lastTick = 0;
  uint32_t eatUntil = 0;
  uint32_t heartUntil = 0;
  uint32_t evolveUntil = 0;
  int16_t eggTarget = 1;       // especie oculta que saldra del huevo
  bool eggShiny = false;       // sorpresa sorteada al crear el huevo
  uint8_t eggTaps = 0;
  uint8_t mistakeCooldown = 0;
  uint8_t ticksSinceSave = 0;
  bool pendingSave = false;     // guardado periodico pendiente de volcar
  uint16_t evoDeclinedLv = 0;   // "mantener forma": no ofrecer evolucion hasta subir de nivel
  uint32_t farDeclinedAge = 0;  // "quedaros juntos": no ofrecer despedida hasta esta edad
  bool starterPick = false;     // primera partida: esperando que el jugador elija inicial
  uint8_t neglectTicks = 0;
  uint16_t goodTicks = 0;  // racha bien cuidado: forja la DEF
  uint32_t ceremonyUntil = 0;
  uint8_t bondToday = 0;       // tope diario de subida de vinculo
  uint32_t medalUntil = 0;     // celebracion de medalla en pantalla
  uint32_t milestoneUntil = 0; // celebracion de hito de racha

  uint32_t today() const { return dev.lastSeenEpoch ? dev.lastSeenEpoch / 86400 : 0; }
  void registerCare();   // primer cuidado del dia: racha + vinculo
  void addBond(uint8_t amt);
  void checkMedals();
  void tick();
  void hatch();
  void save();
  void load();
  static uint8_t clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

  friend class Party;
};

// Partido de companeros (F34): hasta PARTY_MAX mascotas conviviendo, una
// ACTIVA (la enfocada: se renderiza, se cuida, entrena con los pasos) y el
// resto en reserva (envejecen lento, ver Pet::applyIdleMinutes). `pet` es el
// companiero activo en vivo; `slots[i]` es el snapshot persistido del slot i
// (incluido el del activo, que se sincroniza en cada guardado). Cambiar de
// foco = exportar el activo a su slot, importar el del slot elegido.
// Con un solo slot ocupado el bucle monomascota queda identico.
class Party {
public:
  Pet pet;                     // companiero activo (estado vivo)
  PetRecord slots[PARTY_MAX];  // snapshot persistido de cada slot

  void begin();                // NVS: partido guardado, migracion de save antiguo, o primer huevo
  void update(uint32_t nowMs); // llama a pet.update y envejece lento las reservas
  void syncClock(uint32_t nowEpoch);  // offline: progresion completa al activo, lenta a reservas
  void setClock(uint32_t nowEpoch);   // fija la hora sin aplicar progresion
  void flushSave();            // vuelca el guardado periodico pendiente (activo + reservas + meta)

  int filled() const;          // slots ocupados
  bool slotUsed(int i) const { return i >= 0 && i < PARTY_MAX && slots[i].used; }
  int activeSlot() const { return pet.slot; }
  bool canFocus(int i) const { return slotUsed(i) && i != pet.slot; }
  bool focus(int i);           // cambia el foco al slot i (host-testeable)
  int firstFree() const;       // -1 si el party esta lleno
  // una mascota llega al primer slot libre; party lleno = RECHAZO limpio
  // (sin sobrescribir: es la regla de migracion/social de §5d)
  bool add(const PetRecord &rec);
  // vaciar un slot (recibir transferencias/liberar): jamas el slot activo
  bool removeSlot(int i);
  void saveAll();              // activo + registros de reserva + meta del party

private:
  uint32_t lastAge = 0;  // pet.ageMinutes visto: bomba de minutos para reservas
  void syncSlotsFromPet();
};
