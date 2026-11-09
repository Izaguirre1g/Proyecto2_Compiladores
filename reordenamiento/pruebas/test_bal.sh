#!/bin/sh
# Pruebas de extremo a extremo: compila cada .bal con `balc --reorder` y
# revisa el reporte. Ejecutar desde la raiz del repositorio.
set -u
dir=reordenamiento/pruebas
fallos=0

esperar() { # archivo, texto esperado, banderas extra (opcional)
    if ./balc ${3:-} --reorder "$dir/$1" 2>&1 | grep -qF -- "$2"; then
        echo "  ok   $1: $2"
    else
        echo "  FALLO $1: no aparece '$2'"; fallos=$((fallos + 1))
    fi
}

echo "[balc --reorder]"
esperar 01_war.bal              "Ciclos estimados                16        13  -3"
esperar 01_war.bal              "WAR 1 · WAW 1"
esperar 02_waw.bal              "Ciclos estimados                19        14  -5"
esperar 03_war_waw_cadena.bal   "RAW 17 · WAR 3 · WAW 2"
esperar 04_salto_condicional.bal "bloques reordenados: 2 de 4"
esperar 05_llamadas.bal         "efectos 23"
esperar 06_ciclo_desenrollado.bal "✔ PARCIAL ×4: 1 vueltas + 1 sueltas" "--unroll"
esperar 06_ciclo_desenrollado.bal "Ciclos estimados               161       128  -33" "--unroll"
esperar 06_ciclo_desenrollado.bal "REORDENAMIENTO SEGURO" "--unroll"

# Latencia configurable y validacion de la bandera.
if ./balc --reorder=1 "$dir/01_war.bal" | grep -qF "ningún bloque necesita menos ciclos"; then
    echo "  ok   --reorder=1 no mueve nada (sin esperas)"
else echo "  FALLO --reorder=1"; fallos=$((fallos + 1)); fi
if ./balc --reorder=0 "$dir/01_war.bal" >/dev/null 2>&1; then
    echo "  FALLO --reorder=0 deberia rechazarse"; fallos=$((fallos + 1))
else echo "  ok   --reorder=0 se rechaza"; fi

[ "$fallos" -eq 0 ] && echo "OK: pruebas .bal de reordenamiento" || { echo "$fallos fallos"; exit 1; }
