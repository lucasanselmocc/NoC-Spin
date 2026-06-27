#!/usr/bin/env bash
# scripts/run_tests.sh — compila e executa a simulação
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
SYSTEMC_HOME="${SYSTEMC_HOME:-/usr/local/systemc}"

echo "=== Spin NoC — Build & Run ==="
echo "    SystemC: $SYSTEMC_HOME"
echo "    Build:   $BUILD"
echo ""

# Compilação
mkdir -p "$BUILD"
cmake -S "$ROOT" -B "$BUILD" \
    -DSYSTEMC_HOME="$SYSTEMC_HOME" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    > "$BUILD/cmake.log" 2>&1

cmake --build "$BUILD" -- -j"$(nproc)" 2>&1 | tee "$BUILD/build.log"

echo ""
echo "=== Executando simulação ==="
"$BUILD/spin_noc_sim" --vcd "$@"

echo ""
echo "=== Pronto! ==="
echo "    VCD gerado em: $ROOT/sim/noc_trace.vcd"
echo "    Para visualizar: gtkwave $ROOT/sim/noc_trace.vcd"
