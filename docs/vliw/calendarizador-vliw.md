# S7: núcleo del calendarizador estático VLIW

## Objetivo, alcance y fuentes

S7 entrega una biblioteca C11 para asignar operaciones de bajo nivel a bundles
ALU/LSU/BRU/CRIPTO. No genera texto, MEM ni BALB, no cambia el backend y no
modifica `rs_resolver`. El enunciado oficial compartido
`proyecto-ii-grupal-perf-cache.pdf`, sección 4, página 8, exige calendarización,
dependencias, slots y NOP sin forwarding/scoreboarding. S7 implementa el núcleo;
la emisión definitiva y la integración con el compilador corresponden a S8.

Base verificada: `feat/simulador-vliw`, commit S6
`58a985e9a3f9e35fe8b29fb60a9b2913a5caab46`, árbol e índice inicialmente limpios.
Se revisaron `resolucion_saltos.h/c`, `codificacion_binaria.h/c`, interfaces
`sim_decode.h`, `sim_pipeline.h` y documentación de S3/S5/S6. El generador actual
emite una operación y dos NOP por instrucción; sigue intacto.

Arquitectura de referencia: `418c92bb73d3f34fb02f2feca384a83b86a589d9`.
Comportamientos consultados en el RTL original:

- `rtl/regfile.sv`: lecturas combinacionales sin bypass y escrituras al flanco;
  ID simultáneo a WB observa el valor anterior, de donde proviene N+3.
- `rtl/crypto_unit.sv`: operandos y permiso capturados ID, resultados/ESTADO
  publicados WB, bóveda escrita EX. AUTH/INIT exigen margen N+3.
- `rtl/top.sv`: excepción EX cancela todo el culpable y prevalece sobre BRU;
  el flush no cancela WB anterior. No se aprovecha ese flush como margen RAW.
- `rtl/isa_defs.sv`: cuatro slots, bundle de 16 bytes, IMEM de 1024 bundles y
  TRAP_VECTOR=0x3FF0. S7 reserva el último y admite solo 1023 ordinarios.

No se modificó Arquitectura. El máximo de memoria del enunciado no obliga a
ampliar la IMEM de esta implementación ni autoriza cambiar el contrato existente.

## Responsabilidades y frontera con IR

Dylan entrega operaciones ISA con registros físicos después del lowering y la
asignación de registros; Meibel aporta dependencias/transformaciones seguras.
S7 consume esos resultados, no genera IR, no asigna ni renombra registros y no
hace aliasing, liveness, spilling, DCE o unrolling.

`feature/ir` y el objeto `8995980...` no estaban disponibles localmente durante
esta implementación. No se descargaron ni incorporaron sus cambios. La API no
incluye `ir.h` y no supone que nombres como `$t1` sean registros físicos.
El flujo futuro es IR → registros físicos/lowering → metadatos/dependencias →
S7 → emisión y resolución de etiquetas S8. La unión entre esas fases sigue
pendiente; no existe un adaptador de producción en este bloque.

## Entrada, propiedad y contrato de metadatos

`VliwEntrada` referencia instrucciones, aristas y bloques; todos son prestados e
inmutables durante la llamada. Sus cantidades deben corresponder a memoria
accesible real. El texto es opcional y nunca se interpreta ni modifica.

Cada `VliwInstruccion` contiene:

| Campo | Significado |
|---|---|
| `id` | Identidad externa única; puede ser discontinua. |
| `slot` | 0 ALU, 1 LSU, 2 BRU, 3 CRIPTO. |
| `texto` | Cadena opcional prestada para uso posterior; S7 no la copia ni analiza. |
| `lee`, `escribe` | Bits de registros físicos, incluidos ambos destinos Feistel. |
| `efectos` | Memoria, bóveda, ESTADO, excepción, desconocidos o barrera explícita. |
| `control` | Ninguno, salto, llamada o retorno. |
| `bloque` | Índice de su bloque básico. |

Las máscaras utilizan contenedores uint64_t para detectar y rechazar bits x32
hasta x63. Solo bits0..31 son legales; el bit x0 se excluye al evaluar riesgos.
Se validan como máximo dos fuentes y uno o dos destinos efectivos según slot.
No se deduce un destino omitido ni se interpreta una escritura x0 como productor.

