#!/bin/bash
# Regenera web/firmware/*.bin (firmware combinado de cada placa) para el
# instalador web. Uso: bash tools/build_web.sh
set -e
cd "$(dirname "$0")/.."
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
# Guition/Sunton 4848S040: OJO con el USB. Los GPIO19/20 del S3 son el USB
# nativo (D-/D+), y esta placa los usa para IIC_SDA(19) y RGB_G1(20). Con
# CDCOnBoot=cdc la consola Serial va a pines que el propio firm se queda
# (tactil/panel): cero logs y PHY inestable. Sin CDC, Serial = UART0
# (GPIO43/44), que es el puente USB-UART que la placa trae al conector.
FQBN_GUITION="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
B=build/esp32.esp32.esp32s3

merge() {  # $1 = nombre de salida
  esptool.py --chip esp32s3 merge-bin -o "web/firmware/$1" \
    0x0     "$B/TamaPoke.ino.bootloader.bin" \
    0x8000  "$B/TamaPoke.ino.partitions.bin" \
    0xe000  "$B/boot_app0.bin" \
    0x10000 "$B/TamaPoke.ino.bin"
  echo "OK -> web/firmware/$1 ($(du -h "web/firmware/$1" | cut -f1))"
}

echo "Compilando Waveshare AMOLED..."
arduino-cli compile --fqbn "$FQBN" --export-binaries .
merge tamapoke.bin

echo "Compilando Guition 4848S040..."
arduino-cli compile --fqbn "$FQBN_GUITION" --export-binaries \
  --build-property "compiler.cpp.extra_flags=-DBOARD_GUITION_4848S040" .
merge tamapoke_guition.bin

echo "Empaquetando sprites..."
python3 tools/pack_bundle.py
