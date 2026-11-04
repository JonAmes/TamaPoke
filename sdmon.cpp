#include "sdmon.h"
#include "pin_config.h"
#include <FS.h>
#include <SD_MMC.h>
#if HAS_SD_SPI
#include <SD.h>
#include <SPI.h>
// El SD del 4848S040 viaja por SPI de hardware. Comparte SCLK/MOSI (48/47) con
// la cola de init del ST7701S, que es SPI de software (bit-bang) y solo actua
// en setup(): tras el arranque el panel manda pixels por el bus RGB DMA y no
// vuelve a tocar esos pins, asi que el SPI de hardware puede quedarselos. CS
// propio (42) y MISO dedicada (41): no hay contencion en el bus.
static SPIClass g_sdSpi(HSPI);
#endif

// Punto unico de acceso al volumen SD: SDMMC en la Waveshare, SPI en la
// Guition. Todo el fichero habla con el juego a traves de esto.
static fs::FS &sdFS() {
#if HAS_SD_SPI
  return SD;
#else
  return SD_MMC;
#endif
}

// tamanos del volumen: los expone la clase concreta de cada placa, no fs::FS
static uint64_t sdCardSize() {
#if HAS_SD_SPI
  return SD.cardSize();
#else
  return SD_MMC.cardSize();
#endif
}
static uint64_t sdTotalBytes() {
#if HAS_SD_SPI
  return SD.totalBytes();
#else
  return SD_MMC.totalBytes();
#endif
}
static uint64_t sdUsedBytes() {
#if HAS_SD_SPI
  return SD.usedBytes();
#else
  return SD_MMC.usedBytes();
#endif
}

bool sdReady = false;
bool sdDirty = false;
SdThumbs thumbs;

bool PmdMon::load(uint8_t dexNum, bool shiny) {
  unload();
  if (!sdReady) return false;

  char path[28];
  snprintf(path, sizeof(path), "/mons/p%s%03u.bin", shiny ? "s" : "", dexNum);
  File f = sdFS().open(path, FILE_READ);
  if (!f && shiny) {  // sin shiny PMD: usa el normal
    snprintf(path, sizeof(path), "/mons/p%03u.bin", dexNum);
    f = sdFS().open(path, FILE_READ);
  }
  if (!f) return false;

  uint32_t size = f.size();
  if (size < 7 || size > 3UL * 1024 * 1024) { f.close(); return false; }
  blob = (uint8_t *)ps_malloc(size);
  if (!blob || f.read(blob, size) != size || memcmp(blob, "TPK2", 4) != 0) {
    if (blob) { free(blob); blob = nullptr; }
    f.close();
    return false;
  }
  f.close();

  uint8_t nActs = blob[4];
  memcpy(&palCount, blob + 5, 2);
  if (palCount > 256 || (uint32_t)7 + palCount * 2 > size) { unload(); return false; }
  memcpy(pal, blob + 7, palCount * 2);

  const uint8_t *p = blob + 7 + palCount * 2;
  const uint8_t *end = blob + size;
  for (uint8_t i = 0; i < nActs && p + 4 <= end; i++) {
    uint8_t id = p[0], w = p[1], h = p[2], nf = p[3];
    p += 4;
    if (id >= PMD_NACTS || nf > 24) { unload(); return false; }
    // valida que ms[] y los datos del frame caben en el blob (archivo truncado)
    uint32_t bytes = (uint32_t)nf * 2 + (uint32_t)w * h * nf;
    if (w == 0 || h == 0 || nf == 0 || p + bytes > end) { unload(); return false; }
    PmdAct &a = acts[id];
    a.w = w;
    a.h = h;
    a.frames = nf;
    for (uint8_t k = 0; k < nf; k++) {
      a.ms[k] = p[0] | (p[1] << 8);
      if (a.ms[k] == 0) a.ms[k] = 100;  // nunca 0: pmdFrameAt() giraria sin avanzar
      p += 2;
    }
    a.data = p;
    p += (uint32_t)w * h * nf;
    // fila mas baja con contenido en cualquier frame: anclar por los pies
    uint8_t base = 1;
    for (uint8_t f = 0; f < nf; f++) {
      const uint8_t *fr = a.data + (uint32_t)f * w * h;
      for (int r = h - 1; r >= 0; r--) {
        bool any = false;
        for (int c = 0; c < w && !any; c++)
          if (fr[r * w + c] != 0xFF) any = true;
        if (any) { if (r + 1 > base) base = r + 1; break; }
      }
    }
    a.base = base;
  }
  loaded = true;
  Serial.printf("cargado %s (%u KB)\n", path, size / 1024);
  return true;
}

