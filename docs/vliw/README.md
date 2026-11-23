# Contribución VLIW — Simulador, calendarización y validación

Esta carpeta es la entrada a los bloques S1–S8-C, M1/M2 y B1–B5 y a la
integración productiva O0/O1/O2. PR #70 y #71 están fusionados; el estado vigente
se describe en [entrega de Javier](entrega-final-javier.md) e
[integración productiva O2](integracion-productiva-o2.md).

O0 conserva el backend tradicional; O1 aplica DCE/reescritura BAL de José;
O2 baja la IR optimizada al ISA, construye CFG/DAG y usa S7/verificador,
S8-A/CB y enlace reales para el subconjunto admitido. No admite todo BAL.
R1 fue una reorganización anterior: su falta de conexión productiva no describe
el estado vigente.

## Resultados identificados por campaña

La auditoría externa del 17/11/2026 sobre Compiladores `13497a9` y Arquitectura
`192050e` registró **26 suites / 460 métodos** por modalidad normal, ASan,
UBSan, ASan+UBSan y LSan, sin FAIL ni SKIP; quince programas correctos en los
tres niveles y cien contrastes RTL (90 oficiales y diez dirigidos, 20 programas
únicos, modos de memoria 0/2, L2_MODEL=0). Es la campaña histórica
`evidencias-cierre-definitivo-javier-p2`, no una ejecución de este parche.
La corrección F-01 amplía la suite de niveles; sus nuevos resultados se guardan
en `evidencias-javier-cierre-correcciones`, sin sumar métodos de versiones distintas.

Campaña nueva F-01: **26 suites / 470 métodos por modalidad**, normal, ASan,
UBSan, ASan+UBSan y LSan, FAIL=0 y SKIP=0. `make -B balc` y
`make -B -k test` devuelven 0. La suite de niveles contiene 23 métodos y
44 subcasos negativos con hashes conservados; las pruebas adicionales no
se suman de nuevo al total. Valgrind ejecutó doce rutas (una válida y once
rechazos), cero errores y cero bytes/bloques pendientes. La prueba O2/M2
externa conserva el valor esperado 7 y coherencia ASM/BIN/MEM. No se repitió
la campaña extensa de cien contrastes RTL; sí las suites con RTL aplicables.

## Organización y responsabilidades

| Ubicación | Responsabilidad |
|---|---|
| `src/Simulador_VLIW/` | Cargador MEM/BALB, decodificador, ALU/LSU/BRU/cripto, pipeline, métricas, sesiones y comparador. |
| `src/Calendarizacion_VLIW/` | S7 planifica metadatos; S8-A emite ASM; S8-C produce tabla, CSV, JSON y estadísticas. |
| `include/` | Contratos públicos del simulador, scheduler, metadatos, CFG/DAG e integración. |
| `tests/vliw/` | Suites Python y pruebas C; escenarios normales, negativos, diferenciales e integración productiva. |
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

El productor O2 proporciona operaciones físicas, máscaras, efectos, bloques,
CFG y dependencias acreditados a S7. El selector recorre las listas por orden
original determinista; el camino crítico informado por el DAG no cambia esa
política. En una copia externa, la vista productiva se obtiene con:

```bash
make -B balc
./balc -O0 caso.bal
./balc -O1 caso.bal
./balc -O2 -o resultado.bin caso.bal
./balc -O2 -p caso.bal
```

Se publican ASM, BIN, MEM, `.o2.json` y tres IR; el preflight O2 comprueba las
siete rutas entre sí y contra la fuente, incluyendo alias de ruta e identidad.
Por ejemplo, `-O2 -o caso.o2.json caso.bal` debe rechazar antes de escribir.
No hay transacción multiarquivo frente a fallos posteriores de E/S ni bloqueo
de modificaciones concurrentes del filesystem.

El puente **de prueba** anterior sigue disponible para visualizar metadatos
explícitos sin pasar por BAL:

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

## Límites vigentes

El productor debe proporcionar `VliwEntrada`, dependencias y CFG completos;
una lista sin aristas no demuestra independencia. S8-A necesita destinos
coherentes y S8-C no repara un plan inválido. O2 ya conecta esa cadena para el
subconjunto del [contrato productivo](integracion-productiva-o2.md); módulos,
listas/texto, arrays dinámicos y otras características allí enumeradas se
rechazan explícitamente. No se ofrece protección dinámica nueva contra
desbordamiento de pila ni mejora universal de rendimiento.

B5 conserva su corte candidato. La sonda O2 puede acreditar retorno de main,
pero el pipeline sigue ACTIVO y B1/B2 mantiene INCONCLUSO; resultados BAL y
contrastes M2/RTL no constituyen equivalencia universal entre optimizaciones.
La selección literal de tres DCE con llamadas con efectos requiere revisión
de José/equipo. ABI/lowering, pasadas, documentación formal, gráficas y defensa
conservan las responsabilidades compartidas descritas en la guía de entrega.
La bitácora real de horas es individual y no se obtiene de tiempos de pruebas.
