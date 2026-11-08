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
esperar 01_n_constante.bal          "5 vueltas constantes -> desenrollado TOTAL"
esperar 01_n_constante.bal          "1 ciclos, 1 totales, 0 parciales"
esperar 02_variable_pequeno.bal     "3 vueltas constantes -> desenrollado TOTAL"
esperar 02_variable_pequeno.bal     "vueltas desconocidas -> desenrollado PARCIAL factor 4 + ciclo residuo"
esperar 03_variable_grande.bal      "1002 vueltas constantes -> desenrollado PARCIAL factor 4"
esperar 04_condicional_y_llamada.bal "4 vueltas constantes -> desenrollado TOTAL"
esperar 05_no_se_desenrolla.bal     "no se desenrolla: el cuerpo modifica el iterador"
esperar 05_no_se_desenrolla.bal     "no se desenrolla: el cuerpo modifica el limite"
esperar 05_no_se_desenrolla.bal     "0 totales, 0 parciales, 3 sin cambios"

# Factor configurable y validacion de la bandera.
if ./balc --unroll=8 "$dir/03_variable_grande.bal" | grep -qF "PARCIAL factor 8"; then
    echo "  ok   --unroll=8 usa factor 8"
else echo "  FALLO --unroll=8"; fallos=$((fallos + 1)); fi
if ./balc --unroll=1 "$dir/01_n_constante.bal" >/dev/null 2>&1; then
    echo "  FALLO --unroll=1 deberia rechazarse"; fallos=$((fallos + 1))
else echo "  ok   --unroll=1 se rechaza"; fi

[ "$fallos" -eq 0 ] && echo "OK: pruebas .bal de loop unrolling" || { echo "$fallos fallos"; exit 1; }