void PmdMon::unload() {
  if (blob) {
    free(blob);
    blob = nullptr;
  }
  for (auto &a : acts) {
    a.w = a.h = a.frames = a.base = 0;
    a.data = nullptr;
  }
  loaded = false;
}

void SdThumbs::unload() {
  if (data) { free(data); data = nullptr; }
  loaded = false;
  count = 0;
  size = 0;
}

bool SdThumbs::load() {
  unload();  // recargar sin fugar el blob anterior
  if (!sdReady) return false;
  File f = sdFS().open("/mons/thumbs.bin", FILE_READ);
  if (!f) {
    Serial.println("sin thumbs.bin (galeria sin miniaturas)");
    return false;
  }
  uint32_t sz = f.size();
  // acota el tamano: evita un ps_malloc absurdo con un archivo corrupto
  if (sz < 10 || sz > 2UL * 1024 * 1024) {
    Serial.println("thumbs.bin invalido (tamano)");
    f.close();
    return false;
  }
  data = (uint8_t *)ps_malloc(sz);
  if (!data || f.read(data, sz) != sz || memcmp(data, "TPTH", 4) != 0) {
    Serial.println("thumbs.bin invalido");
    if (data) { free(data); data = nullptr; }
    f.close();
    return false;
  }
  f.close();
  memcpy(&count, data + 4, 2);
  // la tabla de offsets (uno por especie) debe caber entera en lo leido
  if ((uint32_t)6 + 4UL * count > sz) {
    Serial.println("thumbs.bin invalido (tabla de offsets)");
    unload();
    return false;
  }
  size = sz;
  loaded = true;
  Serial.printf("miniaturas cargadas: %u (%u KB)\n", count, sz / 1024);
  return true;
}

const uint8_t *SdThumbs::get(int16_t dex) const {
  if (!loaded || dex < 1 || dex > count) return nullptr;
  uint32_t off;
  memcpy(&off, data + 6 + 4 * (dex - 1), 4);
  // el offset viene del fichero sin comprobar: sin esto, un thumbs.bin truncado
  // o corrupto hacia leer fuera de la reserva de PSRAM
  if (off > size - 3) return nullptr;  // la cabecera w,h,palCount debe caber (size >= 10 siempre)
  uint32_t need = 3 + (uint32_t)data[off + 2] * 2 + (uint32_t)data[off] * data[off + 1];
  if (need > size || off > size - need) return nullptr;  // y el blob entero tambien
  return data + off;
}

bool sdBegin() {
#if HAS_SD
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  sdReady = SD_MMC.begin("/sdcard", true /* modo 1-bit */, true /* formatea si no monta */);
  if (sdReady) {
    Serial.printf("SD montada: %llu MB\n", sdCardSize() / (1024ULL * 1024ULL));
    sdFS().mkdir("/mons");
  } else {
    Serial.println("SD no detectada (el juego usa los sprites de flash)");
  }
  return sdReady;
#else
  // placa sin ranura SDMMC (Guition 4848S040): el slot es SPI (CS 42, CLK 48,
  // MOSI 47, MISO 41). CLK/MOSI se comparten con la cola de init del ST7701S,
  // que es SPI de software y solo corre en setup(); el panel manda despues
  // pixels por el bus RGB DMA, asi que el SPI de hardware queda libre.
  g_sdSpi.begin(SD_SPI_SCLK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);
  // format_if_empty=true: formatea solo si la tarjeta viene vacia (la misma
  // politica de la rama SDMMC), nunca toca una FAT con datos
  sdReady = SD.begin(SD_SPI_CS, g_sdSpi, 16000000, "/sd", 5, true);
  if (sdReady) {
    Serial.printf("SD SPI montada: %llu MB\n", sdCardSize() / (1024ULL * 1024ULL));
    sdFS().mkdir("/mons");
  } else {
    Serial.println("SD SPI no detectada (el juego usa los sprites de flash)");
  }
  return sdReady;
#endif
}

