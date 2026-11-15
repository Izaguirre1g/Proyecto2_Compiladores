# Contrato de memoria por funcion: tabla de simbolos -> register spilling

## Responsabilidad de Dylan (productor)

`mem_asignar_proyecto` / `mem_asignar_marco` asignan los offsets de memoria
antes de que se materialicen los accesos a los temporales derramados.
El nuevo adaptador `include/ir_spilling_memoria.h` y `src/ir_spilling_memoria.c`
consumen la **tabla ya asignada**, sin mutarla, y preparan un
`IrSpillingBackend` **por nombre de funcion**. Rechazan tablas incompletas,
locales/parametros con offsets incompatibles con `memory.c`, tamaños distintos
(incluidas matrices estáticas), memoria sin asignar, solapes o ranuras LSU no
representables directamente. En error no publican un marco parcial.

Secuencia reproducible:

```c
/* t validada semanticamente; distribucion global+local ejecutada. */
MemProyectoMapa mapa = {0};
if (mem_asignar_proyecto(&t, 0x4000, 0x10000, 65536, &mapa) != MEM_PROYECTO_OK)
    /* error */;

/* Despues de ir_aplicar_spilling: metricas.ranuras es el numero real de slots
 * que debe reservarse en esta funcion cuando se use una IR de una funcion.
 * La pasada actual cuenta temporales de todo IrPrograma: con multiples
 * funciones hay que reservar ese total en cada marco, salvo que se
 * realice una particion con renumeracion validada de $spillN.
 * No usar mapa.marco_maximo_bytes: es el maximo de todo el proyecto. */
IrSpillingBackend backend = {0};
if (ir_spilling_backend_desde_tabla(&t, "main", metricas.ranuras,
                                     &backend) != 0)
    /* incompatibilidad de ABI/offsets */;
```

La pasada `ir_aplicar_spilling` actual asigna índices `$spillN` globales a
**todo el `IrPrograma`**, no reinicia el inventario en `IR_FUNC`. Para una
IR multifunción la opción segura es reservar `metricas.ranuras` en cada
marco, aunque algunos queden sobreaprovisionados. Reducir las ranuras por
función exige un pase posterior que demuestre máximos índices por función
y/o reenumere todos los `$spillN` con una nueva validación.

La interfaz preexistente `ir_spilling_backend_preparar(bytes_existentes, ... )`
sigue disponible para fixtures físicos que no tengan una tabla semántica.
**No confundirla con un contrato certificado con variables BAL.**

## Disposición de pila

La ABI escalar definida en `memory.h` mantiene `sp` descendente:

| Intervalo tras el prólogo | Contenido | Propietario |
|---|---|---|
| `[sp+0,sp+4)` | Dirección de retorno (x1) | `funciones_marco` |
| `[sp+4,sp+4+4P)` | P home slots de parámetros | `memory.c` |
| `[sp+4+4P,sp+marco_original)` | Locales y arreglos de tamaño conocido, con alineación | `memory.c` |
| `[sp+marco_original,sp+marco_original+4S)` | Ranuras `$spill0..$spill(S-1)` | Dylan |

`marco_original` ya incluye padding final a 4. Los home slots se conservan,
no se reenumeran parámetros ni se desplazan variables locales. Cada función
tiene su marco propio, aunque otra tenga un marco más grande.

Ejemplo real de prueba: función `calcular` con 5 parámetros, `char`, `int`,
arreglo `int[3]` y un `int` de bloque anidado:

- RA: offset 0; parámetros: offsets 4, 8, 12, 16 y 20.
- Locales: `char` 24, `int` 28, arreglo 32..43, anidado 44..47.
- Marco original: 48 bytes; dos spills en offsets 48 y 52.
- **Marco definitivo: 56 bytes**. `mem_abi_argumento(4, 56)` encuentra el
  quinto argumento entrante en `[sp+56]`, no `[sp+48]`.

Todas las cargas y almacenes de spills usan `cargai/guardap` con base `x2`
y desplazamientos de palabra 0..1020, comprobados antes de la emisión. El
prólogo y epílogo usan exactamente `backend.marco.bytes_marco`; el ABI para
argumentos 4+ debe recibir también ese **mismo marco final**. El emisor de
prólogo conserva `x1` y maneja el home de parámetros; el llamador debe
respetar la convención de llamada y los registros reservados de VLIW.

## Punto de entrega para Javier

Dylan entrega: `IrPrograma` reescrito, `IrSpillingMetricas.ranuras`,
`IrSpillingBackend`, desplazamientos validados de `$spillN` y el contrato
`IR_SPILL_LOAD(dst=$rN,a=$spillN)` / `IR_SPILL_STORE(dst=$spillN,a=$rN)`.
En el backend físico: `$r0..$r7` se asignan a `x16..x23`, `x28/x29` son
auxiliares y el LSU está en slot 1. El backend debe respetar aliasing de
memoria, dependencias, barreras de llamadas y sus propias reubicaciones.

**No se declara aquí completo** el lowering de variables locales/globales BAL,
`CALL`, `RET`, parámetros y funciones múltiples ni la ejecución del programa
completo. El CLI `--vliw-ir` continúa siendo una vista previa de fragmentos:
su ruta actual no consume aún este adaptador por función. Cuando se conecte
el lowering de variables se debe sustituir el marco global aproximado
(`mapa.marco_maximo_bytes`) por la API anterior con la función correcta.

## Pruebas

```sh
make test-spilling-memoria
make test-spilling
make test-ir-vliw
```

`test_ir_spilling_memoria.c` usa la tabla de símbolos y `memory.c` reales,
prueba globales, dos funciones con distintos tamaños de marco, cinco
parámetros, locales anidados, arreglos estáticos, acceso LSU, offsets del ABI
y rechazos transaccionales ante offsets/tamaños corruptos o presión excesiva.

**Precaución:** en `memory.c`, `char` y `boolean` pueden ocupar un solo
byte, aunque `cargai`/`guardap` acceden una palabra completa. El adaptador
certifica posiciones, no autoriza al backend a usar stores de 32 bits sobre
variables subpalabra adyacentes: su lowering debe conservar los límites reales
o establecer antes una ABI de padding consistente.
