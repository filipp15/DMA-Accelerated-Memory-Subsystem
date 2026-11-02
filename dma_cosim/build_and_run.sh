#!/usr/bin/env bash
# Pokreni sa: bash build_and_run.sh
# Zahteva instaliran Verilator (apt-get install verilator na Ubuntu/Debian).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

rm -rf obj_dir

echo "== Generisanje C++ omotaca iz Verilog koda + kompajliranje harness-a =="
verilator --cc --exe --build -j 0 \
  -CFLAGS "-std=c++20 -I${SCRIPT_DIR}/harness/include" \
  --top-module dma_controller \
  rtl/dma_controller.v harness/main.cpp \
  --Mdir obj_dir

echo ""
echo "== Pokretanje ko-simulacije =="
./obj_dir/Vdma_controller
