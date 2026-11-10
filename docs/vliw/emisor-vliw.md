# Emisor de bundles VLIW — S8-A

S8-A convierte un `VliwPlan` verificado de S7 en ensamblador para el codificador
existente. Conserva todos los bundles, sus cuatro slots y los NOP de protección.
No calendariza nuevamente, no analiza dependencias ni integra la CLI o la IR.

## API y propiedad

```c
#include "vliw_emisor.h"
char *ensamblador = NULL;
VliwEmisorDiagnostico diagnostico;
VliwEmisorEstado estado = vliw_emitir(&entrada, plan, &destinos,
                                     &ensamblador, &diagnostico);
/* Usar ensamblador solo si estado == VLIW_EM_OK. */
vliw_emisor_liberar(&ensamblador);
```

`VliwEntrada`, `VliwPlan`, textos y `VliwDestinos` son prestados e inmutables
mientras dura la llamada. Sus cantidades deben describir almacenamiento C
realmente accesible. Cada texto debe terminar en NUL. Los índices son posiciones
en el arreglo original, independientes de los identificadores externos `id`.

La salida requiere un `char **` válido que apunte inicialmente a NULL. En éxito
contiene una cadena propia, independiente de las entradas; se libera con
`vliw_emisor_liberar`, que deja el puntero en NULL y admite NULL. En error no se
publica un prefijo ni se modifica una salida preexistente. El diagnóstico es
opcional, pertenece al llamador y conserva estado, índice, bloque, estado S7 y
mensaje. `VLIW_VACIO` identifica índices globales/no aplicables.

Un programa vacío produce una cadena asignada `""`. No se invoca CB para ese
caso porque el codificador rechaza programas vacíos. El emisor no escribe MEM,
BIN ni archivos de salida del usuario.

## Algoritmo y validaciones

1. Ejecutar `vliw_verificar` antes de utilizar el layout. S7 valida metadatos,
   capacidad, índices, slots, mapas, conteos, CFG, terminadores y guardas.
2. Validar textos y construir un mapa de destinos por índice original. Rechazar
   registros duplicados, BRU sin destino, asociaciones a operaciones sin control
   y sucesores duplicados.
3. Contrastar forma BRU, tipo de transferencia, tomado y caída con el CFG.
4. Recorrer los bundles y sus slots sin mover operaciones. Emitir etiquetas en
   `inicio_por_bloque` y una única línea de cuatro slots por bundle.
5. Ensamblar el texto completo con `cb_ensamblar`. CB comprueba sintaxis ISA,
   operandos, rangos, alineación, slots y riesgos/latencias por sus caminos.
6. Decodificar cada palabra real con S2. Comprobar que es una operación no NOP,
   que sus máscaras físicas coinciden y que los efectos declarados cubren sus
   efectos reales. Publicar la cadena únicamente después de estas verificaciones.

La comprobación S2 excluye x0 de las máscaras y reconoce pares Feistel. No
reconstruye aristas RAW/WAR/WAW: el productor continúa siendo responsable de
entregar metadatos y CFG completos. Las operaciones criptográficas deben declarar
los efectos de memoria, bóveda, estado y excepción correspondientes; se admiten
efectos conservadores adicionales. No se sustituyen metadatos incorrectos.

## Texto y formato

Cada operación real ocupa menos de 160 bytes, conforme al codificador actual.
Se rechazan textos vacíos, caracteres de control salvo tabulación, caracteres
fuera de ASCII imprimible, `|`, `:`, `#`, `;`, `/` y barra inversa. De ese modo un
texto no puede introducir etiquetas, líneas, comentarios ni slots adicionales.
Los NOP solo se obtienen de posiciones vacías del plan; una operación real cuyo
texto sea `nop` se rechaza. Los mnemónicos normales se validan con CB, sin copiar
su parser ni tablas ISA.

Una línea siempre tiene el orden `ALU | LSU | BRU | CRIPTO`. El texto normal se
conserva byte a byte. Solo las transferencias directas sustituyen el último
operando por la etiqueta destino: se conserva el prefijo hasta la última coma.
El reconocimiento BRU se limita a las cuatro condiciones ISA, `sye`, `syer` y
`ret`, comprobando cantidad de operandos y forma léxica del destino descartado.
Nunca convierte ese destino viejo a un PC ni lo usa para inferir un bloque.

Ejemplo real del fixture RAW, con productor y consumidor separados N+3:

```asm
__vliw_b0:
sumai x5,x0,7 | cargai x8,x0,0 | nop | nop
nop | nop | nop | nop
nop | nop | nop | nop
suma x6,x5,x8 | nop | nop | nop
```

