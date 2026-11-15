#!/bin/sh
# Smoke test de la CLI real. Requiere balc compilado con Flex, Bison y uthash.
set -eu
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM
cp examples/vliw_ir_preview.bal "$TMP_DIR/mini.bal"
cp examples/factorial.bal "$TMP_DIR/factorial.bal"
./balc --vliw-ir=3 -s -p "$TMP_DIR/mini.bal" > "$TMP_DIR/preview.log" 2> "$TMP_DIR/error.log"
test -s "$TMP_DIR/mini.vliw.asm"
test -s "$TMP_DIR/mini.vliw.mem"
grep -q '\[VLIW IR\]' "$TMP_DIR/preview.log"
grep -q 'NO es un ejecutable autocontenido' "$TMP_DIR/preview.log"
test ! -e "$TMP_DIR/mini.bin"
# Opciones incompatibles y casos fuera del subset se rechazan sin salida.
if ./balc --vliw-ir=2 "$TMP_DIR/mini.bal" > "$TMP_DIR/invalid.log" 2>&1; then
  echo '--vliw-ir=2 debio fallar' >&2; exit 1
fi
if ./balc --vliw-ir -o "$TMP_DIR/incorrecto.bin" "$TMP_DIR/mini.bal" > "$TMP_DIR/invalid.log" 2>&1; then
  echo '--vliw-ir -o debio fallar' >&2; exit 1
fi
if ./balc --vliw-ir -s "$TMP_DIR/factorial.bal" > "$TMP_DIR/invalid.log" 2>&1; then
  echo 'factorial con variables todavia no soportadas debio fallar' >&2; exit 1
fi
# No basta con devolver error: debe proceder del productor fisico y ser claro.
grep -q 'productor fisico rechazado' "$TMP_DIR/invalid.log"
test ! -e "$TMP_DIR/factorial.vliw.asm"
test ! -e "$TMP_DIR/factorial.vliw.mem"
# La via habitual no debe ser desviada a la productora experimental.
./balc "$TMP_DIR/mini.bal" > "$TMP_DIR/tradicional.log" 2> "$TMP_DIR/error.log"
test -s "$TMP_DIR/mini.bin"
echo 'OK CLI: VLIW IR explicito, artefactos acotados y backend tradicional intacto'
