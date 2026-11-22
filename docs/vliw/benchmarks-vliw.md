# Benchmarks VLIW por lotes (B2)

## A. Objetivo

B2 valida manifiestos, coordina comparaciones públicas B1 y organiza sus
resultados. B1 decide equivalencia y reutiliza M2/M1 para ejecutar y medir.
B2 no carga IMEM, decodifica ISA, compara GPR/DMEM, calcula IPC ni vuelve a
calcular diferencias por caso. Tampoco compila programas ni crea optimizaciones.
Solo requiere Python 3 y su biblioteca estándar, más el ejecutable B1 existente.

## B. Arquitectura

Manifiesto JSON → validación B2 → proceso B1 por caso → sesiones M2 →
simulador S1–S6 y métricas M1 → JSON B1 → validación y agregación B2 → JSON/CSV.

Toda la entrada se valida antes de invocar el primer comparador. La ejecución
es secuencial, sin shell, con lista de argumentos y stdin cerrado. Cada caso
usa un proceso nuevo. No se interpreta ningún comando, expresión, import ni
opción de compilación desde el manifiesto.

Los estados EQUIVALENTE, DIFERENTE, INCONCLUSO y ERROR se conservan de B1.
ERROR_DE_INFRAESTRUCTURA identifica timeout, invocación fallida, salida excesiva,
JSON inválido/incompleto, contrato distinto, cambio de entradas o desacuerdo
entre código y documento. Código B1 2/3 no significa fallo de infraestructura.
Código 1 con JSON ERROR válido se conserva como ERROR; sin un JSON válido
no se adivina si falló carga, construcción o generación: se registra infraestructura.

## C. Manifiesto v1

```json
{
  "version": 1,
  "casos": [{
    "id": "raw-demo",
    "categoria": "demostracion",
    "descripcion": "Comparación RAW generada con fixtures S7/S8-A",
    "referencia": {"formato": "balb", "ruta": "conservador.balb"},
    "candidato": {"formato": "balb", "ruta": "calendarizado.balb"},
    "modo": "finita",
    "presupuesto": 100,
    "procedencia": {"nivel_referencia": "-O0", "nivel_candidato": "-O2"}
  }]
}
```

El ejemplo describe el formato; no afirma que existan esos archivos ni que
los niveles hayan sido usados para compilarlos. `procedencia` es opcional:
permite `fuente_bal`, `nivel_referencia` y `nivel_candidato` (-O0/-O1/-O2).
Una fuente declarada debe existir y recibe hash, pero nunca se ejecuta ni se
considera prueba del origen de las imágenes. `verificada=false` en el reporte.

Campos obligatorios: id, categoria, referencia, candidato, modo, presupuesto.
Descripcion y procedencia son opcionales. ID debe ser único y no vacío; se
conserva como dato y nunca se usa para construir comandos o archivos de salida.
Formatos mem/balb; modos finita/fisica; presupuesto entero JSON real entre
0 y 18446744073709551615. Bool, float y strings no son enteros aceptados.

Categorías: loop_unrolling, renombramiento, dce, saltos_funciones y spilling;
demostracion se informa aparte y no cubre los quince requisitos oficiales.
Un manifiesto con casos=[] es válido para trabajo parcial: informa cero
mediciones y quince faltantes, nunca cobertura completa.

Las rutas de imágenes y fuente son absolutas o relativas al directorio del
manifiesto, independientemente del cwd. Se resuelven symlinks, se requieren
archivos regulares existentes y se registran SHA-256. No se copian programas
ni se modifican entradas. El ejecutable y los destinos relativos se resuelven
desde el cwd del usuario, porque son opciones de invocación, no del manifiesto.

Se rechazan UTF-8 inválido, strings con NUL o surrogates, claves duplicadas,
NaN/Infinity y desbordamiento numérico JSON, campos desconocidos, tipos erróneos,
versiones incompatibles y estructuras recursivas excesivas. No se ignoran
categorías o propiedades ambiguas para hacer pasar una entrada corrupta.

Límites de recursos:

| Recurso | Límite |
|---|---:|
| Manifiesto | 2 MiB |
| Casos | 256 |
| ID | 96 caracteres |
| Descripción/ruta | 4096 caracteres |
| Imagen, metadato fuente o ejecutable | Archivo regular de hasta 64 MiB |
| stdout B1 por caso | 1 MiB |
| stderr B1 por caso | 64 KiB |
| Evidencia acumulada stdout+stderr | 16 MiB |
| Timeout por proceso | >0 y ≤3600 segundos; valor predeterminado 30 |

La lectura de hashes también verifica crecimiento sobre 64 MiB. El límite
acumulado reserva capacidad para otro caso completo antes de intentarlo;
si no queda capacidad, los casos restantes reciben infraestructura con
intentado=false. No se afirma que el lote completo haya sido ejecutado.
Estos límites admiten holgadamente quince imágenes de la arquitectura actual.

## D. Ejecución reproducible

Construir B1 una vez desde la raíz de Compiladores:

