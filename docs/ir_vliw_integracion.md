# Integración incremental de IR, register spilling y VLIW

## Contexto y garantía de alcance

El merge `feature/register-spilling` + `develop` traía IR, spilling, un emisor
LSU y la cadena consumidora VLIW, pero no un *productor físico* desde la IR.
`--ir-spilling` solo imprimía la IR reescrita. Además, los Makefiles de spilling
conservaban rutas anteriores al reordenamiento del árbol de fuentes.

Se corrigieron esas rutas y se agregó `include/ir_vliw.h` /
`src/ir_vliw.c`, con una API independiente y comprobable:

```text
IrPrograma + IrVliwConfiguracion
    -> ir_aplicar_spilling (renombra $tN a $rN, emite $spillN)
    -> ir_spilling_backend_preparar (offsets y tamaño real de frame)
    -> lowering: operaciones ALU/LSU/BRU y símbolos de CFG físicos
    -> cb_ensamblar por operación y sim_decode_palabra por slot
    -> vliw_cadena_construir: metadatos -> CFG -> DAG -> S7
       -> verificación -> S8-A -> codificación CB -> estadísticas/JSON
    -> IrVliwResultado (IR reescrita, métricas, marco, cadena completa)
```

No se crean máscaras de registros ni aristas DAG a mano: el consumidor las
deriva de las palabras ISA codificadas. Las cargas y almacenes reales entran
por **slot LSU**, las aritméticas por **slot ALU** y los saltos por **slot BRU**.
Los inmediatos aritméticos se materializan explícitamente en `x28`/`x29`,
y el DAG descubre también sus dependencias RAW/WAR/WAW. El scheduler preserva
la latencia RAW de tres bundles y los hazards de memoria; no se presume
forwarding.

## API pública

```c
IrPrograma ir = {0};                 /* generada desde AST o construida en C */
IrVliwConfiguracion cfg = {3, 16};  /* 3 registros, 16 bytes preexistentes */
IrVliwResultado out = {0};
if (ir_vliw_desde_ir(&ir, &cfg, &out, stderr) == IR_VLIW_OK) {
    /* out.cadena->asm_texto, out.cadena->codigo, out.cadena->plan */
    ir_vliw_liberar(&out);
}
```

La IR original y los metadatos del resto del compilador no se modifican.
La salida debe empezar vacía y solo se publica si toda la cadena tiene éxito.
En fallo no se entrega ASM/binario, se liberan estructuras intermedias y
se emite un diagnóstico específico cuando es posible.

## Contrato del productor físico **acotado**

| Elemento | Comportamiento verificado |
|---|---|
| `IR_FUNC`, `IR_END_FUNC` | Una única función. Prologo con reserva de SP y guardado de RA; epílogo con restauración de RA y SP. |
| `IR_COPY` | Registros lógicos e inmediatos representables. |
| `IR_BIN` | `gauss` (SUMA) y `neumann` (RESTA) con operandos `$rN` o literales inmediatos. |
| `IR_UNARY` | Negación `neumann`. |
| `IR_SPILL_LOAD`, `IR_SPILL_STORE` | LSU real `cargai`/`guardap`, usando el offset validado por `ir_spilling_offset`. |
| `IR_LABEL`, `IR_JUMP`, `IR_JTRUE`, `IR_JFALSE` | Destinos simbólicos, transferencias CFG completas, reubicación del emisor VLIW. |
| Registros | `$r0..$r7` -> `x16..x23`; `x28`/`x29` auxiliares; `x1=ra`, `x2=sp`, `x0=0`; estos pools no se solapan. |
| Marco | Presupone `bytes_ocupados` completo, incluyendo RA, parámetros y locales existentes; máximo inmediato 1023 para ajuste SP y acceso directo de 0..1020 para spills. |

La función queda como **fragmento** que restaura la pila y luego continúa:
este puente **no añade una terminación inventada** y, por lo tanto, su imagen
`.mem` no es un ejecutable BAL autocontenido. El consumidor VLIW sí produce
bundles codificados, pero el *linker*, arranque, retorno y control de fin BAL
siguen siendo responsabilidad de una integración posterior. Tampoco se
integró una opción nueva de CLI para evitar presentar una compilación parcial
como el backend predeterminado.

## Operaciones no habilitadas

