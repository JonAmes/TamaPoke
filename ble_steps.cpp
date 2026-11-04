// Glue BLE del contrato de pasos (F25). Solo ESP32: la logica del contrato
// vive en step_link.cpp, que es la que se prueba en host.
#if defined(ESP32)
#include "net/ble_steps.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "net/step_link.h"

static void (*s_apply)(int32_t) = nullptr;
static uint32_t (*s_total)() = nullptr;
static BLECharacteristic *s_evt = nullptr;
static bool s_on = false;

// STEPS_DELTA: el telefono escribe 4B LE; validamos y hacemos ack por notify
class DeltaCbs : public BLECharacteristicCallbacks {
 public:
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    int32_t d = 0;
    int ack = slParseDelta((const uint8_t *)v.c_str(), v.length(), &d)
                  ? slCheckDelta(d)
                  : SL_BAD_FORMAT;
    if (ack == SL_OK) s_apply(d);
    if (s_evt) {
      uint8_t a[5];
      slBuildAck(ack, s_total(), a);
      s_evt->setValue(a, sizeof a);
      s_evt->notify();
    }
  }
};

// STEPS_TOTAL: lectura siempre fresca, el valor se pone al contestar
class TotalCbs : public BLECharacteristicCallbacks {
 public:
  void onRead(BLECharacteristic *c) override {
    uint8_t t[4];
    slWriteTotal(s_total(), t);
    c->setValue(t, sizeof t);
  }
};

void bleStepsStart(const char *name, void (*apply)(int32_t delta),
                   uint32_t (*total)()) {
  s_apply = apply;
  s_total = total;
  BLEDevice::init(name);
  BLEServer *srv = BLEDevice::createServer();
  BLEService *svc = srv->createService(SL_UUID_SERVICE);

  BLECharacteristic *delta = svc->createCharacteristic(
      SL_UUID_STEPS_DELTA, BLECharacteristic::PROPERTY_WRITE);
  static DeltaCbs deltaCbs;
  delta->setCallbacks(&deltaCbs);

  BLECharacteristic *tot = svc->createCharacteristic(
      SL_UUID_STEPS_TOTAL, BLECharacteristic::PROPERTY_READ);
  static TotalCbs totalCbs;
  tot->setCallbacks(&totalCbs);

  s_evt = svc->createCharacteristic(SL_UUID_STEP_EVT,
                                    BLECharacteristic::PROPERTY_NOTIFY);
  s_evt->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SL_UUID_SERVICE);
  adv->start();
  s_on = true;
}

bool bleStepsOn() { return s_on; }

#endif  // ESP32