Las entradas representan operaciones reales no-NOP; los NOP se representan como
slots vacíos de salida. Si el productor entrega datos incompletos o miente sobre
una instrucción, S7 no puede descubrir su semántica analizando una cadena: esa
responsabilidad pertenece al lowering/decodificación. En particular, informar
solo uno de los destinos de un fsl no se puede detectar sin identificar su opcode.
El productor debe incluir ambos y clasificar correctamente los efectos.

`metadatos_completos=true` es obligatorio para programas no vacíos. Significa
que máscaras, efectos, CFG y restricciones están disponibles. No transforma un
grafo vacío en una prueba de independencia: el verificador revisa máscaras y
efectos antes de publicar el resultado. Si omitir una relación produce una
salida insegura, se devuelve METADATOS, sin inventar una arista para repararla.
Una salida que ya es segura por otras restricciones no requiere reconstruir
relaciones redundantes. No se exige una representación canónica del grafo.

El llamador conserva instrucciones/textos para resolver índices al emitir S8.
El plan solo almacena índices y posee sus arreglos, por lo que liberarlo no
necesita que las cadenas de entrada sigan vivas.

## Grafo de dependencias y bloques

`VliwDependencia` identifica posiciones del arreglo de entrada, no los `id`:

- RAW: distancia mínima 3 o mayor, con intersección escritura→lectura.
- WAR: distancia mínima 1 o mayor, con intersección lectura→escritura.
- WAW: distancia mínima 1 o mayor, con destinos superpuestos.
- Memoria, control y barrera: separación positiva indicada por la fase externa.

Las distancias son diferencias entre índices de bundles, no entre operaciones.
Las aristas explícitas no se eliminan ni se acortan, aunque parezcan redundantes.
Una RAW declarada solo sobre x0 se rechaza como inconsistente. Las aristas
repetidas se conservan y no alteran el resultado. Una separación >=1023 no cabe.

Kahn comprueba todo el DAG antes de emitir, incluidas componentes desconectadas.
Un ciclo devuelve CICLO sin salida parcial. Después se rechazan aristas acíclicas
contrarias al orden original (`antes >= despues`) y tipos incompatibles con
sus máscaras. El CFG sí admite ciclos y sucesores hacia bloques anteriores:
sus backedges no son aristas del DAG de una instancia estática de operaciones.

`VliwBloque` cubre un intervalo [inicio,fin). Los bloques deben ser no vacíos,
contiguos, sin solapamiento y cubrir todas las instrucciones en su orden original.
Las operaciones confirman su pertenencia. Los sucesores son índices válidos del
CFG externo. Sin transferencia se exige caída al bloque siguiente o salida al
final. Con BRU, el productor debe informar todas las rutas posibles; S7 no
interpreta destinos, condiciones ni texto para reconstruirlas.

Toda BRU es transferencia, debe terminar su bloque y ocupa un bundle propio.
Las llamadas requieren efectos desconocidos. No se especula a través de control
ni se mueve ninguna operación de un bloque a otro.

## API y errores

```c
VliwEstado vliw_planificar(const VliwEntrada *, VliwPlan **, VliwDiagnostico *);
VliwEstado vliw_verificar(const VliwEntrada *, const VliwPlan *, VliwDiagnostico *);
void vliw_liberar(VliwPlan **);
```

La salida debe apuntar inicialmente a NULL. La planificación se construye en
privado y solo se publica si supera el verificador. Los errores preservan la
salida previa; el diagnóstico opcional contiene código, índice de instrucción,
arista cuando corresponde y un mensaje estático en español.

Se distinguen ARGUMENTO, METADATOS, BLOQUES, DEPENDENCIA, CICLO, CAPACIDAD,
MEMORIA y PLAN_INVALIDO. Este último describe una planificación incorrecta
entregada al verificador; si aparece al revisar el resultado interno del
scheduler se traduce a METADATOS insuficientes y se descarta el plan.

