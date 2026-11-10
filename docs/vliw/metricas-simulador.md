# Métricas dinámicas del simulador — M1 / issue #9

M1 observa flancos publicados de S1–S6. No ejecuta instrucciones, no modifica el
pipeline y no agrega instrumentación a módulos aprobados. Cuenta trabajo dinámico,
incluidas ejecuciones repetidas de una misma dirección, y exporta resultados
arquitectónicos permitidos. No contiene frontend, IR, optimizaciones, CLI o IDE.

## API y propiedad

```c
SimPipeline simulador;
SimMetricas metricas = {0};
/* Cargar imagen e iniciar/configurar el simulador mediante S1–S6. */
SimMetricasEstado e = sim_metricas_iniciar(&metricas, &simulador);
/* Comprobar e antes de avanzar. */

SimPipelineEstado estado = sim_avanzar_ciclo(&simulador);
e = sim_metricas_observar(&metricas, &simulador);
/* OK: flanco registrado. SIN_CICLO: no ocurrió otro flanco.
 * Si observar falla, no avanzar otro ciclo antes de resolver el error. */

SimMetricasResumen resumen;
e = sim_metricas_resultados(&metricas, &resumen);
char *json = NULL;
e = sim_metricas_exportar(&metricas, SIM_METRICAS_JSON, &json);
/* Usar únicamente resultados con retorno OK. */
sim_metricas_liberar_reporte(&json);
sim_metricas_liberar(&metricas);
```

La imagen y el simulador son referencias prestadas: deben seguir vivos y la
imagen permanecer inmutable. El recolector se inicializa con `{0}` y no se copia
superficialmente ni se modifican sus miembros. Posee solo su historial de traps;
no posee el simulador. `liberar` y `liberar_reporte` admiten NULL y limpian sus
salidas. No hay estado global mutable en la biblioteca.

`SimMetricasResumen` es un snapshot propio sin punteros: puede copiarse y seguir
usándose después de reset o liberación. La cadena exportada también es propia.
El historial completo puede conservarse exportando antes de reemplazar la época,
o consultarse por índice con `sim_metricas_trap` mientras el recolector lo posea.
No existe API para leer claves o contraseña de la bóveda.

## Flancos, secuencias y épocas

`iniciar` exige un simulador válido en ciclo cero, sin admisiones, retiros,
anulaciones, traps ni etapas pendientes. Admite ACTIVO o LIMITE sin flancos.
Cada inicio reemplaza la época anterior e incrementa su número en ese objeto.
No permite comenzar a mitad de una ejecución y fingir que conoce sus eventos.

Después de cada avance se compara `sim->ciclos` con el último observado:

- Igual: `SIN_CICLO`, idempotente. Un presupuesto cero, límite, error sin flanco
  o llamada repetida no agrega eventos. Se permite actualizar el estado final
  administrativo mediante `resultados`, sin sumar reloj.
- Exactamente uno más: comprobar `ultimo_ciclo.ciclo`, señales e invariantes;
  acumular el flanco, incluso cuando deja `SIM_PIPE_TERMINADO`.
- Más de uno: `SECUENCIA`. No se reconstruyen instrucciones ni eventos perdidos.
- Menor, imagen/modo/instancia diferentes o estado observable reiniciado:
  `EPOCA`. Debe iniciarse una observación nueva después del reset.

Los resultados/exportación también rechazan ciclos todavía no observados. Un
fallo de reserva al registrar un trap deja los contadores intactos: se puede
reintentar ese mismo flanco antes de avanzar otro. El flanco del procesador ya
ocurrió; el recolector no lo revierte. Los reportes se construyen en un buffer
privado y solo se publican completos; estadísticas y comparaciones solo sustituyen
sus salidas al finalizar correctamente.

El reset no es un ciclo. Puede publicar stores que estaban preparados en EX,
según el contrato S6: esos efectos administrativos no se cuentan como accesos de
ejecución. La época siguiente ve la DMEM resultante. Guardar el resumen previo
antes de reset si se necesita conservar el corte anterior.

