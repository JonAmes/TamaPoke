// TamaPoke DIAGNOSTICO Guition 4848S040 — smoke test por etapas.
// Objetivo: encontrar donde muere el arranque en la placa Guition. Imprime
// latidos para siempre; cuando el equipo se reinicia, el monitor ve hasta que
// etapa llego antes de cada muerte. Si NUNCA se reinicia y todo sube, el
// firmware real tiene un problema ajeno al display.
//
// Etapas:
//   0  heartbeat + PSRAM info + backlight fijo al maximo y barrido PWM
//      (si la pantalla se ilumina aqui, BL 38 y la alimentacion estan bien)
//   1  gfx->begin(): init ST7701S (SWSPI) + arranque del bus RGB-DMA
//   2  rellenos de color solidos (rojo/verde/azul)
//
// Se flashea desde el instalador web con la opcion "DIAG".

#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "../../board/board_guition_4848s040.h"

// Identico al TamaPoke.ino real (config ESP32_4848S040_86BOX_GUITION)
Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED /* MISO */);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    RGB_DE, RGB_VSYNC, RGB_HSYNC, RGB_PCLK,
    RGB_R0, RGB_R1, RGB_R2, RGB_R3, RGB_R4,
    RGB_G0, RGB_G1, RGB_G2, RGB_G3, RGB_G4, RGB_G5,
    RGB_B0, RGB_B1, RGB_B2, RGB_B3, RGB_B4,
    1 /*hsync pol*/, 10 /*front*/, 8 /*pulso*/, 50 /*back*/,
    1 /*vsync pol*/, 10 /*front*/, 8 /*pulso*/, 20 /*back*/,
    0 /*pclk activo flanco pos*/, RGB_PCLK_HZ /*prefer_speed*/, false /*big endian*/);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    LCD_WIDTH, LCD_HEIGHT, rgbpanel, 0 /*rotation*/, true /*auto flush*/,
    bus, GFX_NOT_DEFINED /*RST*/, st7701_type9_init_operations,
    sizeof(st7701_type9_init_operations));

int stage = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("DIAG-GU 0.9 start");
  Serial.printf("heap=%lu psram=%lu core=%s\n",
                (unsigned long)ESP.getHeapSize(), (unsigned long)ESP.getPsramSize(),
                ESP.getCoreVersion());
  ledcAttach(LCD_BL, 5000, 8);
  ledcWrite(LCD_BL, 255);
  Serial.println("ETAPA 0: backlight 255 (la pantalla deberia encender ya)");
}

void loop() {
  static uint32_t lastBeat = 0, blT = 0;
  static int duty = 255;
  uint32_t now = millis();

  // barrido de retroiluminacion: si el pin PWM manda, el brillo palpita solo
  if (now - blT > 1500) {
    blT = now;
    duty -= 64;
    if (duty <= 0) duty = 255;
    ledcWrite(LCD_BL, duty);
    Serial.printf("BL=%d\n", duty);
  }

  if (stage == 0 && now > 8000) {
    stage = 1;
    Serial.println("ETAPA 1: gfx->begin (ST7701S + RGB DMA)...");
    bool ok = gfx->begin();
    Serial.printf("ETAPA 1: gfx->begin = %d\n", ok ? 1 : 0);
    if (!ok) { stage = 9; }
    else stage = 2;
  }

  if (stage == 2) {
    stage = 3;
    Serial.println("ETAPA 2: rellenos RGB");
    gfx->fillScreen(0xF800); delay(800);
    gfx->fillScreen(0x07E0); delay(800);
    gfx->fillScreen(0x001F); delay(800);
    Serial.println("ETAPA 2: done");
  }

  if (now - lastBeat > 400) {
    lastBeat = now;
    Serial.printf("beat stage=%d heap=%lu\n", stage, (unsigned long)ESP.getFreeHeap());
  }
  delay(5);
}