```bash
b2dir=$(mktemp -d /tmp/ce1108-b2.XXXXXX)
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  tools/vliw/comparar_vliw.c \
  src/Simulador_VLIW/sim_{comparador,ejecucion,metricas,pipeline,alu,lsu,bru,crypto,decode,cargador}.c \
  -o "$b2dir/comparar_vliw"
python3 tools/vliw/benchmarks_vliw.py \
  --manifest /ruta/manifiesto.json --comparador "$b2dir/comparar_vliw" \
  --json "$b2dir/lote.json" --csv "$b2dir/lote.csv" --timeout 30
```

No se compila B1 por caso ni se modifica Makefile. Ambos destinos son
obligatorios: no se escriben reportes en el repositorio por defecto.
`--exigir-cobertura` requiere exactamente tres entradas válidas registradas
por cada categoría oficial; no convierte registro en equivalencia. Las
comparaciones diferentes, inconclusas o fallidas mantienen su propio código
no exitoso aun cuando estén registradas las quince entradas.

Prioridad de salida, de mayor a menor tras validar argumentos/manifiesto:

| Código | Significado |
|---|---|
| 64 | Argumentos incorrectos, opción desconocida o timeout inválido. |
| 4 | Manifiesto inválido; no se ejecutan comparaciones ni se publica reporte nuevo. |
| 1 | Infraestructura/preparación/publicación fallida o algún caso de infraestructura. |
| 6 | Algún ERROR estructurado de B1. |
| 5 | Cobertura registrada incompleta/excedida cuando se exige. |
| 2 | Algún DIFERENTE, sin problemas anteriores de mayor prioridad. |
| 3 | Algún INCONCLUSO, sin problemas anteriores de mayor prioridad. |
| 0 | Casos sin incidencias (o lista vacía sin exigir cobertura). |

No se oculta ERROR por la presencia de un caso equivalente. Tras un error
individual se continúa con los siguientes casos. Un comparador inexistente
no permite iniciar el lote y retorna infraestructura global, sin reporte
nuevo. El usuario debe comprobar el código antes de consumir reportes previos.

El timeout mide tiempo real del anfitrión, no ciclos VLIW. Al vencer, B2
termina el grupo de procesos y recoge al proceso directo. También espera
con timeout si B1 cerró sus pipes antes de finalizar. Los descendientes se
terminan; su eventual recolección como huérfanos corresponde al reaper del
sistema. La implementación de grupos/señales está dirigida al entorno POSIX.

## E. Reportes JSON y CSV

El JSON B2 tiene versión 1, contrato estricto-publico, manifiesto (ruta/hash),
comparador (ruta/hash), timeout, cobertura, resumen, categorias, casos y
limitaciones. Conserva el orden de los casos del manifiesto; claves ordenadas,
sin timestamps ni mediciones variables de tiempo de pared.

Cada caso incluye parámetros, rutas/hash, procedencia declarada, comando
exacto, resultado, intentado/proceso_iniciado, diagnóstico y rendimiento_valido.
`b1` conserva el documento completo validado una sola vez. Su clasificación,
campos y valores no se cambian. Se valida el esquema real v1 y su consistencia
con el retorno y el nombre de las imágenes; no se reconstruyen sus cálculos.

El diagnóstico conserva retorno, stderr y SHA-256 de stdout. Ante una salida
no validada conserva sus bytes capturados en base64, incluso con UTF-8 corrupto;
los prefijos se limitan según la tabla. `b1=null` impide tratar esa respuesta
como resultado arquitectónico. Las métricas null siguen siendo null, no cero.
Hashes se comparan antes/después de cada B1 para detectar cambios de imágenes
o ejecutable. Esto no es un bloqueo transaccional contra modificaciones
concurrentes que cambien y restauren un archivo entre ambas lecturas.

CSV usa csv.DictWriter con UTF-8 y una fila por caso. Incluye identificación,
clasificación/motivo, métricas y diferencias B1, rutas/hash, procedencia declarada,
descripción y bandera de rendimiento válido. Ausencias se escriben vacías.
El signo de deltas uint64 se presenta como número Python exacto, sin convertir
a int64 ni recalcular candidato-referencia. Los textos que pueden interpretarse
como fórmulas reciben un apóstrofo; números como -5 no se alteran. JSON conserva
los textos originales, incluidos los niveles declarados -O0/-O2.

Ambos contenidos se generan antes de publicar y se preparan en temporales
del mismo directorio destino. fsync y os.replace publican cada archivo
individualmente. No existe atomicidad conjunta JSON/CSV: si falla el segundo
replace puede existir JSON nuevo y CSV anterior. Se retorna 1 y se enumeran
en stderr los destinos ya publicados; temporales se eliminan en la limpieza.

Se rechazan destinos coincidentes o alias de entradas (manifiesto, comparador,
imágenes y fuente declarada), incluidos symlinks/hardlinks existentes. Los
directorios deben existir. Una falla de validación/preparación conserva los
reportes previos; no los presenta como resultados de la ejecución nueva.

## F. Cobertura oficial

Cada categoría oficial informa registrados, intentados, comparables finales,
conteos por resultado, faltantes=max(0,3-registrados) y excedentes. Demostracion
no tiene cuota oficial y se presenta como categoría separada.