## Destinos estructurados y CFG

Cada `VliwDestino` contiene `instruccion`, `tipo`, `tomado` y `caida`.

| Tipo | Texto admitido | Tomado | Caída y CFG |
|---|---|---|---|
| `DIRECTO` | `sye rd,destino` | Bloque válido | Salto: caída VACIO; llamada: siguiente bloque o VACIO al final. CFG exactamente destino más continuación cuando exista. |
| `CONDICIONAL` | `igualsi`, `igualno`, `menora`, `mayoroigual` | Bloque válido | Siguiente bloque o VACIO al final. CFG exactamente tomado más caída. |
| `INDIRECTO` | `syer rd,rs,imm` | VACIO | Llamada: continuación secuencial incluida en CFG. Otro salto: caída VACIO. |
| `RETORNO` | `ret` o `syer` con control RETORNO | VACIO | Caída VACIO; CFG puede enumerar retornos posibles. |
| `NO_RESUELTO` | Transferencia pendiente | — | Rechazo explícito; requiere resolución externa. |

Los sucesores se interpretan como conjunto, no por orden. Tomado y caída pueden
ser el mismo bloque. Para indirectos/retornos, el programa debe construir una
dirección dinámica correcta para su layout; S8-A no relocaliza enteros, punteros
a código ni tablas de saltos. La validez de los destinos dinámicos sigue siendo
responsabilidad del productor. No se transforma `syer` en un salto directo.

Los nombres reservados `__vliw_bN` son únicos por índice y válidos para CB. Como
el texto de entrada no puede declarar etiquetas, no hay colisiones externas.
Cada etiqueta ocupa cero bytes y precede exactamente al bundle indicado:
`PC_inicio[b] = 16 * inicio_por_bloque[b]`, con origen PC=0. Los bloques protegidos
comienzan con sus dos NOP, incluido B0 cuando recibe un backedge. El flush no
reemplaza esas guardas.

En el diamante tomado, B0 salta a B2 y ambas ramas confluyen en B3:

```asm
__vliw_b2:
nop | nop | nop | nop
nop | nop | nop | nop
sumai x6,x0,2 | nop | nop | nop
__vliw_b3:
nop | nop | nop | nop
nop | nop | nop | nop
suma x7,x5,x6 | nop | nop | nop
```

## N+3, CRIPTO y límites

La emisión conserva posiciones, distancias y orden. Las seis operaciones `fsl`,
`fsli`, `ell`, `vcr`, `camcon`, `setpwd` conservan operandos y separaciones S7;
no se optimizan bóveda, contraseña, AUTH/INIT ni excepciones. Un bundle con los
cuatro slots útiles simultáneamente no es un plan legal de S7 cuando incluye
sus barreras CRIPTO/BRU. Se verifica el orden de los cuatro slots y cada unidad
en planes legales; no se inventa un plan coemitido que el verificador rechazaría.

El límite es 1023 bundles ordinarios, contando vacíos y protección. Los 16 bytes
del bundle físico 1023, en 0x3FF0, quedan reservados al vector de excepción. CB
resuelve los inmediatos desde el PC de la BRU: condicional de 11 bits y sye de 16 bits,
alineados a 16 bytes. Si no cabe, `VLIW_EM_RANGO` informa que requiere relajación
posterior. No se trunca ni expande el salto, ni se modifica el layout.

## Errores y memoria

Los estados distinguen argumentos, plan, texto, destino, no resuelto, rango,
codificador, metadatos, memoria y E/S. El crecimiento del buffer comprueba
sumas/terminador y conserva la reserva anterior si falla `realloc`. Todos los
caminos liberan mapas, buffers y `CbPrograma`. Hay un único punto de publicación.

CB solo ofrece retorno entero y diagnóstico textual. Se captura mediante
`tmpfile` C11 privado; fallos de creación, escritura/posicionamiento, lectura o
cierre dan `VLIW_EM_IO`. Las cadenas actuales `sin memoria` y `salto fuera de
rango` permiten distinguir esos dos estados de CB. Si cambia su diagnóstico,
se debe revisar esta traducción; un rechazo no reconocido sigue siendo error
`VLIW_EM_CODIFICADOR`, nunca éxito. No se altera su API protegida.

## Pruebas reproducibles

Desde la raíz del compilador, con Arquitectura e Icarus disponibles:

```bash
PYTHONDONTWRITEBYTECODE=1 \
ARQUITECTURA_ROOT=/ruta/Proyecto2_Arquitectura1 \
S8_EVIDENCIA=/tmp/s8-evidencia.json \
S8_ARTEFACTOS=/tmp/s8-ejemplos \
python3 tests/vliw/test_vliw_emisor.py
```

