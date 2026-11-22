# Proyecto II — Contrato de representacion intermedia (IR)

Este documento describe la **IR de tres direcciones que implementa el repositorio**,
sus bloques basicos y las interfaces con register spilling y VLIW. Es el contrato
que consumen las optimizaciones; no debe confundirse la disponibilidad de una
operacion en la IR con su traduccion a codigo maquina ejecutable.

## 1. Modulos y estructuras publicas

| Archivo | Responsabilidad |
|---|---|
| `include/ir.h` | Tipos `IrOp`, `IrInstr`, `IrBloque`, `IrPrograma` y API de IR. |
| `src/Representacion_Intermedia/ir.c` | Construccion desde AST, CFG e impresion. |
| `include/ir_spilling.h`, `src/ir_spilling.c` | Asignacion limitada de temporales y operaciones de derrame. |
| `include/ir_spilling_marco.h`, `src/ir_spilling_marco.c` | Ranuras logicas, offsets y acceso LSU. |
| `include/ir_spilling_backend.h`, `src/ir_spilling_backend.c` | Preparacion de marco, prologo/epilogo y emision LSU. |
| `include/ir_spilling_memoria.h`, `src/ir_spilling_memoria.c` | Verificacion del marco **por funcion** a partir de la tabla de simbolos ya asignada. |
| `include/ir_vliw.h`, `src/ir_vliw.c` | Puente experimental IR -> instrucciones fisicas -> consumidor VLIW. |

`IrPrograma` contiene `codigo` (arreglo ordenado de `IrInstr`), `cantidad`,
`capacidad`, contadores de `temporales` y `etiquetas`, y `bloques` con
`cantidad_bloques`. Cada `IrInstr` contiene:

- `op`: enumerador `IrOp` que identifica la operacion.
- `dst`: destino de valor, referencia de escritura, etiqueta o nombre declarado,
  **segun `op`**; no siempre es una definicion de registro.
- `a`, `b`: operandos, valores, indices, longitudes, nombres o cantidades,
  segun la instruccion.
- `operador`: operador aritmetico, logico o relacional en `IR_BIN`/`IR_UNARY`.
- `linea`: linea de origen conservada para diagnosticos y visualizacion.

Los campos de texto son copias propias de la IR: no dependen de la vida del
AST. `IrPrograma` se inicializa con `{0}` y se libera con `ir_liberar`.
`ir_desde_ast(ast, &ir, diagnosticos)` devuelve **0** si genero la IR y el
CFG, **1** cuando encuentra un nodo no soportado y **2** ante un problema de
argumentos/memoria; un fallo libera la salida parcial. No modifica el AST.

## 2. Temporales, nombres y registros

- Los nombres de variables y funciones provienen del AST y se conservan como
  nombres **simbolicos**: no son direcciones ni registros fisicos.
- `$t1`, `$t2`, ... son **temporales virtuales** generados para resultados
  intermedios. `$L1`, `$L2`, ... son **etiquetas de control**. Estos prefijos
  no pueden escribirse como identificadores normales del lenguaje BAL.
- Los operandos pueden ser identificadores, temporales, referencias logicas o
  literales (`10`, `declare_true`, etc.). La representacion conserva el texto
  de caracteres/cadenas; la conversion a datos maquina es responsabilidad del
  backend.
- Tras `ir_aplicar_spilling`, los `$tN` pasan a `$rN` (**registros logicos
  de trabajo**), y las ranuras `$spillN` designan almacenamiento en el marco.
  `$rN` **no** equivale por si solo a un numero de registro del procesador.
- El productor VLIW experimental mapea `$r0..$r7` a `x16..x23`, pero solo
  dentro de su subconjunto soportado. La asignacion fisica general y el ABI
  para todas las funciones siguen pendientes.

### Ejemplo de tres direcciones

Fuente BAL (esquematica):

```bal
create_funk #declare_infinite_void# main() {
    x * declare_int : 1;
    x : x gauss 2;
}
```

IR correspondiente (sin mostrar bloques ni numeros de linea):

```text
func main
x = 1
$t1 = x gauss 2
x = $t1
endfunc main
```

La descomposicion evita generar una instruccion ISA directamente por cada nodo
AST: las optimizaciones pueden trabajar primero sobre operaciones simbolicas.

## 3. Catalogo y semantica de operaciones (`IrOp`)

Las columnas emplean los **nombres reales de los campos** de `IrInstr`.
Los campos no mencionados quedan vacios (`NULL`) salvo el `operador` indicado.