El puente devuelve `IR_VLIW_NO_SOPORTADO`, sin publicar resultados, en caso
de `IR_CALL`, `IR_ARG`, `IR_RETURN`, `IR_PARAM_DECL`, variables BAL por nombre,
accesos a arreglos/listas, divisiones/multiplicaciones/potencias o inmediatos
fuera del rango implementado. No transforma directamente globales, módulos,
referencias compuestas, prólogos/epílogos de múltiples funciones ni controla
clobbers de llamadas. Es intencionado: el backend existente usa `x4..x13`
y guarda RA en `sp+0`, sin política general acreditada de callee-saved.

Antes de reemplazar el `balc` productivo hay que cerrar con el equipo:
identidad de símbolos y funciones, expansión completa de operaciones a ISA,
ABI/call/return con clobbers reales, asignación de memoria local/global,
reubicaciones en ALU, regiones indivisibles y el comparador de resultados
BAL/RTL. No se declara `ABI` completo para código que contenga llamadas.

## Pruebas y reproducibilidad

```sh
make test-spilling
make test-spilling-backend
make test-ir-vliw
```

`test-ir-vliw` no depende de Flex/Bison: compila la IR, spilling, marco, CB,
S2, el CFG/DAG físicos y la cadena VLIW **real** con GCC/C11 en un temporal.
Pruebas incluidas:

1. Presión de siete temporales con tres registros, desalojos, cargas y
   escrituras y contrastes del plan, DAG y palabras decodificadas.
2. Condicional y salto incondicional: etiquetas en CFG y destino reubicado.
3. Backedge condicionado con inmediato: guardas y dirección de retorno de ciclo.
4. Rechazo de llamadas, offsets imposibles e inmediatos fuera de rango sin
   generar binario o dejar resultados parciales.

Estos tests verifican la **frontera física acotada**, no que factorial, matriz
o todo el lenguaje BAL pasen ahora por IR -> VLIW. Se recomienda integrar
el siguiente tramo en un PR distinto con benchmarks RTL/ejecución observables.

## Conexión explícita con `balc` (modo experimental)

El compilador ahora ofrece una ruta **opt-in** `--vliw-ir[=N]`, que pasa por
la misma visita semántica del compilador principal, obtiene el marco de pila
con `mem_asignar_proyecto`, construye la IR desde el AST y llama al productor
físico `ir_vliw_desde_ir`. `N` admite 3 a 8 registros temporales (predeterminado
3). No modifica el camino `balc archivo.bal` ni el encoder/ABI tradicionales.

```sh
make
./balc --vliw-ir=3 -v -s -p examples/vliw_ir_preview.bal
make test-ir-vliw-flags
make test-ir-vliw-cli
```

En éxito, `-s` escribe `ejemplo.vliw.asm` (bundles calendarizados) y
`ejemplo.vliw.mem` (palabras de instrucción para inspección). `-p` muestra
el ensamblador calendarizado y la codificación física; `-v` también muestra
la IR después del spilling; `-t` añade el AST. La CLI informa tamaño del marco,
registros, ranuras, cargas, stores y cantidad de bundles.

**Advertencia:** `.vliw.mem` **no es** un programa ejecutable completo ni un
archivo `.bin` con formato BALB: no contiene arranque, enlazado completo ni
fin/retorno funcional. El modo prohíbe `-o`, `-x`, `-m`, el resto de pasadas IR
y los modos JSON. Tampoco promete soportar factorial o programas generales.
Cuando se encuentra una operación/operando sin lowering acreditado, el comando
falla con estado distinto de cero **sin publicar los fragmentos**.

Las pruebas de CLI que usan `balc` requieren las dependencias normales del
repositorio (Flex, Bison, `uthash-dev`). El test del puente C
`make test-ir-vliw` continúa siendo independiente de Flex/Bison; por tanto,
en entornos sin generadores se puede ejecutar esa prueba y compilar
`cli.c` como unidad de traducción, pero no demostrar la CLI completa.

### Trabajo aún pendiente para el backend productivo

El modo experimental **no reemplaza** el backend por defecto. Para ello se
necesita lowering semántico de variables por alcance y memoria, parámetros,
llamadas, retornos, arrays, opcodes faltantes; asignación ABI y preserves,
arranque y finalización, y validación por simulación/RTL del programa completo.
El productor de una única función se mantiene deliberadamente restrictivo.

## Productor de programas completos

`ir_vliw_programa()` complementa la API de fragmentos anterior. Integra IR optimizada, marcos particulares, funciones y enlace. Véase [contrato productivo](vliw/integracion-productiva-o2.md). Los rechazos del productor de fragmentos se conservan.
