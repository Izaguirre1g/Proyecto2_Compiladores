# Entrega final del aporte de Javier

Este documento consolida la ejecución, calendarización y validación. Los contratos
precisos permanecen en [integración física](contrato-integracion.md),
[CFG](cfg-fisico.md), [DAG](dag-fisico.md) e
[integración con Arquitectura](integracion-ir-arquitectura.md).
La base de esta entrega es Compiladores `e66ee5e` (PR #69); la referencia RTL
es Arquitectura `5e6e45f`. Las cifras actuales se conservan en el informe externo
`evidencias-proyecto2-compiladores-javier-final`, con logs y comandos por campaña.

## Ejecución por ciclos

S1 carga imágenes; S2 decodifica palabras reales del ISA. Cada bundle tiene
128 bits, con slots ALU, LSU, BRU y CRIPTO. M2 modela IF/ID/EX/WB: las lecturas
se capturan en ID, los resultados pasan de EX a WB y las escrituras a x0 se
ignoran. Los stores publican en EX; los loads conservan su resultado hasta WB.
Las unidades ejecutan en paralelo y el arbitraje de escrituras reproduce la
prioridad CRIPTO, BRU, LSU, ALU. El modelo conserva sus conflictos en la traza.
Un salto tomado anula las instrucciones especulativas de IF/ID, conserva el
enlace del propio salto y continúa en el destino comprobado por BRU.

El estado arquitectónico incluye PC, 32 GPR, DMEM, estado público CRIPTO y
traps. La bóveda es privada. Los registros de pipeline, validez y resultados
pendientes son estado microarquitectónico. CRIPTO reutiliza el modelo ISA y
maneja permisos, dos destinos Feistel y excepciones; no se expone la bóveda
como un observable público. M1 cuenta admisión, finalización, anulaciones,
operaciones útiles, saltos y accesos. Los errores distinguen PC, decodificación,
unidad funcional, argumento y presupuesto; no se convierten en terminación.

La búsqueda finita drena el pipeline al agotarse la imagen. La búsqueda física
reproduce el direccionamiento IMEM del procesador y exige presupuesto. Alcanzar
ese presupuesto produce un corte activo: ni un bucle estable ni B5 demuestran
el retorno de `main`. B1/B2 conservan `INCONCLUSO` cuando su contrato estricto
no permite acreditar equivalencia; la igualdad de observables BAL a un corte
acreditado se presenta por separado.

El simulador usa memoria funcional por instancia; no modela automáticamente
los ciclos de stalls de L1/L2/RAM del RTL. Por ello ciclos M2 y ciclos físicos
con caché no son medidas intercambiables.

## Del flujo físico al código

I1-A2a extrae metadatos físicos con S2: slot, registros, efectos y tipo. Un ID
externo identifica una operación, pero las aristas S7 usan índices. La generación
invalida los análisis al cambiar el flujo; no autentica por sí sola su contenido.
El CFG identifica bloques, terminadores y sucesores. Un backedge es válido en
el CFG, sin convertirse en una arista retrospectiva del DAG de emisión.

El DAG físico cubre globalmente los riesgos del orden original. RAW exige
separación N+3 porque no hay forwarding; WAR, WAW y memoria conservadora
requieren al menos un bundle. Se excluye x0. Dos loads también conservan orden
sin una prueba de aliasing. Control y barreras derivan de efectos acreditados;
CRIPTO/estado/bóveda/excepciones y efectos desconocidos reciben las restricciones
conservadoras del contrato. No se inventa una barrera para ocultar un riesgo.
La cobertura transitiva debe conservar la suma de distancias de un camino,
incluso entre bloques; una arista válida aislada no demuestra cobertura global.

S7 calcula prioridades por camino crítico y selecciona operaciones listas en
orden determinista. Agrupa hasta una por slot cuando las dependencias lo
permiten, aísla barreras y cierra bloques con sus transferencias. Introduce NOP
para respetar latencias y dos guardas en entradas protegidas. Verifica tanto
el plan como sus mapas, ubicaciones, riesgos y restricciones arquitectónicas.
El máximo estructural de 4092 operaciones no garantiza caber en 1023 bundles:
latencias, guardas y camino crítico consumen capacidad. El rechazo de capacidad
es un resultado válido, sin publicar una salida incompleta.

S8-A emite el programa planificado y comprueba que palabra, texto y decodificación
concuerden. CB/S2 y S8-C verifican imágenes y roundtrip; no reemplazan la
resolución de símbolos, ABI o reubicaciones que deba proporcionar el productor.
Los constructores publican resultados completos, con propiedad y liberación
explícitas; las entradas prestadas permanecen inmutables.

## Rutas productivas y límites

| Ruta | Estado implementado |
|---|---|
| BAL → AST → backend tradicional → ensamblador/binario/MEM | Productiva para el lenguaje admitido por ese backend |
| `-O0` → misma ruta tradicional | Implementado; conserva los bytes de la compilación predeterminada |
| `-O1` → DCE IDE → BAL reescrito → frontend/backend tradicional | Implementado en el alcance acreditado del reescritor de José |
| IR → USE/DEF → spilling/mapeo → productor ISA parcial → metadatos/CFG/DAG → S7 → S8-A | Implementado para el subconjunto aceptado, validado con fixtures y preview CLI |
| IR BAL general optimizada → programa completo por S7 | BLOQUEADO por lowering, llamadas/ABI y enlace generales |
| `-O2` con unrolling/reordenamiento productivos | Rechazo explícito; PENDIENTE DE ACUERDO del pipeline del equipo |

`-O1` no llama a un backend nuevo ni conecta automáticamente IR optimizada con
S7. Reutiliza `ide_dce_analizar`, que reconstruye AST/IR/CFG y devuelve BAL cuando
puede justificar su reescritura; el backend tradicional vuelve a generar código.
Si IR cambia y no existe BAL acreditado, se rechaza. Si la pasada no cambia el
fuente, se informa `cambio=0`. No se promete ahorro en todos los programas.

El adaptador guarda temporales en un directorio privado; no sobrescribe el
fuente ni un archivo `.ide_` del usuario. Limita a 2 MiB, rechaza NUL, errores
léxicos y módulos `bring` sin reescritura global acreditada. Rechaza niveles
duplicados/invalidos y combinaciones con vistas de análisis, JSON o ejecución
`-x`. La liberación es idempotente y la salida se publica solo tras éxito.
La CLI predeterminada mantiene su comportamiento. `-O2` devuelve error sin
fallback; `--dce`, `--unroll`, `--reorder` siguen siendo análisis independientes.

## Evaluación reproducible

Construir siempre en una copia externa para preservar el ejecutable versionado:

```bash
make -B balc
./balc -O0 -s programa.bal
./balc -O1 -s programa.bal
python3 -B tools/vliw/validar_vliw.py --help
python3 -B tools/vliw/validar_vliw.py --arquitectura /tmp/arquitectura-referencia --evidencia /tmp/regresion.json
python3 -B tools/vliw/evaluar_entrega_final.py --help
python3 -B tools/vliw/evaluar_entrega_final.py --balc ./balc --arquitectura /tmp/arquitectura-referencia --salida /tmp/evaluacion-nueva
```

La última herramienta exige un destino externo nuevo. Su manifest contiene
quince fuentes, tres por categoría, hashes, valores lógicos y offsets por
variante. Compila cada imagen mediante el CLI oficial y B5 vuelve a compilar
mediante el mismo nivel: los hashes deben coincidir. Los observables provienen
de las fuentes y se ubican mediante el marco `main` del ensamblador emitido.
No se comparan indiscriminadamente offsets que cambian por DCE.

La campaña compara ensamblado de Arquitectura contra MEM y código BALB; rechaza
datos iniciales no acreditados. Ejecuta M2/M1 y el RTL real con MEM_MODE 0/2,
L2_MODEL=0. Captura 32 GPR, 64 KiB DMEM tras flush y estado público CRIPTO/traps.
La comparación completa corresponde a cada variante contra su RTL; la
comparación entre variantes corresponde a observables lógicos declarados.

La matriz distingue la categoría solicitada de la transformación realmente
aplicada. Un caso de presión que ejecuta O1 mediante DCE no acredita spilling
productivo. Un análisis `--ir-spilling` o `--unroll` guardado junto a la imagen
no demuestra que esa imagen incorpore la transformación. Los bloqueos del
backend se conservan en sus logs y no se reparan modificando módulos ajenos.

## Lectura de métricas

Código y bundles son magnitudes estáticas de la imagen; operaciones útiles,
ciclos, saltos y accesos son dinámicos al horizonte registrado. IPC requiere
explicitar qué operaciones se cuentan y sobre qué ciclos. Ocupación mide uso
de los cuatro slots y no prueba corrección. Los stalls/hits/misses del RTL
requieren configurar cachés y estado inicial equivalentes; el flush ocurre
después del corte observado y no debe incluirse como ejecución del programa.

Los reportes guardan comandos, códigos, tiempos reales de compilación y
simulación, hashes, configuración y horizontes. No se publica speedup de `main`
a partir de cortes activos. Las comparaciones estrictas no se reemplazan por
la igualdad de un subconjunto de temporales. ASan/UBSan/LSan y Memcheck
acreditan las rutas ejecutadas, sin demostrar ausencia universal de errores.
Los niveles C `-O0..3` de instrumentación no son niveles de optimización BAL.

## Defensa técnica breve

Javier implementó el consumidor físico y la calendarización VLIW, el simulador
por ciclos, sus comparadores y métricas, y la validación cruzada con el procesador
real. El scheduler evita dependencias ilegales y verifica el plan antes de
emitir. El simulador permite observar cuándo se publican registros y memoria;
los contrastes RTL detectan diferencias de codificación, latencia, saltos,
excepciones y persistencia. El cierre añade opciones productivas O0/O1 sin
reimplementar DCE y una evaluación reproducible de quince casos que expone
resultados y rechazos reales. El backend general, las pasadas de otros miembros,
ABI y enlace siguen bajo responsabilidad del equipo; esta entrega no los
presenta como terminados por Javier.

## Resultados medidos del cierre

La campaña actual recompiló quince fuentes: catorce parejas O0/O1 produjeron
resultados lógicos correctos y 56 contrastes RTL completos (dos variantes,
dos modos). B1 y el lote B2 devolvieron 14 INCONCLUSO, cero EQUIVALENTE,
cero DIFERENTE y cero errores. Cinco casos acreditan su categoría: dos DCE
y tres de control. El caso `dce_efectos` no compila por parámetros compuestos
pendientes; las otras nueve categorías de caso siguen sin transformación
productiva propia. No se afirma que estos quince programas estén certificados.

| Caso | Bundles O0/O1 | Horizontes B5 O0/O1 |
|---|---|---|
| lu_constante | 144 / 138 | 310 / 304 |
| lu_variable | 243 / 237 | 333 / 327 |
| lu_grande | 150 / 144 | 1681 / 1675 |
| reg_war | 123 / 117 | 121 / 115 |
| reg_waw | 105 / 93 | 103 / 91 |
| reg_cruzado | 129 / 123 | 127 / 121 |
| dce_puntajes | 801 / 795 | 2748 / 2742 |
| dce_funciones | 528 / 435 | 6193 / 2176 |
| dce_efectos | BLOQUEADO | no ejecutado |
| control_condicional | 153 / 147 | 136 / 130 |
| control_backedge | 144 / 138 | 232 / 226 |
| control_llamadas | 363 / 357 | 433 / 427 |
| spill_suma | 141 / 135 | 139 / 133 |
| spill_diferencias | 141 / 135 | 139 / 133 |
| spill_mixto | 141 / 135 | 139 / 133 |

Estos horizontes no son tiempos de terminación de main. Por ejemplo,
`dce_funciones` pasa de 528 a 435 bundles y de 6193 a 2176 ciclos observados,
con observables 16/15 preservados; ese dato no se rotula como speedup global.
La matriz externa conserva IPC, instrucciones útiles, stalls, configuración,
memoria, contadores DCE y tiempos de compilación de cada pareja.

La regresión final ejecutó **25 suites y 439 métodos por modalidad** normal,
ASan, UBSan y ASan+UBSan: FAIL=0, SKIP=0. Son veinte métodos nuevos sobre
la referencia histórica 419. `make -B -k test` y los targets vigentes de IR,
spilling, defuse, mapeo y latencias pasaron; Arquitectura pasó sim, assembler,
programs y memory-modes en copia externa. Memcheck O0–O3 y LSan no detectaron
errores/fugas en las rutas probadas. Los logs íntegros y límites de inyección
están en el directorio externo de evidencia; la GUI GTK no se verificó.