Registrado significa entrada validada en el manifiesto; ejecutado/intentado
significa que se intentó invocar B1 (también puede fallar exec); proceso_iniciado
indica que sí se creó el proceso. Comparable significa EQUIVALENTE o DIFERENTE
con comparación final completa; disponer de métricas parciales no basta.
Equivalente estricto exige certificación completa de B1.

completa_registrada requiere exactamente tres casos por categoría oficial;
completa_estricta requiere además los tres equivalentes de cada categoría.
No se confunden con cobertura semántica de optimizaciones. Los nombres -O2,
metadatos de origen o la categoría no prueban qué transformación se aplicó.
Los reportes reales de demostración de B2 tienen cero casos oficiales; no se
crean programas BAL para completar las cuotas del equipo.

## G. Métricas y agregación

El resumen global incluye todas las categorías, también demostraciones, pero
estas nunca aumentan cobertura oficial. Cada categoría tiene su resumen propio.
Solo EQUIVALENTE, con ambas ejecuciones TERMINADO y documento coherente B1,
participa en rendimiento validado. Los demás resultados figuran en conteos y
pueden mostrar métricas diagnósticas, sin conclusiones de optimización correcta.

Se agrupa rendimiento por modo FINITA/FISICA para no mezclar políticas. Se
suman ciclos referencia/candidato y deltas ya declarados por B1. La reducción
porcentual agregada es -100×suma_delta/suma_referencia; referencia cero produce
null. Sin casos elegibles las sumas son null. Python mantiene sumas enteras
exactas aunque excedan uint64_t. IPC solo se presenta por ejecución: no se
promedian IPC, ni se lo interpreta como speedup o tiempo de pared.

## H. Limitaciones

B1 compara 32 registros físicos; una diferencia por renombramiento no demuestra
por sí sola un defecto semántico del compilador. B2 conserva esa clasificación
y no agrega mapeos de registros, liveness, DCE, IR ni interpretación BAL.
Tampoco certifica estado privado de la bóveda, futuras cachés, tiempo de
compilación, tamaño ejecutable ni estadísticas gráficas del IDE. El tamaño
de un MEM de texto no equivale al tamaño del código generado.

Los procesos C son B1; Python no está instrumentado por ASan. La selección
del ejecutable mediante --comparador es responsabilidad del usuario y su hash
queda registrado; B2 no certifica la identidad semántica de un binario ajeno
que imite el protocolo B1. Los fixtures adversariales prueban validación, no
se usan como evidencia de optimizaciones reales.

## I. Pruebas y evidencia

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_benchmarks_vliw.py
B2_ARTEFACTOS="$b2dir/demo" B2_EVIDENCIA="$b2dir/pruebas.json" \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_benchmarks_vliw.py
python3 tools/vliw/benchmarks_vliw.py \
  --manifest "$b2dir/demo/manifiesto.json" --comparador "$b2dir/demo/comparar_vliw" \
  --json "$b2dir/demo/repetido.json" --csv "$b2dir/demo/repetido.csv" --timeout 30
# El lote mixto retorna 2: contiene DIFERENTE e INCONCLUSO válidos.
```

La suite compila B1 una vez por proceso de pruebas, CB y el puente S7/S8-A.
El RAW proviene de esos fixtures aprobados: 12/7 ciclos, tres instrucciones,
IPC 0.25/3÷7, EQUIVALENTE y delta -5. En el lote mixto solo ese caso participa
en la suma validada. La prueba de quince entradas con tres por categoría usa
NOP repetido/respuestas controladas y verifica únicamente conteo; no prueba
quince optimizaciones. No se cambian fixtures anteriores.

```bash
CC='cc -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie -no-pie' \
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_benchmarks_vliw.py
```

Aquí se instrumentan los procesos C construidos (B1/CB/puente S7), no Python.
Se ejecuta también la suite aprobada B1 y las regresiones completas. LSan se
intenta por separado; bajo ptrace se informa NO VERIFICADO. Las evidencias,
reportes, manifiestos de protección y diff quedan fuera de los repositorios.

## Observación de descendientes en las pruebas

La cancelación POSIX conserva el contrato: se envía SIGKILL al grupo y se espera
al proceso directo. La entrega al descendiente es asíncrona; un retorno de
wait del padre no demuestra que el huérfano ya recibió su señal. La prueba
registra PID y tiempo de arranque antes de cancelar y observa, con plazo
monotónico de un segundo, desaparición, zombi o reemplazo de esa identidad.
Nunca acepta R/S persistentes ni señala un PID reutilizado. Los intervalos
de sondeo de hasta cinco milisegundos evitan espera ocupada; no reemplazan
la condición de terminación ni amplían el timeout de ejecución del lote.

Los presupuestos positivos y los timeouts adversariales anteriores permanecen
intactos. Se añaden oráculos de entrega asíncrona, proceso activo al vencer
el plazo, identidad reutilizada y pipes heredados después de terminar el padre.
La recolección del huérfano corresponde al reaper; un zombi no ejecuta código.
La repetibilidad y los resultados reales se registran fuera del repositorio.
