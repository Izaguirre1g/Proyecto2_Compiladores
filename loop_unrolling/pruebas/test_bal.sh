#!/bin/sh
# Pruebas de extremo a extremo: compila cada .bal con `balc --unroll` y
# revisa la decision que tomo el loop unrolling. Ejecutar desde la raiz.
set -u
dir=loop_unrolling/pruebas
fallos=0

esperar() { # archivo, texto esperado en la salida
    if ./balc --unroll "$dir/$1" 2>&1 | grep -qF -- "$2"; then
        echo "  ok   $1: $2"
    else
        echo "  FALLO $1: no aparece '$2'"; fallos=$((fallos + 1))
    fi
}

echo "[balc --unroll]"
esperar 01_n_constante.bal           "✔ TOTAL: 5 copias, sin saltos"
esperar 01_n_constante.bal           "Ciclos: 1  ·  total 1  ·  parcial 0  ·  sin cambios 0"
esperar 02_variable_pequeno.bal      "✔ TOTAL: 3 copias, sin saltos"
esperar 02_variable_pequeno.bal      "✔ PARCIAL ×4 + ciclo residuo"
esperar 03_variable_grande.bal       "✔ PARCIAL ×4: 250 vueltas + 2 sueltas"
esperar 04_condicional_y_llamada.bal "✔ TOTAL: 4 copias, sin saltos"
esperar 05_no_se_desenrolla.bal      "✘ sin cambios: el cuerpo modifica el iterador"
esperar 05_no_se_desenrolla.bal      "✘ sin cambios: el cuerpo modifica el limite"
esperar 05_no_se_desenrolla.bal      "total 0  ·  parcial 0  ·  sin cambios 3"

# Factor configurable y validacion de la bandera.
if ./balc --unroll=8 "$dir/03_variable_grande.bal" | grep -qF "PARCIAL ×8"; then
    echo "  ok   --unroll=8 usa factor 8"
else echo "  FALLO --unroll=8"; fallos=$((fallos + 1)); fi
if ./balc --unroll=1 "$dir/01_n_constante.bal" >/dev/null 2>&1; then
    echo "  FALLO --unroll=1 deberia rechazarse"; fallos=$((fallos + 1))
else echo "  ok   --unroll=1 se rechaza"; fi

[ "$fallos" -eq 0 ] && echo "OK: pruebas .bal de loop unrolling" || { echo "$fallos fallos"; exit 1; }