bool SdMon::load(uint8_t dexNum, bool shiny) {
  unload();
  if (!sdReady) return false;

  char path[24];
  snprintf(path, sizeof(path), "/mons/%s%03u.bin", shiny ? "s" : "", dexNum);
  File f = sdFS().open(path, FILE_READ);
  if (!f && shiny) {  // sin variante shiny: usa la normal
    snprintf(path, sizeof(path), "/mons/%03u.bin", dexNum);
    f = sdFS().open(path, FILE_READ);
  }
  if (!f) {
    Serial.printf("no existe %s\n", path);
    return false;
  }

  char magic[4];
  uint16_t header[4];
  if (f.read((uint8_t *)magic, 4) != 4 || memcmp(magic, "TPK1", 4) != 0 ||
      f.read((uint8_t *)header, 8) != 8) {
    f.close();
    return false;
  }
  w = header[0];
  h = header[1];
  frames = header[2];
  frameMs = header[3];
  // acota dimensiones: evita size desbordado o absurdo con archivo corrupto
  if (f.read((uint8_t *)&palCount, 2) != 2 || palCount > 256 ||
      w == 0 || w > 256 || h == 0 || h > 256 || frames == 0 || frames > 64) {
    f.close();
    return false;
  }
  if (f.read((uint8_t *)pal, palCount * 2) != palCount * 2) {
    f.close();
    return false;
  }

  uint32_t size = (uint32_t)w * h * frames;
  data = (uint8_t *)ps_malloc(size);
  if (!data) {
    Serial.println("sin PSRAM para el sprite");
    f.close();
    return false;
  }
  uint32_t got = f.read(data, size);
  f.close();
  if (got != size) {
    Serial.printf("%s truncado (%u de %u)\n", path, got, size);
    unload();
    return false;
  }

  // zoom entero para que el bicho mida ~200 px de alto en pantalla
  scale = 200 / h;
  if (scale < 2) scale = 2;
  if (scale > 5) scale = 5;

  Serial.printf("cargado %s: %ux%u x%u frames @%ums, escala %u\n",
                path, w, h, frames, frameMs, scale);
  loaded = true;
  return true;
}

void SdMon::unload() {
  if (data) {
    free(data);
    data = nullptr;
  }
  loaded = false;
}

// ---------------------------------------------------------------------------
// Protocolo de carga por USB (para llenar la SD sin sacarla de la placa):
//   PUT <ruta> <bytes>\n  + datos crudos   -> "OK" ... "DONE"
//   LS\n                                   -> listado de /mons
// Usar con tools/send_sd.py
// ---------------------------------------------------------------------------

bool sdSerialCommand(const String &line) {
  if (line.startsWith("PUT ")) {
    int sp = line.lastIndexOf(' ');
    String path = line.substring(4, sp);
    uint32_t size = line.substring(sp + 1).toInt();
    if (!sdReady || size == 0 || size > 4 * 1024 * 1024) {
      Serial.println("ERR");
      return true;
    }
    if (!path.startsWith("/")) path = "/" + path;
    // acota la escritura a /mons/: la ruta llega tal cual de la linea serie, sin
    // sanear, asi que un PUT manipulado podria escribir en cualquier sitio de la
    // tarjeta. Los dos clientes reales (tools/send_sd.py y web/index.html) ya
    // mandan nombres con el prefijo mons/.
    if (!path.startsWith("/mons/") || path.indexOf("..") >= 0) {
      Serial.println("ERR");
      return true;
    }
    // FILE_WRITE ANADE al final si el fichero ya existe, asi que reintentar uno
    // que quedo a medias lo alargaba en vez de reemplazarlo: quedaba un sprite
    // corrupto y mas grande que el original. Importa mas desde que el instalador
    // reanuda transferencias cortadas, porque reintenta justo los que fallaron.
    if (sdFS().exists(path)) sdFS().remove(path);
    File f = sdFS().open(path, FILE_WRITE);
    if (!f) {
      Serial.println("ERR");
      return true;
    }
    Serial.println("OK");
    static uint8_t buf[2048];
    uint32_t remaining = size;
    Serial.setTimeout(5000);
    while (remaining > 0) {
      size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
      size_t n = Serial.readBytes(buf, want);
      if (n == 0) break;  // timeout
      // sin mirar el retorno, una tarjeta llena o con fallo de escritura daba
      // DONE igualmente y dejaba el fichero truncado en la SD: justo la entrada
      // corrupta contra la que hay que protegerse luego al cargar el sprite
      if (f.write(buf, n) != n) break;
      remaining -= n;
      Serial.println("#");  // ack: listo para el siguiente bloque
    }
    f.close();
    Serial.setTimeout(1000);
    sdDirty = (remaining == 0);
    Serial.println(remaining == 0 ? "DONE" : "ERR");
    return true;
  } else if (line == "SDINFO") {  // diagnostico remoto de "no me reconoce la SD"
    Serial.printf("total=%llu used=%llu\n", sdTotalBytes(), sdUsedBytes());
    Serial.println("DONE");
    return true;
  } else if (line == "LS") {
    File dir = sdFS().open("/mons");
    if (dir) {
      File e;
      while ((e = dir.openNextFile())) {
        Serial.printf("%s %u\n", e.name(), (uint32_t)e.size());
        e.close();
      }
      dir.close();
    }
    Serial.println("DONE");
    return true;
  }
  return false;
}