No requiere `make` ni modifica fuentes/artefactos del repositorio. Compila C11
con `-Wall -Wextra -Wpedantic -Werror -O2`. La suite reutiliza fixtures/oráculos
S7, el codificador y las sondas C/RTL de S6 intactas. Los metadatos y destinos de
los fixtures son explícitos. El comparador conservador es exclusivamente de
pruebas: una operación y dos NOP, con etiquetas resueltas por el mismo CB.

| Cobertura solicitada | Evidencia |
|---|---|
| T1–T4 | Texto exacto de una operación, ALU+LSU, vacíos y RAW N+3. |
| T5–T10 | Etiquetas, inmediatos decodificados, CFG con tomado no primero, diamantes, bucles, B0 protegido, múltiples predecesores. |
| T11–T13 | RAW/WAR/WAW, seis formatos CRIPTO, posiciones de los cuatro slots. |
| T14–T19 | Texto ausente, índices/destinos/CFG inválidos, rango, 1023 admitidos y 1024 rechazados. |
| T20–T25 | 20 emisiones deterministas, mapas/slots/conteos/guardas corruptos, reservas y E/S, inyección textual, vacío, snapshots de entrada y plan. |

Las ejecuciones normales comparan registros, DMEM completa no nula, estado,
bóveda y operaciones efectivamente llegadas a WB. Exigen rutas del diamante,
tres iteraciones de bucles y consumidores previstos. La llamada vuelve a las
guardas de su continuación; el fixture borra ra al salir para comparar estados
entre layouts con PC diferentes. Hay una prueba separada de FSL sin AUTH que
comprueba excepción, vector y anulación de destinos. BIN se contrasta byte a
byte contra MEM; la carga ejecutada es MEM mediante S1. BIN es imagen IMEM sin
encabezado, no un contenedor BALB.

Resultado final: 11 tests Python, 21 escenarios C y 1193 comprobaciones C; 96
posiciones de fallo de reserva (14 efectivas) y 4 fallos de E/S, todos PASS.
11 planes, 21 ejecuciones S6/3605 ciclos y 11 programas RTL/1805 ciclos. Los
contrastes RTL comparan cada campo de cada flanco y DMEM final.
ASan+UBSan y UBSan independiente pasan la suite; LeakSanitizer explícito retorna 1
con error de ptrace del entorno: **NO VERIFICADO**, no se afirma ausencia de
fugas por LSan. Regresiones S1–S7: 91 tests Python PASS antes y después, más sus
oráculos C/RTL. `make -B -k test` también se ejecuta en copias desechables para
preservar los artefactos versionados del repositorio.

## Tamaño y ocupación medidos

Ocupación media = operaciones útiles / bundles totales, incluidos NOP.
Bytes IMEM = 16 × bundles. La reducción compara contra 3 bundles por operación.

| Fixture | Ops | Bundles S8-A | Vacíos | Ocupación media | Bytes | Bundles conservador | Reducción |
|---|---:|---:|---:|---:|---:|---:|---:|
| RAW ALU+LSU |3|4|2|0,750|64|9|55,56%|
| Memoria |5|8|3|0,625|128|15|46,67%|
| Diamante (cada ruta) |6|12|6|0,500|192|18|33,33%|
| Bucle (cada variante) |4|10|6|0,400|160|12|16,67%|
| CRIPTO y par |6|16|10|0,375|256|18|11,11%|
| Seis CRIPTO |10|28|18|0,357|448|30|6,67%|
| ALU independientes |3|3|0|1,000|48|9|66,67%|
| Llamada/retorno |7|15|8|0,467|240|21|28,57%|

Son mediciones estáticas de tamaño/ocupación. El presupuesto de ejecución es
fijo para comparar trazas y estados; estos resultados no miden aceleración ni
ciclos hasta terminación, ni justifican una mejora de IPC del procesador.

## Alcance pendiente

S8-A está disponible como biblioteca independiente y fixtures controlados.
No hay cambios de ISA, RTL, S1–S7, CLI, Makefile ni archivos de compañeros.
S8-B debe acordar productores reales de instrucciones, dependencias completas,
CFG y destinos; integrar emisión/codificación con el backend y tratar saltos
largos sin perder verificación/layout. La relajación y reubicación general,
punteros dinámicos a código, enlazado y política del handler no se implementan.
S9, optimizaciones de IR/AST, registros, cachés e IDE permanecen fuera del alcance.
