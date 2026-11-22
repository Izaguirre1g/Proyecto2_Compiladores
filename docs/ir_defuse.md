# Issue #7 — API publica de usos, definiciones y efectos de IR

Esta API analiza **una instruccion** `IrInstr` del contrato IR, sin modificarla.
Esta implementada en `src/Representacion_Intermedia/ir_defuse.c`, con interfaz
`include/ir_defuse.h` y pruebas en `tests/test_ir_defuse.c`.

```c
IrDefUse r = {0};
if (ir_consultar_def_usos(&instruccion, &r) != 0) {
    /* instruccion, opcode u operandos invalidos */
}
```

## Resultado

- `r.usos[]` y `r.definiciones[]`: nombres **simbolicos de valores** (no
  registros fisicos); cantidades respectivas `cantidad_usos` y
  `cantidad_definiciones`.
- `lee_memoria`, `escribe_memoria`: acceso abstracto a memoria, no dato
  certificado de aliasing.
- `barrera`: no reordenar a traves de una secuencia con posibles efectos,
  especialmente argumentos y llamadas.
- `control`: etiqueta/salto/retorno o limite de funcion.

`IR_FUNC` y `IR_END_FUNC` son limites de control, **no** barreras de
efectos: el CFG y las posiciones fijas ya impiden cruzarlos. Marcarlos
como barreras crearia dependencias `EFECTO` artificiales en los informes.
El conjunto de usos elimina duplicados: `v gauss v` aporta un solo USE de
`v`, por lo que cada dependencia RAW se cuenta una vez por pareja de
instrucciones y nombre de valor.
- `objeto_memoria`: identificador de ranura `$spillN` para un acceso de
  spilling; `NULL` indica objeto no especificado, no disjuncion.

La llamada devuelve 0 en exito y 1 si la instruccion o sus operandos son
invalidos. En error, el resultado queda a cero. La consulta no reserva memoria.
Todos los punteros devueltos apuntan a los textos de `IrInstr` y se invalidan
si la instruccion se modifica o libera.

La funcion auxiliar `ir_es_nombre_valor()` reconoce variables BAL,
`$tN` y `$rN`, y descarta enteros, booleanos, literales, etiquetas `$LN`,
identificadores de ranuras `$spillN` y cadenas. **No resuelve identidades de
simbolos/ambitos** ni convierte nombres a direcciones o GPR.

## Casos importantes

| Operacion | USE | DEF | Otros efectos |
|---|---|---|---|
| `IR_BIN dst,a,b` | `a`, `b` si son nombres | `dst` | Ninguno inferido |
| `IR_JTRUE` / `IR_JFALSE` | condicion `a` | Ninguna | Control |
| `IR_ARG` | `a` | Ninguna | Barrera |
| `IR_CALL dst,a,b` | Argumentos ya observados en `IR_ARG` | `dst` | Memoria desconocida, barrera y control |
| `IR_LOAD dst,a` | referencia `a` | `dst` | Lectura de memoria |
| `IR_STORE dst,a` | referencia `dst`, valor `a` | Ninguna | Escritura de memoria |
| `IR_SPILL_LOAD dst=$rN,a=$spillN` | Ninguno de registro | `$rN` | Lee `$spillN` |
| `IR_SPILL_STORE dst=$spillN,a=$rN` | `$rN` | Ninguna de registro | Escribe `$spillN` |

El test unitario cubre las **24 variantes** del enumerador `IrOp`, entradas
invalidas, literales, repeticion de usos, llamadas y accesos a spilling.

```sh
make test-ir-defuse
make test-ir
make test-spilling
make test-ir-vliw
```

## Limites de la entrega

La API ya es consumida por **DCE** (`dce_calcular_vivas`, eliminacion y
propagacion de constantes) y **reordenamiento** (DAG RAW/WAR/WAW, memoria y
barreras). Las ranuras de spilling se modelan conservadoramente como
`@memoria` en el DAG: **no se afirma independencia entre direcciones**.
`IR_CALL` se trata como lectura y escritura de memoria desconocida y conserva
su barrera original. Ambas pasadas rechazan una instruccion no admitida en
lugar de inventar dependencias.

La transformacion de registros en `src/ir_spilling.c` mantiene su propio
contrato de campos para reescribir operandos; esta API es una consulta,
no sustituye esa reescritura. Para validar el acoplamiento:

```sh
make test-ir-defuse
make test-dce test-reordenamiento test-ide-dce
make test-spilling test-ir-vliw
```

Las pruebas `test_ir_defuse_consumidores.c` son independientes del parser y
verifican vivacidad, recursos del DAG, llamadas, spills y entradas invalidas.
El resto de pruebas completas requieren las herramientas del entorno BAL.

El analisis simbolico no demuestra El analisis simbolico no demuestra
latencias de ISA ni sustituye CFG/DAG fisico, validacion ABI o tratamiento
de aliasing en el consumidor VLIW. Las reglas RAW N+3 son responsabilidad
separada del backend fisico.
