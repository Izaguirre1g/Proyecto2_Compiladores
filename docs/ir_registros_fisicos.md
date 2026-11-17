# Issue #23: contrato de asignacion a registros fisicos VLIW

## Alcance implementado

1. `ir_aplicar_spilling()` asigna temporales virtuales de IR (`$tN`) a un
   banco finito de **registros logicos** (`$rN`). Usa reemplazo local LRU,
   guarda valores modificados en `$spillN` y recupera valores cuando se necesitan.
   Limpia el estado del banco al salir de un bloque basico y preserva los
   temporales frente a llamadas mediante vaciado conservador.
2. `ir_fisico_mapear()` transforma la identidad logica a la ISA:
   `$r0 -> x16`, ..., `$r7 -> x23` con **3 a 8 registros configurables**.
3. `ir_fisico_validar_programa()` verifica una IR **despues del spilling**:
   instrucciones validas bajo `ir_consultar_def_usos()`, ausencia de temporales
   `$tN` por asignar y de registros `$rN` fuera del banco contratado.
4. `ir_vliw_desde_ir()` utiliza exactamente ese mapeo y verifica la IR
   asignada antes del lowering fisico de su subconjunto. La codificacion real
   y el DAG fisico siguen comprobando las dependencias sobre los `xN` emitidos.

Los registros `x0`, `x1` (retorno), `x2` (pila) y los auxiliares `x28/x29`
**no** se asignan a temporales. La funcion de mapeo no cambia las convenciones
ABI del procesador.

## Uso en C

```c
#include "ir_registros_fisicos.h"

/* Primero ir_aplicar_spilling(origen, &ir_asignada, 8, &metricas). */
size_t error_en = SIZE_MAX;
if (ir_fisico_validar_programa(&ir_asignada, 8, &error_en) != 0)
    /* Manejar primera instruccion no valida */;

unsigned reg;
if (ir_fisico_mapear("$r7", 8, &reg) == 0) {
    /* reg == 23; el emisor puede usar x23. */
}
```

`ir_fisico_mapear` retorna 0 o 2 y deja la salida intacta en error. La
validacion de programa devuelve la primera instruccion problematica si se
solicita; ante configuracion invalida el indice vale `SIZE_MAX`.

## Pruebas

```sh
make test-ir-fisico
make test-ir-defuse
make test-spilling
make test-ir-vliw
make balc
```

`tests/test_ir_fisico.c` comprueba configuraciones 3..8, todos los x16..x23,
rechazos de indices y formatos invalidos, no uso de registros reservados,
spilling real con presion para 3 y 8 registros, cargas/almacenamientos LSU y
verificacion de cada campo de la IR reescrita. `tests/test_ir_vliw.py`
continua comprobando conversion fisica, codificacion y VLIW sobre el subconjunto.

## Limitaciones que no oculta este issue

**No** es un lowering completo de BAL. Las variables escalares de BAL permanecen
como simbolos hasta su materializacion en memoria; `ir_fisico_validar_programa`
no certifica offsets, representacion de arreglos, compatibilidad de llamadas,
retornos ejecutables o ejecutabilidad de una aplicacion multifuncion. El
productor `--vliw-ir` sigue rechazando estos casos fuera de su subconjunto.
Las latencias N+3 y el scheduling son responsabilidad del DAG fisico, no de
esta asignacion.
