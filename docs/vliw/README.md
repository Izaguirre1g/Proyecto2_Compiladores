# Contribución VLIW — Simulador, calendarización y validación

Esta carpeta es la entrada a los bloques S1–S8-C, M1/M2 y B1–B5. La reorganización
R1 conserva algoritmos, ISA, APIs, métricas y formatos; no integra todavía el
scheduler al backend BAL ni implementa niveles `-O0`, `-O1` o `-O2`.

## Organización y responsabilidades

| Ubicación | Responsabilidad |
|---|---|
| `src/Simulador_VLIW/` | Cargador MEM/BALB, decodificador, ALU/LSU/BRU/cripto, pipeline, métricas, sesiones y comparador. |
| `src/Calendarizacion_VLIW/` | S7 planifica metadatos; S8-A emite ASM; S8-C produce tabla, CSV, JSON y estadísticas. |
| `include/` | Las trece cabeceras públicas conservan nombres, firmas, estructuras y enumeraciones. |
| `tests/vliw/` | Las dieciséis suites Python y sus pruebas C; incluye escenarios normales, negativos y diferenciales. |
| `tests/vliw/rtl/` | Cinco bancos propios contra el procesador de referencia; no contienen sustitutos del RTL. |
| `tools/vliw/` | CLIs M2/B1, B2–B5, sondas y validación reproducible. |
| `docs/vliw/` | Contratos técnicos, comandos y limitaciones de cada bloque. |

Las fuentes compartidas permanecen con sus dueños. `rutas_vliw.py` permite que las
pruebas encuentren el codificador en el árbol plano de esta rama o en
`src/Codificacion_Binaria/` de develop. Rechaza ausencia y copias ambiguas.
No se reemplaza el Makefile compartido ni se copian frontend, IR o codificador.

## Dependencias y validación reproducible

Se requieren Python 3, compilador C11, make, bison y flex. El contraste RTL necesita
Icarus (`iverilog`, `vvp`) y una referencia de Arquitectura compatible.
`rutas_vliw.py` localiza `rtl/top.sv` en el Proyecto I o `rtl-p2/top.sv` en el
árbol reorganizado y agrega los includes/fuentes de sus dependencias. Los cinco
bancos y sus expectativas se conservan; no se modifica ni fabrica RTL. La
reorganización se verifica con ambas ubicaciones y el directorio puede indicarse
explícitamente.

Desde la raíz, o usando la ruta absoluta del script desde otro directorio:

```bash
python3 tools/vliw/validar_vliw.py --rapida
python3 tools/vliw/validar_vliw.py --arquitectura /ruta/a/referencia-proyecto-i
python3 tools/vliw/validar_vliw.py --arquitectura /ruta/a/referencia-proyecto-i \
  --sanitizador address,undefined --evidencia /tmp/vliw-sanitizadores.json
python3 tools/vliw/validar_vliw.py --arquitectura /ruta/a/referencia-proyecto-i \
  --sanitizador address --fugas
```

El script copia fuentes a un temporal, excluye ejecutables antiguos, reconstruye
balc y ejecuta las suites reales. La modalidad completa normal también ejecuta
`make -B -k test`. Con sanitizadores, instrumenta bibliotecas y sondas propias
compiladas por cada suite; Python y el balc reconstruido previamente no se
presentan como instrumentados. La modalidad rápida ejecuta S1, S7 y S8-C y no
sustituye las demás suites. Los fallos propagan código 1 y las omisiones código 2;
un SKIP no acredita contraste RTL. La evidencia opcional debe quedar fuera del
repositorio. No se generan binarios ni parser/lexer en el árbol original.

Para una suite individual, ejecutar **en una copia externa**:

```bash
ARQUITECTURA_ROOT=/ruta/a/referencia-proyecto-i \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_sim_pipeline.py -v
PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_vliw_scheduler.py -v
PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_vliw_visualizador.py -v
```

Los tests individuales pueden llamar al Makefile de su copia; la opción recomendada
para proteger el árbol de trabajo es el validador temporal.

## Compilar herramientas y ejecutar el simulador

Los siguientes comandos se ejecutan desde la raíz de una **copia externa** y
escriben los ejecutables/resultados en un directorio temporal nuevo:

```bash
salida=$(mktemp -d)
flags=(-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude)
sim=(src/Simulador_VLIW/sim_{ejecucion,metricas,pipeline,alu,lsu,bru,crypto,decode,cargador}.c)
cc "${flags[@]}" tools/vliw/sim_vliw_metricas.c "${sim[@]}" -o "$salida/sim-vliw"
cc "${flags[@]}" tools/vliw/comparar_vliw.c src/Simulador_VLIW/sim_comparador.c \
  "${sim[@]}" -o "$salida/comparar-vliw"
make -B balc
./balc -o "$salida/factorial.bin" examples/factorial.bal
"$salida/sim-vliw" balb "$salida/factorial.bin" fisica 10000 json > "$salida/metricas.json"
"$salida/comparar-vliw" balb "$salida/factorial.bin" balb "$salida/factorial.bin" \
  fisica 10000 json > "$salida/comparacion.json"
```