| Familia | Operacion | Campos principales y significado |
|---|---|---|
| Funciones | `IR_FUNC` / `IR_END_FUNC` | `dst`: nombre de la funcion; delimitan su IR. |
| Funciones | `IR_PARAM_DECL` | `dst`: nombre del parametro formal. |
| Valores | `IR_COPY` | `dst = a`: asignacion o inicializacion escalar. |
| Valores | `IR_BIN` | `dst = a operador b`: expresion binaria de tres direcciones. |
| Valores | `IR_UNARY` | `dst = operador a`: expresion unaria; `bool` normaliza a 0/1. |
| Saltos | `IR_LABEL` | `dst`: etiqueta definida en la posicion actual. |
| Saltos | `IR_JUMP` | `dst`: etiqueta destino, salto incondicional. |
| Saltos | `IR_JFALSE` / `IR_JTRUE` | `a`: condicion; `dst`: etiqueta a la cual salta si es falsa/verdadera. |
| Llamadas | `IR_ARG` | `a`: argumento evaluado en orden de aparicion. |
| Llamadas | `IR_CALL` | `dst`: resultado temporal; `a`: funcion; `b`: numero de argumentos. |
| Llamadas | `IR_RETURN` | `a`: valor opcional a retornar. |
| Arreglos | `IR_ARRAY_DECL` | `dst`: nombre; `a`: numero de dimensiones. |
| Arreglos | `IR_ARRAY_DIM` | `dst`: arreglo; `a`: indice de dimension; `b`: longitud evaluada. |
| Arreglos | `IR_INDEX_REF` | `dst`: referencia logica; `a`: base; `b`: indice evaluado. |
| Memoria logica | `IR_LOAD` | `dst`: valor leido; `a`: referencia indexada. |
| Memoria logica | `IR_STORE` | `dst`: referencia indexada; `a`: valor que se escribe. |
| Listas | `IR_LIST_DECL` | `dst`: nombre de la lista. |
| Listas | `IR_LIST_ADD` / `IR_LIST_REMOVE` | `dst`: lista/referencia; `a`: elemento u operando. |
| Listas | `IR_SIZE` | `dst`: resultado temporal; `a`: coleccion consultada. |
| Spilling | `IR_SPILL_LOAD` | `dst=$rN`, `a=$spillN`: recarga una ranura al registro logico. |
| Spilling | `IR_SPILL_STORE` | `dst=$spillN`, `a=$rN`: almacena un registro logico en una ranura. |

`IR_INDEX_REF` crea una **referencia abstracta** (tambien para indices
anidados de matrices). `IR_LOAD`/`IR_STORE` de colecciones **no son** las
instrucciones fisicas `cargai`/`guardap`: aun requieren layout, tipos y
lowering de memoria. Las inicializaciones de arreglos producen referencias y
almacenamientos en el orden del literal; las listas producen operaciones
abstractas de modificacion. La presencia de estos `IrOp` no certifica
que el backend VLIW pueda ejecutarlos.

## 4. Saltos, funciones y flujo de control

El generador recorre funciones del AST principal y emite `IR_FUNC`,
`IR_PARAM_DECL` (en el orden formal), sentencias, retornos y `IR_END_FUNC`.
Para llamadas locales evalua argumentos en orden, emite un `IR_ARG` por
argumento y despues `IR_CALL`; conserva el resultado temporal aun si la
llamada aparece como sentencia. Las llamadas a miembros de modulos importados
y el enlace de cuerpos importados **no** estan resueltos por esta ruta IR.

- `whether`/`alif`/`also`: `IR_JFALSE`, etiquetas y saltos hacia la union.
- `whale`: etiqueta de condicion, salto hacia salida, cuerpo y salto de
  retorno hacia el encabezado.
- `cycle`: inicializacion del iterador, comparacion con limite **exclusivo**,
  cuerpo, incremento y salto hacia la condicion. Solo acepta paso constante
  positivo en esta etapa.
- `&&` y `||`: cortocircuito mediante etiquetas y `IR_JFALSE`/`IR_JTRUE`;
  el operando derecho no se evalua si ya se conoce el resultado.

### Bloques basicos y CFG

`ir_bloques_basicos` construye `IrBloque {inicio, fin, sucesores,
cantidad_sucesores}`. El intervalo `[inicio, fin)` incluye `inicio`, pero no
`fin`. Son lideres el inicio del programa, las etiquetas, las funciones y
la instruccion siguiente a un salto, `IR_RETURN` o `IR_END_FUNC`.

Una arista del CFG representa el destino de `IR_JUMP` o del salto condicional;
los condicionales tambien conservan la continuidad hacia el siguiente bloque.
No hay caida implicita hacia la **siguiente funcion** ni sucesores despues de
un retorno o final de funcion. El CFG es **simbolico**, previo al CFG fisico
que usa la calendarizacion VLIW.

## 5. Register spilling y contrato de memoria

`ir_aplicar_spilling(&original, &reescrita, registros, &metricas)` deja la IR
original intacta, recibe un programa con bloques basicos validos y genera
otro `IrPrograma`. El modulo C admite **3 a 32** registros de trabajo;
la opcion experimental de CLI restringe el rango a **3 a 8** porque su
productor fisico mapea ese subconjunto a `x16..x23`.

