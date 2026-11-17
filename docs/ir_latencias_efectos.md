# Issue #3 — Contrato de latencias, dependencias y efectos

Alcance: **definir y comprobar el contrato entre la IR de tres direcciones y
el calendarizador VLIW**, no sustituir la construcción del CFG/DAG físico ni
integrar el lowering BAL completo. Esta entrega no modifica el scheduler físico.

## Regla arquitectónica N+3

En el backend físico (`include/vliw_scheduler.h`),
`VLIW_RAW_DISTANCIA == 3` bundles. Si la instrucción productora escribe un
registro GPR en el bundle **N**, otra instrucción que *lea ese resultado*
(RAW: read after write) no puede emitirse antes de **N+3**. No hay forwarding.
La regla se comprueba sobre registros físicos x0–x31 (x0 es constante y no
introduce dependencias). Si no hay trabajo independiente, el planificador
puede necesitar NOPs.

| Riesgo | Situación | Separación mínima en el contrato | Motivo |
|---|---|---|---|
| RAW | `A.DEF ∩ B.USE` | 3 bundles | Resultado no disponible antes de N+3 |
| WAR | `A.USE ∩ B.DEF` | 1 bundle | Conservar la lectura anterior |
| WAW | `A.DEF ∩ B.DEF` | 1 bundle | Conservar la última escritura |
| Memoria sin aliasing acreditado | Ambos acceden a memoria | 1 bundle, conservador | Orden de efectos/alias; no promete bypass |
| Barrera física: llamada, retorno, efectos globales desconocidos | No cruzar el límite | 3 bundles en modelo físico | Conserva estado/ABI sin atribuir pureza |
| Control | Salto/retorno/fin de bloque | Orden y límites del CFG | No desplazar por encima del terminador |

**No confundir los planos**: en `reordenamiento/`, `latencia` modela una
estimación de IR (una instrucción por ciclo); el DAG físico de Javier analiza
*operaciones ya codificadas*, máscaras reales, efectos acreditados y el CFG.
La distancia simbólica es una **cota orientativa y conservadora**, no autoriza
`metadatos_completos=true` ni declara ejecutable un `.bin`.

## Efectos de instrucciones de IR

La fuente canónica de operandos USE/DEF y efectos abstractos está en
`include/ir_defuse.h` / `ir_consultar_def_usos()`. El adaptador nuevo
`include/ir_latencias_efectos.h` complementa esa consulta cuando importa si
un nombre BAL puede representar una **variable global**:

| Instrucciones | Efectos identificables |
|---|---|
| `IR_COPY`, `IR_BIN`, `IR_UNARY`, `IR_INDEX_REF` | USE/DEF de valores; si operan sobre nombres globales, lectura/escritura de memoria abstracta |
| `IR_ARG` | Uso del argumento, barrera de orden; **no** es operación física y no recibe N+3 automática |
| `IR_CALL` | Define retorno si existe, memoria global potencialmente leída/escrita, barrera de efectos y control |
| `IR_LOAD`, `IR_SIZE` | Lectura de memoria, más accesos a globales de sus operandos |
| `IR_STORE`, `IR_ARRAY_DIM`, `IR_LIST_ADD`, `IR_LIST_REMOVE` | Escrituras o lecturas/escrituras de memoria según `ir_defuse` |
| `IR_ARRAY_DECL`, `IR_LIST_DECL` | Declaración de colección; escritura abstracta de memoria |
| `IR_SPILL_LOAD` | Lee memoria de ranura `$spillN`, define `$rN` |
| `IR_SPILL_STORE` | Lee `$rN`, escribe memoria de ranura `$spillN` |
| `IR_LABEL`, `IR_JUMP`, `IR_JTRUE`, `IR_JFALSE`, `IR_RETURN`, `IR_FUNC`, `IR_END_FUNC` | Control (no barrera de efectos artificial en límites de función) |
| `IR_PARAM_DECL` | Define el parámetro local; no se infiere que sea global |

Los accesos a colecciones, globals, llamadas y spilling se mantienen
**conservadores**: no se deduce independencia de dos accesos solo por mostrar
`$spill0` y `$spill1` u otros nombres distintos. Las funciones llamadas pueden
modificar globales **sin** que esos nombres aparezcan en `IR_CALL`.

## Resolución de nombres globales

`ir_efectos_consultar(inst, contexto, &salida)` acepta un callback
`IrResolverAmbito`, que informa si un identificador BAL es local, global o
**desconocido**. Las variables internas `$tN` y `$rN` no se consultan; los
spills se representan como memoria. Sin callback, todo identificador BAL
expreso se trata como **posible global** y activa memoria conservadora.

La clasificación es **por nombre**, no por identidad semántica: si hay
homónimos en ámbitos distintos o sombreado de variables, el consumidor debe
normalizar nombres con resolución de símbolos o responder
`IR_AMBITO_DESCONOCIDO`. Nunca declarar local una variable cuya identidad
no esté acreditada. `puede_leer_global`, `puede_escribir_global` indican
posibilidad, no prueba de que el símbolo sea global.

```c
#include "ir_latencias_efectos.h"

IrEfectos efectos = {0};
/* contexto puede ser NULL: comportamiento conservador. */
if (ir_efectos_consultar(&inst, NULL, &efectos) != 0) {
    /* instruccion IR invalida; rechazar antes de optimizar */
}
```

## Consultar riesgos simbólicos

`ir_dependencia_consultar(&anterior, &posterior, contexto, &resultado)`
compara dos instrucciones **en orden dentro del mismo bloque básico**.
Devuelve los bits `IR_DEP_RAW`, `IR_DEP_WAR`, `IR_DEP_WAW`,
`IR_DEP_MEMORIA`, `IR_DEP_BARRERA` y `IR_DEP_CONTROL`; varios pueden
coexistir. Si hay RAW o una llamada, la cota orientativa se eleva a 3.
Para una barrera de `IR_ARG` sola se conserva el orden, sin asignar latencia
de ISA a esa pseudoinstrucción. La consulta no arma CFG, no mapea registros
virtuales a físicos y no determina si una función modifica una región concreta.

El DAG físico (`src/Calendarizacion_VLIW/vliw_dag_fisico.c`) valida los
metadatos S2, el CFG y acreditaciones de efectos antes de construir riesgos.
El calendarizador (`src/Calendarizacion_VLIW/vliw_scheduler.c`) exige
`metadatos_completos` y distancias RAW >=3. **La existencia de esta API IR
no acredita esos metadatos por sí misma**.

## Pruebas y compatibilidad

```sh
make test-ir-latencias
make test-ir-defuse
make test-ir-fisico
make test-ir-vliw
make test-dce
make test-reordenamiento
```

`tests/test_ir_latencias_efectos.c` cubre: RAW/WAR/WAW, N+3 medido en un
`VliwPlan` generado por el scheduler real, rechazo de RAW con distancia 2,
rechazo de metadata incompleta, barrera física de llamada con efectos
`DESCONOCIDOS`, ámbitos local/global/desconocido, alias de memoria, arreglos
indirectos, llamadas, `IR_ARG`, spills y fallos transaccionales.

Esta entrega **no** prueba que los 15 programas BAL `-O0/-O2` sean equivalentes
ni sustituye la generación ejecutable de funciones/calls/returns del backend.
Esos trabajos pertenecen a otros issues.