M2 recibe formato `mem`/`balb`, búsqueda `finita`/`fisica`, presupuesto y reporte
`texto`/`json`. Los programas BAL conservan su bucle final: agotar el presupuesto
no demuestra terminación semántica (la CLI M2 devuelve 2). B1 puede declarar
INCONCLUSO (código 3) incluso comparando
un programa consigo mismo. No se cambia esa conclusión para convertirla en PASS.

## Mostrar una calendarización

S7 recibe operaciones, máscaras RAW/WAR/WAW, efectos, bloques, CFG y dependencias
proporcionadas por otro productor. No analiza BAL directamente. Se puede compilar
el puente **de prueba** existente para visualizar esos metadatos:

```bash
cc "${flags[@]}" tests/vliw/test_vliw_visualizador.c \
  src/Calendarizacion_VLIW/vliw_{scheduler,visualizador}.c -o "$salida/visualizador"
python3 - "$salida/raw.txt" <<'PY'
from pathlib import Path
import sys
p = '2 1 1 0\n0 0 0 2 0 0 0\n1 0 2 4 0 0 0\n0 1 0 3\n0 2 0\n'
p += 'sumai x1,x0,1'.encode().hex() + '\n'
p += 'sumai x2,x1,1'.encode().hex() + '\n'
Path(sys.argv[1]).write_text(p)
PY
"$salida/visualizador" --tabla "$salida/raw.txt"
"$salida/visualizador" --csv "$salida/raw.txt" > "$salida/raw.csv"
"$salida/visualizador" --json "$salida/raw.txt" > "$salida/raw.json"
```

El ejemplo tiene un productor y un consumidor ALU con distancia RAW tres. La
protección N+3 obliga a insertar bundles vacíos. La sonda no es una nueva CLI de
producción ni una integración con la IR.

## B1–B5 y formatos

| Bloque | Entrada / salida | Documento y pruebas |
|---|---|---|
| S1–S6 | MEM/BALB → ejecución IF/ID/EX/WB, estado público, GPR/DMEM y excepciones. | [Pipeline](simulador-pipeline.md), documentos LSU/BRU/cripto/cargador/decode. |
| S7 / S8-A / S8-C | Metadatos → plan → ASM o vistas tabla/CSV/JSON. | [Scheduler](calendarizador-vliw.md), [emisor](emisor-vliw.md), [visualizador](visualizador-vliw.md). |
| M1 / M2 | Observaciones por flanco / sesiones → métricas texto/JSON. | [M1](metricas-simulador.md), [M2](integracion-simulador-metricas.md). |
| B1 | Dos imágenes y presupuesto → equivalencia, diferencia, inconcluso o error. | [Comparador](comparador-vliw.md), `test_sim_comparador.py`. |
| B2 | Manifiesto JSON y B1 → lotes y reportes JSON/CSV. | [Benchmarks](benchmarks-vliw.md), `test_benchmarks_vliw.py`. |
| B3 | Fuentes BAL, balc y M2 → artefactos/trazabilidad del pipeline. | [Validación BAL](validacion-pipeline-bal.md), `test_validar_pipeline_bal.py`. |
| B4 | Evidencia estática/dinámica → análisis de finalización y primer salto final. | [Contrato final](contrato-finalizacion-bal.md), `test_analizar_fin_bal.py`. |
| B5 | Evidencia B4 → corte candidato, captura y contraste M2. | [Corte arquitectónico](corte-arquitectonico-bal.md), `test_analizar_corte_bal.py`. |

B2–B5 conservan sus argumentos públicos y muestran las opciones con `--help`:

```bash
python3 tools/vliw/benchmarks_vliw.py --help
python3 tools/vliw/validar_pipeline_bal.py --help
python3 tools/vliw/analizar_fin_bal.py --help
python3 tools/vliw/analizar_corte_bal.py --help
```

Las suites B3/B4/B5 reconstruyen sus sondas, ejecutan factorial/whale/puntajes y
verifican artefactos. Las variables `B3_BALC`, `B4_BALC`, `B5_BALC` admiten un balc
reconstruido externamente; `B*_ARTEFACTOS` y `B*_EVIDENCIA` permiten conservar
capturas fuera del repositorio. Los comandos detallados de cada documento usan
las rutas nuevas. B5 conserva ciclos históricos 3403/247/2748 cuando los comprueba
la ejecución real: esos números no se fuerzan en la herramienta.

## Límites e integración pendiente

El backend debe producir `VliwEntrada`, dependencias y CFG completos y verificables
para invocar S7. No basta una lista sin aristas para afirmar independencia.
S8-A necesita destinos coherentes y S8-C no repara un plan inválido. Las APIs son
bibliotecas separadas; el lowering/IR y la integración BAL→IR→scheduler pertenecen
a trabajo posterior de sus responsables. R1 no implementa I1-A ni optimizaciones.

B5 mantiene el pipeline ACTIVO y un **corte candidato**, no una certificación del
retorno semántico de main. Comparación M2 y métricas consistentes no prueban por
sí solas equivalencia universal de optimizaciones. La futura integración debe
conservar el Makefile actual de develop y revisar la referencia RTL; esta
reorganización no fusiona ramas ni publica cambios.
