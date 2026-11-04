#pragma once
#include <stdint.h>
// F26: respaldo Wi-Fi/HTTP para el vinculo de pasos, para placas/telefonos
// sin app BLE. Apagado hasta que alguien lo manda (consola WIFI): no paga
// RAM ni radio por defecto. Mismo contrato de validacion que BLE.
//   POST /steps  cuerpo "delta=N" o {"delta":N}  -> "OK" o "REJECTED"
//   GET  /steps                                  -> total en texto plano
void wifiStepsStart(const char *ssid, const char *pass,
                    void (*apply)(int32_t delta), uint32_t (*total)());
void wifiStepsLoop();  // atender peticiones; vacio si esta apagado
bool wifiStepsOn();
