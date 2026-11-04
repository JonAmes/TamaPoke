#pragma once
// ---------------------------------------------------------------------------
// Vinculo telefono->reloj para pasos (F25 BLE, F26 Wi-Fi). Logica pura: sin
// plataforma, sin BLE, sin HTTP: solo el contrato de bytes. Se prueba en host
// con test/test_steplink.cpp. El glue de cada transporte (net/ble_steps.cpp,
// net/wifi_steps.cpp) es fino y llama siempre a estas funciones.
//
// Contrato del plan (step-counter-research.md, "0000-TAM-0001-Poke"):
//   STEPS_DELTA  WRITE  int32 LE firmado: pasos desde el ultimo envio
//   STEPS_TOTAL  READ   int32 LE: pasos vitalicios acumulados
//   STEP_EVT     NOTIFY 1B ack + total (4B LE)
// Las UUID van semidas del tag ASCII "TAMAPOKE" + numero, para que al escanear
// se distingan del resto de servicios: "TAMA" "POKE" 0001/2/3/4.
// ---------------------------------------------------------------------------
#include <stddef.h>
#include <stdint.h>

#define SL_UUID_SERVICE     "54414d41-504f-4b45-0001-000000000001"  // TAMA POKE 0001
#define SL_UUID_STEPS_DELTA "54414d41-504f-4b45-0001-000000000002"  //        0002
#define SL_UUID_STEPS_TOTAL "54414d41-504f-4b45-0001-000000000003"  //        0003
#define SL_UUID_STEP_EVT    "54414d41-504f-4b45-0001-000000000004"  //        0004

// un telefono no deberia mandar mas de un dia largo de pasos en un write;
// por encima se rechaza (bicho del emisor o intento de abuso)
#define SL_MAX_DELTA 30000

// codigos de ack (STEP_EVT primer byte; tambien los usa F26 por HTTP)
enum { SL_OK = 0, SL_BAD_FORMAT = 1, SL_BAD_DELTA = 2 };

// payload de STEPS_DELTA: exactamente 4 bytes int32 little-endian
bool slParseDelta(const uint8_t *data, size_t len, int32_t *delta);

// cuerpo del POST /steps de F26: "delta=123" o {"delta":123}; sin plataforma
bool slParseHttpBody(const char *body, size_t len, int32_t *delta);

// politica del delta ya parseado: debe entrar en (0, SL_MAX_DELTA]
int slCheckDelta(int32_t delta);

// valor de STEPS_TOTAL: u32 vitalicio saturado a int32, little-endian
void slWriteTotal(uint32_t total, uint8_t out[4]);

// payload de STEP_EVT: 1B ack + total LE
void slBuildAck(int ack, uint32_t total, uint8_t out[5]);
