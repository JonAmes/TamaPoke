#pragma once
#include <stdint.h>
// Glue BLE (solo ESP32): GATT server con el servicio del contrato de pasos
// (F25). apply() recibe deltas ya validados y los mete en el juego; total()
// devuelve los pasos vitalicios para STEPS_TOTAL y el ack.
void bleStepsStart(const char *name, void (*apply)(int32_t delta),
                   uint32_t (*total)());
bool bleStepsOn();
