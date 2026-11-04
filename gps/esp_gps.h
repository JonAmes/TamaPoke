// Glue ESP32 del GPS (B2). Waveshare: LC76G en UART0 (GPIO43 RX / GPIO44 TX
// = pines por defecto de Serial0; Serial es USB-CDC). Guition: sin GNSS ->
// gpsFix() reporta siempre no-fix y el juego degrada a pasos (plan.md §5b.5).
#pragma once
#include <stdint.h>
#include "nmea.h"

void gpsStart();                    // abre Serial0 a 9600 (no-op sin HAS_GPS)
void gpsPump(uint32_t nowMs);       // no bloqueante: traga bytes y actualiza
const FixState &gpsFix();           // ultimo estado conocido
// metros integrados por velocidad entre fijaciones (capa 3 del plan); se
// resetea al consumir el encuentro
float gpsTravelledM();
void gpsTravelledReset();
// consola de taller: inyecta una frase NMEA como si llegara del receptor
// (permite probar todo el camino sin antena conectada)
bool gpsInjectLine(const char *line, uint32_t nowMs);