Un programa vacío sin bloques ni aristas produce un plan vacío liberable.
`vliw_liberar(&plan)` limpia el puntero y admite llamadas repetidas y NULL.
No hay estado global mutable en la biblioteca ni referencias compartidas entre
planes. Las reservas comprueban tamaños antes de multiplicar; los índices se
validan antes de acceder. Como toda API C, no puede validar punteros colgantes
arbitrarios ni conocer el tamaño real de un arreglo cuya cantidad fue falsificada.

## Algoritmo determinista

1. Validar metadatos, bloques, aristas y capacidad teórica.
2. Representar las salidas del DAG en CSR y contar predecesores pendientes.
3. Detectar ciclos con Kahn y restaurar los grados de entrada para emitir.
4. Recorrer bloques en su orden, fijando el inicio lógico antes de su protección.
5. Por ciclo, considerar instrucciones pendientes en orden original.
6. Una operación está lista si no quedan predecesores, llegó su ciclo mínimo,
   su slot está libre y no cruza la primera barrera/BRU pendiente.
7. Al emitir un productor en C, actualizar cada sucesor a
   `minimo = max(minimo, C + distancia)` y descontar su predecesor.
8. Si no se puede emitir, conservar el bundle completamente vacío en IMEM.
9. Verificar el plan completo por índices, máscaras y efectos antes de publicarlo.

Dos ALU nunca comparten slot. ALU/LSU independientes sí pueden compartir ciclo.
Ningún par RAW/WAR/WAW comparte bundle, incluso si el RTL admitiría alguna
combinación. La prioridad física de los cinco puertos WB no justifica conflictos.
La selección no contiene aleatoriedad y no depende del orden del arreglo de
aristas. No calcula un camino crítico ni promete planificación global óptima.

El primer acceso de memoria pendiente es el único elegible de ese ciclo:
loads y stores conservan su orden aun si no hay información de aliasing. Se
calcula esa frontera una vez por ciclo, sin comparar direcciones ni fabricar
un grafo de aliasing. Los efectos desconocidos, bóveda, ESTADO, posibles
excepciones y barreras explícitas se emiten aislados y separados por al menos
tres bundles de todas las operaciones anteriores y posteriores. Toda CRIPTO se
trata así durante S7. Es conservador también para ell, cuya bóveda física se
publica antes, y para vcr, que no exige permiso previo.

Las transferencias ordinarias solo esperan a que terminen sus predecesores y
las operaciones previas; llamadas/retornos actúan además como barreras fuertes.
La distancia tres antes de una barrera permite que los resultados anteriores
se publiquen antes de su ID; después evita usar AUTH/INIT o GPR pendientes y
no adelanta efectos a través de una posible excepción.

La memoria de trabajo es O(N+E+B), con CSR, arreglos por instrucción y hasta
1023 bundles. La comprobación defensiva es O(N²). Los barridos de selección y
barreras dan una cota conservadora O(B·N+N²+E), con B<=1023 y N<=4092; la validación
también recorre los sucesores CFG suministrados. No se reserva una matriz de
adyacencia ni se hace un análisis de liveness. La medición de rendimiento es un
caso concreto, no una garantía de tiempo de pared para cualquier grafo.

## Protección entre bloques: argumento de seguridad

Cada bloque distinto de B0 comienza con dos bundles vacíos. B0 también los recibe
si cualquier sucesor del CFG apunta a él. El mapa `inicio_por_bloque` apunta al
primer NOP, nunca a la primera operación. S8 deberá dirigir todas las etiquetas,
entradas y saltos lógicos a ese inicio; saltarse la protección viola el contrato.

Sea P el último bundle ejecutado antes de entrar a un bloque y Q su primera
operación. En caída secuencial, los dos NOP ocupan P+1/P+2 y Q no puede aparecer
antes de P+3. Cualquier productor anterior a P tiene aún más margen. En una
transferencia tomada, ejecutar ambos NOP de entrada proporciona al menos ese
margen sin contar los ciclos adicionales de flush. Por tanto, GPR y AUTH/INIT
publicados en WB son visibles en ID de Q. WAR/WAW y los efectos anteriores
también conservan orden. La prueba se aplica a cada entrada, independientemente
del predecesor, incluyendo backedges y múltiples rutas.