**Límite de observabilidad:** S6 no expone un identificador de reset. Si el cliente
resetea y vuelve a ejecutar sin notificarlo, hasta reproducir contadores/estado
compatibles, no existe información suficiente para detectar toda esa historia.
El contrato exige llamar `iniciar` después de cada reset; no se promete detectar
un reset oculto que deje un estado indistinguible. No se modifica S6 para añadir
un identificador ni se inventa una reconstrucción de ciclos omitidos.

## Definición de contadores

Todas las cantidades son por época. Los eventos se acumulan como uint64_t con
comprobación de desbordamiento; las sondas físicas de traps siguen siendo 32 bits.

| Campo de `SimMetricasResumen` | Definición y momento |
|---|---|
| `ciclos` | Flancos efectivamente aceptados; coincide con el contador oficial. |
| `admitidos` | Bundles aceptados por IF. Se contrasta el delta oficial con IF válido sin descarte. |
| `completados` | Bundles válidos en WB, incluidos los totalmente NOP. |
| `anulados` | Oficial: IF/ID descartado más culpable EX cuando hay excepción. |
| `pendientes` / `pendientes_por_etapa` | Ocupación posterior IF/ID, ID/EX y EX/WB. |
| `bundles_nop` / `bundles_utiles` | Clasificación de cada bundle retirado por S2: cero o al menos una operación no NOP. |
| `instrucciones_utiles` | Número de operaciones no NOP en los bundles que llegaron válidamente a WB. |
| `instrucciones_por_slot[4]` | Mismo conteo, separado en ALU/LSU/BRU/CRIPTO. |
| `operaciones_isa` | Desglose WB con los nombres canónicos de S2; ret se reconoce como syer. |
| `bru_ex` | Transferencias BRU realmente ejecutadas en EX. |
| `saltos_tomados` / `saltos_no_tomados` | Decisión EX observada; sye/syer siempre tomados. No tomado corresponde a condicionales. |
| `flush_salto` | Flush por BRU tomada; excluye excepciones. |
| `anulados_salto` | IF/ID ya admitido descartado por BRU. |
| `anulados_excepcion` | Bundle culpable EX más IF/ID joven descartado, cuando existe. |
| `busquedas_descartadas` | IF especulativo suprimido: nunca fue admitido y no suma a anulados. |
| `traps` | Excepciones arquitectónicas aceptadas en EX, uint64_t independiente de la sonda física. |
| `lsu_lecturas_ex` | Loads A ejecutados: transacción válida y carga. La lectura combinacional de un store no suma otro load. |
| `lsu_escrituras_ex` | Stores A ejecutados, observados en la transacción EX válida. |
| `puerto_b_operaciones_ex` / `puerto_b_lecturas_ex` | Transacciones B válidas: vcr y camcon leen la palabra previa. |
| `puerto_b_escrituras_ex` | Escritura B observada de camcon. |
| `conflictos_memoria` | Ciclos con ambas escrituras sobre la misma palabra física. B mantiene su prioridad S6. |
| `escrituras_por_puerto[5]` | Solicitudes habilitadas WB: ALU, LSU, BRU, CRIPTO L y CRIPTO R. |
| `descartes_x0_por_slot[4]` | Solicitudes descartadas hacia x0; no equivalen a instrucciones anuladas. |
| `conflictos_registro` | Ciclos en los que la señal WB de conflicto está activa; no cantidad de pares en conflicto. |

Se verifica en cada observación:

```text
admitidos = completados + anulados + ocupación IF/ID + ID/EX + EX/WB
```

Los deltas de los contadores oficiales deben coincidir además con la traza del
flanco. No se suman nuevamente los bundles al pasar por cada etapa.

## Instrucciones, NOP y permisos

Para WB válido se selecciona el bundle original por PC y se llama exclusivamente
a `sim_decode_palabra`; nunca se recalcula ALU, LSU, BRU o CRIPTO. En modo finito
el PC debe corresponder a código cargado. En modo físico se usa `(PC >> 4) & 1023`;
las posiciones no cargadas equivalen a cuatro NOP, como S6. Cada retiro dinámico
cuenta, aunque su PC ya haya aparecido antes en un bucle o por aliasing.

Un store o instrucción hacia x0 cuenta como operación útil si su bundle llega a
WB, aunque no escriba un registro. Un bundle enteramente NOP sí completa, pero
aporta cero instrucciones. Una ronda Feistel cuenta como una instrucción CRIPTO,
aunque habilite dos puertos de escritura. Las solicitudes WB superpuestas siguen
siendo observables; el último puerto conserva su prioridad, sin stalls nuevos.