La pasada inventaria los `$tN`, asigna ranuras logicas `$spillN`, elige
registros de trabajo con una politica de reutilizacion por uso reciente,
inserta `IR_SPILL_LOAD`/`IR_SPILL_STORE` cuando corresponde y reconstruye
el CFG de la salida. Al finalizar un bloque guarda temporales modificados;
al entrar en otro invalida su estado local. Las llamadas fuerzan guardar e
invalidar. `IrSpillingMetricas` comunica `registros`, `ranuras`, `cargas`,
`almacenes` y `desalojos`. No se puede aplicar la pasada por segunda vez a
una IR que ya contenga operaciones de spilling.

Las ranuras son **logicas**, no offsets asignados por el frontend:

1. `ir_spilling_backend_desde_tabla` consulta una tabla de simbolos cuya
   memoria ya fue asignada y valida los espacios de **una funcion**.
2. Los slots se reservan despues de RA, parametros y locales; se conserva el
   marco original y se calculan offsets de palabra. `ir_spilling_offset`
   comprueba que cada acceso directo LSU este dentro de rango.
3. El backend debe usar el **mismo tamaño final de marco** para prologo,
   epilogo y ubicacion de argumentos recibidos por pila.

**Limite multifuncion:** la pasada actual numera `$spillN` globalmente dentro
de `IrPrograma`, no reinicia la numeracion por funcion. Hasta que exista una
particion/renumeracion verificada, se debe reservar el total de ranuras en
cada marco individual, aunque algunas no se utilicen. La ruta `--vliw-ir`
actual todavia **no** consume el adaptador por funcion para traducir variables
BAL. Para ejemplos, reglas de offsets, limites LSU y precauciones sobre
`char`/`boolean`, ver [contrato de memoria](contrato_memoria_spilling.md).

## 6. Integracion con optimizaciones y VLIW

La IR es independiente de la ISA. DCE y otras transformaciones pueden
consumirla; no deben eliminar ni adelantar operaciones con efectos sin un
analisis que lo justifique. En especial, llamadas y argumentos, operaciones
de memoria y modificaciones de colecciones requieren tratamiento conservador.
El análisis de defs/uses de DCE no constituye todavia una **API publica
unificada** de definiciones y usos para todas las instrucciones (issue #7).
Las latencias y la regla RAW N+3 corresponden al contrato del consumidor
**fisico** VLIW, no a una propiedad implícita de esta IR (issue #3).

El productor experimental `ir_vliw_desde_ir` conecta la IR reescrita con
instrucciones fisicas ALU/LSU/BRU, CFG/DAG fisico, calendarizador y bundles.
Actualmente admite **una funcion acotada**, operaciones aritmeticas simples,
saltos y spilling representables; rechaza nombres de variables BAL, llamadas,
retornos, colecciones y operaciones sin lowering acreditado. Su salida
`.vliw.asm`/`.vliw.mem` es un **fragmento**, no un ejecutable BALB completo.
El backend tradicional de `balc` no ha sido reemplazado.

El alcance exacto, el mapeo de registros, las dependencias y los rechazos
se documentan en [integracion IR/VLIW](ir_vliw_integracion.md).

## 7. Comandos y pruebas reproducibles

```sh
make balc
make test-ir
make test-spilling
make test-spilling-memoria
make test-ir-vliw
make test-ir-vliw-cli

./balc --ir examples/factorial.bal
./balc --ir-spilling examples/factorial.bal
./balc --vliw-ir=3 -v -s -p examples/vliw_ir_preview.bal
```

- `--ir`: valida semantica con `sem_visitar_proyecto` y muestra la IR/CFG
  del AST principal. No publica `.asm` ni `.bin`.
- `--ir-spilling`: muestra la IR transformada con **3** registros de trabajo;
  es una inspeccion, no una compilacion con el backend nuevo.
- `--vliw-ir=N`: modo separado para el subconjunto soportado. `-v` muestra
  la IR tras spilling, `-p` los bundles y `-s` guarda fragmentos
  `.vliw.asm` y `.vliw.mem`. Rechaza combinaciones incompatibles y programas
  sin traduccion fisica soportada.

`make balc` y la prueba CLI requieren el entorno normal del proyecto
(GCC, Flex, Bison y `uthash-dev`). Las pruebas C independientes permiten
validar IR/spilling sin ejecutar el compilador completo.

## 8. Limites y responsabilidades

El generador IR cubre funciones locales, parametros, escalares, control,
colecciones y accesos indexados **a nivel de representacion abstracta**.
Rechaza declaraciones globales, enlace IR de funciones importadas,
expresiones no implementadas y `cycle` con paso dinamico o negativo. Las
referencias simbolicas, el layout de memoria, llamadas y retornos productivos,
y la codificacion de strings/caracteres siguen necesitando trabajo conjunto.

Dylan mantiene IR, spilling y su contrato de memoria; Jose consume IR para
DCE; Meibel para unrolling/reordenamiento; Javier integra lowering,
calendarizacion y emision VLIW. El cierre de la documentacion de IR (#2)
**no implica** el cierre de la API compartida defs/uses (#7), del contrato de
latencias/efectos (#3), ni del lowering productivo (#25).