Proteger únicamente bloques diferentes del inicial sería insuficiente como
regla general si un backedge apunta a B0. Por eso se consulta el CFG completo
y se protege también ese destino. No se agrega una arista cíclica al DAG ni
se supone que el flush reemplace N+3.

El vector de excepciones no pertenece al código ordinario. S7 no programa su
handler ni reubica traps. Los márgenes y bundles vacíos se incluyen en el límite
1023; no se trunca una planificación que no cabe.

## Salida y ejemplo

`VliwPlan` contiene bundles con cuatro índices o VLIW_VACIO, conteos de operaciones
por bundle, cantidad de instrucciones, cantidad de vacíos, mapa índice→bundle y
mapa bloque→inicio. Los bundles de protección también tienen bloque propietario.
No se codifica todavía la palabra binaria NOP ni se resuelven saltos.

```c
VliwInstruccion ins[] = {
    {.id=10, .slot=VLIW_SLOT_ALU, .texto="sumai x5,x0,7", .escribe=UINT64_C(1)<<5},
    {.id=20, .slot=VLIW_SLOT_LSU, .texto="cargai x8,x0,0", .escribe=UINT64_C(1)<<8,
     .efectos=VLIW_EF_LEE_MEMORIA},
    {.id=30, .slot=VLIW_SLOT_ALU, .texto="suma x6,x5,x8",
     .lee=(UINT64_C(1)<<5)|(UINT64_C(1)<<8), .escribe=UINT64_C(1)<<6}
};
VliwDependencia deps[] = {{0,2,VLIW_DEP_RAW,3}, {1,2,VLIW_DEP_RAW,3}};
VliwBloque bb = {.inicio=0, .fin=3};
VliwEntrada entrada = {.instrucciones=ins, .cantidad_instrucciones=3,
    .dependencias=deps, .cantidad_dependencias=2,
    .bloques=&bb, .cantidad_bloques=1, .metadatos_completos=true};
VliwPlan *plan = NULL;
VliwDiagnostico diagnostico;
VliwEstado estado = vliw_planificar(&entrada, &plan, &diagnostico);
/* Consultar plan solo con estado==VLIW_OK. */
vliw_liberar(&plan);
```

Resultado real del fixture `raw`:

| Bundle | ALU | LSU | BRU | CRIPTO |
|---|---|---|---|---|
| 0 | instrucción 0 | instrucción 1 | vacío | vacío |
| 1 | vacío | vacío | vacío | vacío |
| 2 | vacío | vacío | vacío | vacío |
| 3 | instrucción 2 | vacío | vacío | vacío |

Mapa [0,0,3], cuatro bundles, dos completamente vacíos. El generador conservador
utilizaría nueve bundles. Otro fixture con consumidor bloqueado I1 permite emitir
I2 LSU junto a I0 e I3 ALU en ciclo1, dejando ciclo2 vacío antes de I1 en ciclo3.

## Verificador independiente

`vliw_verificar` no vuelve a ejecutar la selección. Recorre la salida y comprueba:

- Dimensiones, capacidad, intervalos y mapas de bloques monótonos.
- Dos NOP de entrada donde corresponden y pertenencia de cada bundle.
- Cada índice exactamente una vez, slot correcto y mapa índice→bundle coherente.
- Conteos de instrucciones, operaciones y vacíos.
- Barreras aisladas y transferencias en el último bundle de su bloque.
- Separaciones explícitas y riesgos de máscaras RAW>=3, WAR/WAW>=1.
- Orden de memoria y de todas las barreras según la secuencia original.

La comprobación por máscaras es defensiva: no devuelve un grafo ni cambia
registros o instrucciones. Python incorpora otro oráculo sobre el resultado
numérico; las pruebas C corrompen deliberadamente slots, índices, mapas,
conteos, pertenencia y riesgos para comprobar el rechazo de planes ajenos.

## Pruebas, comandos y evidencias