Los pares CRIPTO legales de S2 no pueden escribir x0 ni coincidir entre sí; ese
contador permanece cero para CRIPTO en las codificaciones soportadas.

## Excepciones frente a errores administrativos

La excepción en EX anula todo el bundle culpable, incluidos los slots ALU/LSU/
BRU que pudieran existir junto a CRIPTO. No llegan a WB ni producen accesos de
memoria contables. El WB anterior sí puede completar y se cuenta normalmente.
La excepción no incrementa los saltos BRU aunque exista una BRU en el culpable.

Se conserva cada evento público `{ciclo, pc, vector}` en el historial. El vector
actual es 0x3FF0; también se observa su ejecución y las excepciones del propio
handler. `trap_count32` conserva el wraparound físico de S6 y `traps` acumula el
número de eventos por época sin ese wraparound de 32 bits.

Errores de API, argumentos, decodificación o PC finito no son traps. Un rechazo
administrativo puede dejar un WB pendiente sin retirarlo: si no se aceptó el
flanco, M1 no cuenta su trabajo ni interpreta otra vez la última traza vieja.

## IPC y resultados permitidos

```text
IPC = instrucciones útiles completadas / ciclos aceptados
Bundles/ciclo = bundles completados / ciclos aceptados
```

Ambos cocientes valen cero si no hay ciclos. La estructura C conserva double;
los reportes usan nueve decimales truncados y punto fijo independiente del locale.
La división larga evita multiplicar resto*10, que podría desbordar uint64_t.

El snapshot contiene estado de ejecución, PC final, ciclos, 32 GPR, AUTH/INIT,
trap_count32/trap_pc, último evento y huella DMEM. Los reportes solo serializan
una lista blanca de estos datos y contadores: nunca SimPipeline/SimCiclo completos,
claves, contraseña ni el almacenamiento de bóveda.

La huella es FNV-1a64 sobre los 65536 bytes en orden creciente de dirección:
valor inicial 14695981039346656037; por byte: XOR y producto por 1099511628211,
módulo 2^64. Se exporta como 16 dígitos hexadecimales minúsculos. Se calcula al
pedir resultados, no en cada observación. Una huella no sustituye la comparación
completa de memoria.

Las métricas no representan frecuencia ni tiempo de pared. En FINITA se incluye
llenado/drenado hasta terminar. En FISICA continúa la búsqueda de NOP/aliasing:
el presupuesto determina el horizonte observado y se informa LIMITE. Un índice
estático S8-C, utilización estática, IPC dinámico y ciclos aceptados son distintos.

## Comparaciones

`sim_metricas_comparar(referencia,candidato,&salida)` devuelve candidato menos
referencia para ciclos, bundles completados e instrucciones. Una diferencia
entera utiliza signo y magnitud uint64_t, sin convertir extremos a int64_t.
También devuelve diferencia de IPC y cambio porcentual de ciclos. Base cero:
`porcentaje_definido=false` y valor cero. No exige iguales instrucciones dinámicas.

La API no certifica equivalencia ni denomina una diferencia una optimización.
Las pruebas verifican por separado terminación, GPR, 65536 bytes DMEM, AUTH/INIT y
excepciones esperadas, con idénticas entradas. En imágenes distintas, el PC final
es relativo a su layout y se valida frente a su extensión, sin exigir su igualdad
numérica. No se implementó ninguna optimización nueva para esas comparaciones.

## Reportes y ejemplo real

Se ofrecen TEXTO y JSON, ambos atómicos y deterministas. JSON versión 1 declara
naturaleza `dinamica`, estado, contadores, IPC, histogramas por slot/ISA,
solicitudes WB, pendientes, historial de excepciones y resultados permitidos.
No se agrega CSV ni se modifica la CLI del compilador.

El fixture RAW utiliza ALU+LSU en bundle 0, dos NOP y consumidor en bundle 3.
Con DMEM[0]=11 produce x6=18. Su reporte incluye:

```text
Métricas dinámicas VLIW
Estado: TERMINADO
ciclos: 7
admitidos: 4
completados: 4
anulados: 0
instrucciones_utiles: 3
IPC: 0.428571428
Bundles/ciclo: 0.571428571
Instrucciones completadas WB: ALU=2,LSU=1,BRU=0,CRIPTO=0
```

Son líneas seleccionadas del reporte completo. Proyección JSON válida:

```json
{"version":1,"naturaleza":"dinamica","estado":"TERMINADO","ciclos":7,"completados":4,"instrucciones_utiles":3,"ipc":0.428571428,"instrucciones_por_slot":{"ALU":2,"LSU":1,"BRU":0,"CRIPTO":0}}
```

## Verificación y reproducción

```bash
PYTHONDONTWRITEBYTECODE=1 \
ARQUITECTURA_ROOT=/ruta/Proyecto2_Arquitectura1 \
M1_EVIDENCIA=/tmp/m1-evidencia.json \
M1_ARTEFACTOS=/tmp/m1-ejemplos \
python3 tests/vliw/test_sim_metricas.py
```

La suite compila seis ejecutables C11 con `-Wall -Wextra -Wpedantic -Werror`:
M1 normal/fallos, puente CB, puente S7+S8-A y sondas S2/S6 originales. El auxiliar
M1 toma `mem|balb ruta datos presupuesto finita|fisica json|texto dmem_salida`;
no se conectan opciones a balc. Todos los ejecutables/evidencias van a temporales.

El test C y cada ejecución del auxiliar avanzan simultáneamente un segundo
simulador sin M1, comparando PC, estado, contadores, GPR, DMEM completa y bóveda
antes/después de observar. Esa inspección privada solo existe en el test.
Python utiliza JSON real, FNV independiente, valores concretos y sondas originales.
CB codifica los programas y S7/S8-A producen las planificaciones comparadas.
Los fixtures de conflicto combinan palabras legales CB deliberadamente, sin
introducir otro codificador ni cambiar sus validaciones globales.

Resultados finales:

| Verificación | Cantidad | Resultado / código |
|---|---:|---|
| C |20 escenarios,1548 comprobaciones|PASS /0|
| Python |11 tests|PASS /0|
| Ejecuciones del auxiliar M1 |75, cada una con un segundo simulador sin M1|PASS /0|
| RTL |10 programas,166 ciclos|PASS /0|
| Comparaciones conservador/S7+S8-A |2 pares|PASS /0|
| Fallos |9 reservas,4 errores de generación|PASS /0|
| ASan+UBSan y UBSan independiente |suite completa|PASS /0|
| LeakSanitizer explícito |1 ejecución final|NO VERIFICADO: ptrace /1|
| Regresiones S1–S8-C |112 tests Python antes/después|PASS /0|
| Compilador general |`make -B -k test` antes/después|PASS /0|

La inyección cubre buffers y las dos reservas de historial (inicial/crecimiento),
con reintento del flanco ya aceptado. Los errores de las dos fases de vsnprintf
se inyectan también en su variante fortificada, sin desactivar FORTIFY.
ASan usa detect_leaks=0; no se afirma una verificación de fugas que LSan no pudo
realizar. Las suites anteriores se ejecutan en copias desechables para conservar
los artefactos versionados originales.

Las trazas RTL/S6 se comparan campo por campo y contra los 65536 bytes DMEM. Los
contadores M1 se contrastan con sus señales de IF/WB, descartes, EX, escrituras y
traps. El conteo de instrucciones se deriva de WB válido + PC + S2, pues RTL no
expone un contador de instrucciones útil. El conflicto de memoria se deriva de
ambas escrituras y sus direcciones físicas. El estado LIMITE es administrativo
C; no se presenta como señal RTL. El PC alto por aliasing tiene además prueba C.

## Límites y pendiente

M1 está completo como biblioteca independiente con resultados/reportes,
comparación y pruebas. Se requieren observación continua, datos prestados vivos
y nueva época explícita tras reset. No reconstruye historia perdida ni garantiza
detectar resets ocultos indistinguibles. Los structs propios no deben alterarse.

La instrumentación de CLI y los acuerdos para integrar productores/IR o niveles
de optimización permanecen pendientes. M1 no implementa dependencias, lowering,
registros, renombramiento, liveness, DCE, unrolling, IDE, cachés ni cambios RTL.