Compilación independiente, sin Makefile, IR, codificador ni simulador:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  src/Calendarizacion_VLIW/vliw_scheduler.c tests/vliw/test_vliw_scheduler.c -o /tmp/test_vliw_scheduler
/tmp/test_vliw_scheduler
python3 tests/vliw/test_vliw_scheduler.py
```

La suite Python crea ejecutables y fixtures en TemporaryDirectory y los elimina.
Para conservar manifiesto de comandos/códigos y ejemplos de prueba:

```sh
S7_EVIDENCIA=/tmp/evidencia-s7.json S7_EJEMPLOS=/tmp/ejemplos-s7 \
  python3 tests/vliw/test_vliw_scheduler.py
```

Además de la biblioteca, las pruebas compilan un puente temporal mínimo hacia
`cb_ensamblar/cb_escribir_mem` y la sonda S6 existente. El adaptador de pruebas
coloca etiquetas en los mapas de entrada y convierte índices en los textos
originales; no codifica instrucciones ni calcula desplazamientos. El codificador
real resuelve/valida los saltos y produce MEM. No es una integración de backend,
una CLI nueva ni un segundo ensamblador. La salida pública de S7 sigue sin ser
un programa ejecutable por sí sola.

Se comparan siete fixtures optimizados contra una operación + dos NOP:
registros finales, toda DMEM, bóveda y ESTADO. Los casos incluyen RAW, memoria,
diamante tomado/no tomado, dos clases de bucle y provisionamiento/Feistel.
La equivalencia comprobada pertenece a estos fixtures controlados, no a cualquier
programa del compilador ni a un adaptador S8 inexistente.

La prueba de reservas usa interposición `--wrap=malloc/calloc` solo en el
ejecutable de pruebas GNU/Linux, sin modificar la biblioteca. Se inyectan fallos
en todos sus puntos de reserva y se comprueba que nunca publique un plan parcial.

Sanitizadores, preferiblemente en copias de fuentes separadas:

```sh
CC='cc -g -fno-omit-frame-pointer -fsanitize=undefined' \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 tests/vliw/test_vliw_scheduler.py
CC='cc -g -fno-omit-frame-pointer -fsanitize=address,undefined' \
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 tests/vliw/test_vliw_scheduler.py
```

Regresiones originales, ejecutadas en copias temporales para conservar artefactos:

```sh
python3 tests/vliw/test_sim_cargador.py
python3 tests/vliw/test_sim_decode.py
python3 tests/vliw/test_sim_pipeline.py
python3 tests/vliw/test_sim_lsu.py
python3 tests/vliw/test_sim_bru.py
python3 tests/vliw/test_sim_crypto.py
make -B -k test
```

## Resultados obtenidos

Todas las compilaciones y suites siguientes terminaron con código 0, salvo
la ejecución explícita de LeakSanitizer indicada por separado.

| Conjunto / comando | Resultado real |
|---|---|
| Compilación C11 estricta y ejecutable C | 25 escenarios, 619 comprobaciones PASS |
| `python3 tests/vliw/test_vliw_scheduler.py` | 11 tests PASS; 129 planes revisados por el oráculo Python |
| Inyección `--wrap=malloc/calloc` | 32 posiciones; 15 fallos efectivos controlados, sin plan parcial |
| Fixtures ejecutables mediante codificador y S6 | 7 comparaciones, 14 ejecuciones, 2520 ciclos; estados iguales |
| UBSan, suite S7 completa | 11 tests y mismos recuentos PASS, sin hallazgos |
| ASan+UBSan, suite S7 completa, `detect_leaks=0` | 11 tests y mismos recuentos PASS, sin hallazgos |
| S1–S6 antes/después | 5+13+14+14+17+17 = 80 tests PASS |
| `make -B -k test` antes/después | PASS; compilador/codificador/integraciones originales |

Las verificaciones C cubren los 22 casos mínimos solicitados, entrada vacía,
errores, ciclos, metadatos incompletos, mutaciones de salida, límites con NOP,
determinismo y rendimiento. Los planes Python incluyen 60 DAG sintéticos,
cada uno con el orden de aristas original e invertido. Los recuentos no suman
otra vez repeticiones con sanitizadores ni los asserts internos del verificador.

La regresión conserva 224 vectores de decodificación, 1184 vectores/363 ciclos
LSU, 1262 vectores/884 ciclos BRU y 2104 vectores/3983 ciclos CRIPTO contra el
RTL. No se modificaron ni se rebajaron sus pruebas.

Resultados de los fixtures controlados:

| Fixture | Operaciones | Bundles S7 | Conservador (3 por operación) |
|---|---|---|---|
| raw | 3 | 4 | 9 |
| memoria | 5 | 8 | 15 |
| diamante-False | 6 | 12 | 18 |
| diamante-True | 6 | 12 | 18 |
| bucle | 4 | 10 | 12 |
| bucle-inicial | 4 | 10 | 12 |
| cripto | 6 | 16 | 18 |

En cada comparación se usó el mismo presupuesto de 180 ciclos para dejar ambos
programas drenados y comparar estado. Esos 2520 ciclos no son una medición de
speedup: las reducciones anteriores son de tamaño del layout. No se atribuye
al compilador actual una optimización que aún no está conectada a su backend.

Caso de rendimiento C: 2046 operaciones planificadas en 1023 bundles,
0.024002 s de CPU en una ejecución normal; con UBSan 0.071641 s y con
ASan+UBSan 0.096560 s. Son mediciones del entorno de validación, no umbrales.
Se rechazaron 2047 operaciones con conflicto estructural y los planes que
superaban capacidad por dos NOP de entrada, sin truncar salida.

LeakSanitizer quedó **NO VERIFICADO**. Se compiló y ejecutó explícitamente el
test C con `ASAN_OPTIONS=detect_leaks=1`: código 1, error fatal
`LeakSanitizer does not work under ptrace`. No se cuenta como PASS. ASan y UBSan
sí se ejecutaron y aprobaron con detección de fugas deshabilitada.

No hubo fallos funcionales en las suites S7 ejecutadas. La selección inicial
se ajustó para calcular una sola vez por ciclo el primer acceso de memoria
pendiente, evitando barridos redundantes; se repitieron S7, UBSan y ASan después.
No se corrigieron fallos preexistentes ni se editaron pruebas anteriores.

BALC reconstruido conserva el SHA256 de la línea base:
`3079dfc930c3b77bebb029db0fc41e3a0a14b9e9020ff725cb2e26e800283c8f`.
Los registros completos, manifiestos de comandos/códigos, tiempos y ejemplos
están fuera del repositorio, en `/tmp/ce1108-s7-q92kw35j/`:
`baseline.json`, `normal-resultados.json`, `s7-resultados.json`,
`evidencia-final.json`, `evidencia-ubsan.json`, `evidencia-asan.json`,
`adicionales.json`, logs `baseline-*`, `normal-*`, `final-s7.log`,
`ubsan-s7.log`, `asan-s7.log`, `reservas.log`, `lsan.log` y `ejemplos/`.
Los archivos temporales permiten revisar esta ejecución y los tests/semillas
versionables permiten reproducirla.


## Limitaciones y trabajo pendiente

S7 está completo como biblioteca: selección determinista, dependencias externas,
barreras, protección CFG, verificación y liberación. No modifica el código usado
por la CLI ni mejora todavía el binario que genera el backend conservador.
No se afirma optimalidad global ni se relajan barreras mediante aliasing o
pruebas de autenticación: esas políticas son deliberadamente conservadoras.

La calidad y veracidad de máscaras/CFG provienen de Dylan y Meibel. El verificador
no puede descubrir el significado de una instrucción cuyo productor declara
metadatos falsos, ni garantizar que un emisor futuro respete los mapas. Esa
integración requiere acordar interfaces y validar sus adaptadores.

S8 deberá consumir mapas, asociar etiquetas al primer NOP de protección y resolver
saltos después del layout; el texto de entrada puede contener direcciones previas
y no debe emitirse ciegamente. Aquí no se cambian `rs_resolver`, enlazado de
funciones, CLI, niveles de optimización ni representación intermedia. La
visualización definitiva y las demás etapas siguen pendientes.

No se implementan renombramiento, liveness, spilling, DCE, unrolling, parser,
lexer, ISA nueva, cachés, forwarding ni modificaciones de S1–S6 o Arquitectura.
