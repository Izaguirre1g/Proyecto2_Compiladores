# S6: unidad criptográfica del simulador VLIW

## Alcance y referencia

S6 completa las seis instrucciones del slot 3 sobre el pipeline IF–ID–EX–WB
aprobado en S3–S5. Conserva las 34 instrucciones reconocidas por S2, el cargador
S1, ALU, LSU, BRU y la ISA. No incorpora calendarización automática, forwarding,
scoreboard, HALT, cachés, IDE ni nuevos formatos binarios.

Base de Compiladores: `feat/simulador-vliw`,
`5a7f23d5b9186d37b389f4ea960f4d6a201ccd58`, verificado localmente y en GitHub.
Referencia de Arquitectura: `418c92bb73d3f34fb02f2feca384a83b86a589d9`, sin modificar.
Se estudiaron `crypto_unit`, `key_vault`, `decoder_crypto`, conexiones de `top`,
`memory`, `wb`, `regfile`, registros del pipeline, `pc_branch`, `isa_defs` y la
documentación ISA/microarquitectura/simulación. Las prioridades y los tiempos
se contrastaron con sus asignaciones reales y con ejecuciones del RTL.

## Organización y API

`sim_crypto.h/c` mantiene cálculo criptográfico y efectos de bóveda separados
del control global. El pipeline coordina operandos, permisos, PC, memoria y WB.
No fue necesario separar la bóveda en otro módulo: contiene solo 16 subllaves,
una contraseña y un indicador.

| API | Contrato |
|---|---|
| `sim_crypto_rol32(x,n)` | Rotación unsigned, cantidad efectiva `n & 31`; cero nunca evalúa shift por 32. |
| `sim_crypto_f(x,k)` | Función de ronda con suma módulo 2³². |
| `sim_crypto_reset(boveda)` | Borra llaves, contraseña y `pwd_set`; NULL es inocuo. No toca ESTADO ni memoria. |
| `sim_crypto_capturar(i,a,b,estado,salida)` | Recibe una instrucción CRIPTO ya decodificada por S2; captura valores y marca de permiso ID. |
| `sim_crypto_preparar(boveda,memoria,id,salida)` | Función combinacional EX, sin efectos; entrega excepción o transacciones y resultados pendientes. |
| `sim_crypto_aplicar(boveda,efecto)` | Publica solo la bóveda de un efecto preparado, válido y sin excepción. No aplica memoria ni WB. |

Las funciones booleanas conservan su salida ante error. Se exige usar
`sim_decode_palabra/bundle` antes de capturar; no se reinterpretan los campos de
la palabra ni se mueve la autorización al decodificador estático. Las estructuras
preparadas no deben inventarse ni alterarse entre preparar y aplicar.

`SimCryptoId` conserva `SimInstruccion`, operandos y falta de permiso.
`SimEfectoCrypto` contiene operación, excepción, acceso B, escritura de llave o
contraseña y resultado WB. `SimCryptoWb` conserva el par y cambios AUTH/INIT.
`SimPipeline` agrega bóveda, ESTADO y `trap_pc/trap_count`; mantiene todas sus
funciones públicas. Los campos nuevos están antes de `memoria`, que permanece
al final: se copia únicamente el prefijo de control y no 64 KiB por ciclo.

## Bóveda y aislamiento

`keys[LK][RK]` representa cuatro llaves de cuatro palabras uint32_t. `ell` escribe
un par 0/1 o 2/3; `fsl/fsli` consultan una palabra durante EX. La contraseña de
32 bits y `pwd_set` son independientes: contraseña cero es válida después de
provisionar, pero la candidata cero antes de `setpwd` nunca autentica.

Cada instancia contiene su propia bóveda y DMEM, sin variables globales compartidas.
La imagen prestada permanece inmutable. Las pruebas comparan la imagen completa
al final y verifican independencia entre instancias y restauración de memoria.

Las estructuras de almacenamiento son visibles al programa anfitrión C para
permitir instancias sin heap interno; esto no es una interfaz de lectura de la
ISA. No existen getters arquitectónicos ni instrucciones nuevas de extracción.
El núcleo no imprime la bóveda. `SimCiclo` no copia su almacenamiento privado;
puede conservar operandos GPR originales y resultados que la ISA permite observar.
Solo el ejecutable de pruebas, invocado explícitamente con `sonda`, y el testbench
de referencia inspeccionan el almacenamiento interno para verificarlo.
No se guardan secretos en GPR o DMEM como efecto de `ell/setpwd`.

## Semántica de instrucciones

| Operación | Efectos autorizados | Permiso capturado ID |
|---|---|---|
| `fsl` | EX calcula `(R, L XOR F(R,K))`; WB escribe `rd,rd+1`. | AUTH=1 |
| `fsli` | EX calcula `(R XOR F(L,K), L)`; WB escribe el par. | AUTH=1 |
| `ell` | Al final de EX escribe las dos subllaves con valores capturados ID. Sin GPR, DMEM ni resultado WB. | AUTH=1 |
| `vcr` | Lee candidata por B en EX; prepara `pwd_set && candidata==password`; publica AUTH en WB. | Siempre |
| `camcon` | Lee B y prepara palabra completa rotada; al final de EX rota contraseña y publica memoria B. Mantiene AUTH. | AUTH=1 |
| `setpwd` | EX escribe contraseña y `pwd_set=1`; WB limpia INIT. Sin GPR ni DMEM. | INIT=1 |

`F(x,K) = (ROL32(x,5)+K) XOR ROL32(x,13)`. Todas las operaciones utilizan
uint32_t, sin overflow signed ni dependencia del endianness del anfitrión.
La inversa reutiliza F; cifrar usa RK=0,1,2,3 y descifrar RK=3,2,1,0.
El vector conocido del enunciado, también usado por `tb_crypto_unit`, es:

- Entrada little-endian `HOLA2026`: L=`414C4F48`, R=`36323032`.
- K0..K3: `0F1E2D3C`, `4B5A6978`, `8796A5B4`, `C3D2E1F0`.
- Cifrado: L=`03D44466`, R=`D685AFAF`.

Además del roundtrip y el RTL, Python implementa una referencia independiente
con rotación de cadenas de bits y valida valores de ronda/cifrado conocidos.
Los programas reales de archivo prueban orden de subllaves y carga/almacenamiento
little-endian de varios bloques.

## Temporización, permisos y ESTADO

ESTADO conceptual tiene AUTH en bit0 e INIT en bit1, con los demás bits a cero.
Iniciar/reset deja ESTADO=2. `vcr` solo modifica AUTH y `setpwd` solo limpia INIT.
Ambas actualizaciones consumen el resultado EX/WB anterior.

Los operandos GPR se leen en ID antes de publicar WB, sin bypass. El par fuente
Feistel se captura completo; EX consulta la subllave actual sin volver al banco.
Un resultado GPR y AUTH/INIT están disponibles para ID del bundle N+3, mientras
N+1/N+2 todavía capturan el valor anterior. Se probaron ALU/LSU→CRIPTO y
CRIPTO→ALU/LSU/BRU/CRIPTO, además de producción ALU simultánea sin forwarding.

La bóveda se publica antes: `ell→fsl/fsli`, `setpwd→vcr` y `camcon→vcr` en bundles
consecutivos observan la escritura EX anterior. Esto no reduce la latencia de
ESTADO. Un `setpwd` que vio INIT=1 en ID puede ejecutarse aunque al llegar a EX
INIT ya haya cambiado; se probaron tres provisionamientos consecutivos.

## Excepciones y control de flujo

Una falta de permiso es una excepción arquitectónica. Viaja por ID/EX y ocurre
en EX; no produce una detención administrativa en ID. Primero se prepara CRIPTO
y se conoce la excepción; solo si no existe se habilitan ALU, LSU y BRU del
mismo bundle. Una falta cancela par, llave, contraseña, AUTH/INIT, ambos puertos
de memoria, resultado ALU, carga LSU y salto/enlace BRU del culpable.

La excepción prevalece sobre un salto tomado: `redirect_pc=0x3FF0`. Se invalidan
IF/ID e ID/EX, sin aceptar ni diagnosticar instrucciones del camino descartado.
EX/WB nuevo queda inválido, pero WB anterior conserva sus escrituras y su
actualización de ESTADO. `trap_pc` recibe el PC culpable y `trap_count` suma uno
como uint32_t con wraparound. La traza identifica operación y ciclo EX.

El manejador es código normal en el bundle físico 1023. Se comprobó su ejecución
y una excepción producida por el propio manejador. No existe retorno implícito.
La búsqueda física usa PC[13:4], por lo que después de 0x3FF0 la búsqueda
secuencial puede volver a IMEM[0] por aliasing sin que el PC se reduzca a 14 bits.

Las prioridades son: rechazo administrativo del intento sin flanco; reset
explícito; excepción EX; salto EX; incremento secuencial. Los errores de archivo,
API, codificación o PC finito no incrementan trap_count. Una validación rechazada
congela también memoria/bóveda preparadas, como en S4/S5.

## Puerto B y atomicidad

Ambas unidades comparten los 65536 bytes de `SimMemoria`. B reutiliza las
transacciones `SimAccesoLsu` de S4 como carga/store de palabra completa; no se
modificó la implementación ni la interfaz del puerto A.

Dirección B absoluta sin signo de 16 bits; palabra física `direccion & 0xfffc`.
Las direcciones no alineadas seleccionan una palabra, sin cruzar a otra. A
conserva byte enables y aliasing de bits altos. Ambas lecturas consultan la
misma memoria anterior: no se reenvía un store A a una candidata de vcr ni a
la rotación camcon del mismo flanco.

El orden de un paso aceptado es:

1. Preparar CRIPTO/decidir excepción; preparar ALU, LSU y BRU permitidos.
2. Resolver redirect y aceptar IF/ID del camino correcto.
3. Capturar ID con banco/ESTADO anteriores y validar el resto del ciclo.
4. Preparar WB anterior, incluidos puertos 3/4 y AUTH/INIT.
5. Publicar transacción A, después B y bóveda siguiente; publicar control.

Si A/B escriben la misma palabra, B gana completamente, incluso sobre guardab.
La rotación B siempre usa el dato anterior, nunca el dato escrito por A.
Las transacciones B son generadas y validadas antes de publicar A; no se copia
DMEM por paso ni se permite una escritura parcial por un rechazo posterior.

## Cinco puertos WB

Los resultados pendientes se aplican en orden ALU(0), LSU(1), BRU(2), L(3), R(4).
El puerto de mayor índice gana y todas las solicitudes habilitadas permanecen
en la traza. x0 no habilita escritura. Los conflictos son observaciones de
calendarización: no crean stalls, errores de CPU ni reordenamientos.

El contrato S2 exige rd par entre x2 y x30: rd y rd+1 nunca coinciden ni son x0.
Por tanto, un conflicto entre los dos puertos CRIPTO o su destino x0 es
inalcanzable en instrucciones legales. Se probaron los límites legales y x0
para los otros tres puertos; los testbenches originales de WB/regfile también
cubren codificaciones físicamente más permisivas.

## Reset

`sim_pipeline_reiniciar` limpia PC, GPR, etapas, resultados pendientes, bóveda,
ESTADO, contadores y trap_pc. Conserva la imagen, modo administrativo y DMEM.
No suma un ciclo de ejecución. `restaurar_memoria` es una recarga explícita de
los datos originales, separada del reset físico.

Antes de limpiar control, reset prepara los accesos EX vigentes. Como memory.sv
no recibe reset, stores A y camcon B autorizados escriben en ese flanco; B gana
conflictos. La bóveda sí recibe reset: se borra en vez de rotarse/escribirse.
Una falta de permiso ya capturada inhibe A/B incluso en el flanco de reset.
Se recorrieron todos los límites de fase de provisionamiento, autenticación,
llaves, Feistel, salto, store, camcon y excepciones pendientes.

## Contabilidad y observabilidad

Se conservan ciclos, admitidos, completados y anulados. Un bundle NOP sigue
siendo válido. Una excepción anula el culpable EX y el IF/ID joven ya admitido;
IF suprimido nunca se admitió y no incrementa anulados. Una operación con
excepción no se retira como completada. Tras cada paso se verifica:

`admitidos = completados + anulados + valid(IF/ID) + valid(ID/EX) + valid(EX/WB)`.

Las observaciones nuevas son ESTADO, trap_pc/trap_count, permisos ID, excepción
EX, solicitudes WB y conflicto A/B. No se agregan métricas de caché ni se
confunden diagnósticos administrativos con excepciones del procesador.
La ocupación ID/EX de la traza no significa ejecución exitosa: hay que consultar
flush y excepción antes de atribuir efectos o retiros.

## Trazas obtenidas de ejecuciones reales

Las siguientes tablas se generaron de los JSON de la suite y coinciden con RTL.
Cada fila describe ocupación antes del flanco y PC/ESTADO después del flanco.
Todos los PC son hexadecimales; `—` indica una etapa inválida. El modo es físico,
por eso sigue buscando NOP fuera del código cargado.

### A. setpwd → vcr → fsl

B0 produce x2=0; B3 ejecuta setpwd, B4 vcr y B7 fsl. Los NOP intermedios
respetan N+3. El ciclo 6 escribe contraseña/pwd_set, el 7 limpia INIT y calcula
AUTH pendiente, el 8 publica AUTH. ID de fsl en ciclo9 ve AUTH=1.

| Ciclo | IF | ID | EX | WB | ESTADO antes→después | PC después | Efecto EX/WB |
|---|---|---|---|---|---|---|---|
| 1 | 0000 | — | — | — | 2→2 | 0010 | — |
| 2 | 0010 | 0000 | — | — | 2→2 | 0020 | — |
| 3 | 0020 | 0010 | 0000 | — | 2→2 | 0030 | — |
| 4 | 0030 | 0020 | 0010 | 0000 | 2→2 | 0040 | — |
| 5 | 0040 | 0030 | 0020 | 0010 | 2→2 | 0050 | — |
| 6 | 0050 | 0040 | 0030 | 0020 | 2→2 | 0060 | contraseña EX; INIT pendiente |
| 7 | 0060 | 0050 | 0040 | 0030 | 2→0 | 0070 | AUTH pendiente=1; publicación ESTADO WB |
| 8 | 0070 | 0060 | 0050 | 0040 | 0→1 | 0080 | publicación ESTADO WB |
| 9 | 0080 | 0070 | 0060 | 0050 | 1→1 | 0090 | — |
| 10 | 0090 | 0080 | 0070 | 0060 | 1→1 | 00A0 | par Feistel pendiente |
| 11 | 00A0 | 0090 | 0080 | 0070 | 1→1 | 00B0 | par escrito WB |

### B. Excepción y ejecución del manejador

B0 produce x10=42; B1 combina ALU, store, sye y fsl sin AUTH. B2/B3 son
jóvenes. El ciclo3 captura falta de permiso; el 4 cancela B1 y conserva WB de
B0. El handler en 3FF0 produce x9=9 y se retira en ciclo8.

| Ciclo | IF | ID | EX | WB | ESTADO antes→después | PC después | trap_count |
|---|---|---|---|---|---|---|---|
| 1 | 0000 | — | — | — | 2→2 | 0010 | 0 |
| 2 | 0010 | 0000 | — | — | 2→2 | 0020 | 0 |
| 3 | 0020 | 0010 | 0000 | — | 2→2 | 0030 | 0 |
| 4 | 0030 | 0020 | 0010 | 0000 | 2→2 | 3FF0 | 1 |
| 5 | 3FF0 | — | — | — | 2→2 | 4000 | 1 |
| 6 | 4000 | 3FF0 | — | — | 2→2 | 4010 | 1 |
| 7 | 4010 | 4000 | 3FF0 | — | 2→2 | 4020 | 1 |
| 8 | 4020 | 4010 | 4000 | 3FF0 | 2→2 | 4030 | 1 |

### C. LSU + camcon

B7 contiene `guardab x0,x0,18` y `camcon 17,7`; B8 contiene vcr0. Las dos
direcciones físicas son 0010. Memoria inicial=80000001; A prepara cero con
BE=0100 y B prepara ROL7=000000C0. En ciclo10 ambas lecturas ven 80000001 y
la palabra posterior es 000000C0: B prevalece sobre A. AUTH se conserva.

| Ciclo | IF | ID | EX | WB | ESTADO antes→después | PC después | Efecto EX/WB |
|---|---|---|---|---|---|---|---|
| 1 | 0000 | — | — | — | 2→2 | 0010 | — |
| 2 | 0010 | 0000 | — | — | 2→2 | 0020 | — |
| 3 | 0020 | 0010 | 0000 | — | 2→2 | 0030 | — |
| 4 | 0030 | 0020 | 0010 | 0000 | 2→2 | 0040 | — |
| 5 | 0040 | 0030 | 0020 | 0010 | 2→2 | 0050 | — |
| 6 | 0050 | 0040 | 0030 | 0020 | 2→2 | 0060 | contraseña EX; INIT pendiente |
| 7 | 0060 | 0050 | 0040 | 0030 | 2→0 | 0070 | AUTH pendiente=1; publicación ESTADO WB |
| 8 | 0070 | 0060 | 0050 | 0040 | 0→1 | 0080 | publicación ESTADO WB |
| 9 | 0080 | 0070 | 0060 | 0050 | 1→1 | 0090 | — |
| 10 | 0090 | 0080 | 0070 | 0060 | 1→1 | 00A0 | A/B: 80000001→000000C0 |
| 11 | 00A0 | 0090 | 0080 | 0070 | 1→1 | 00B0 | AUTH pendiente=1 |
| 12 | 00B0 | 00A0 | 0090 | 0080 | 1→1 | 00C0 | — |

## Pruebas reproducibles y contrato común

Ejecutar desde la raíz de Compiladores. Arquitectura debe conservar el HEAD de
referencia; `ARQUITECTURA_ROOT` permite apuntar a una copia de ese repositorio.
Se requieren C11, Python3, make, flex, bison, Icarus Verilog y vvp.

```sh
export ARQUITECTURA_ROOT=/ruta/Proyecto2_Arquitectura1
python3 tests/vliw/test_sim_cargador.py
python3 tests/vliw/test_sim_decode.py
python3 tests/vliw/test_sim_pipeline.py
python3 tests/vliw/test_sim_lsu.py
python3 tests/vliw/test_sim_bru.py
python3 tests/vliw/test_sim_crypto.py
make -B -k test
```

Para guardar comandos/códigos de salida y trazas detalladas de las tres tablas:

```sh
S6_EVIDENCIA=/tmp/evidencia-s6.json S6_TABLAS=/tmp/tablas-s6 \
  python3 tests/vliw/test_sim_crypto.py
```

Los fixtures usan el ensamblador existente, semillas fijas y el escritor BALB
real. Para datos de fixtures de `balc -x`, que no admite una sección de datos en
su sintaxis ensamblador, se actualizan tamaño/base de datos del contenedor v1;
S1 sigue siendo el único lector. La comparación cubre MEM y BALB, los programas
originales de cifrado/descifrado con bucles, y un handler en el último bundle.

La sonda `tb_sim_crypto_ref` instancia top original. Captura entradas antes del
flanco y efectos después de NBA, incluidos PC/etapas, operandos ID/EX de las
cuatro unidades, permisos, AUTH/INIT, excepción, redirect/flush, ambos puertos,
memoria pre/post, par pendiente, ESTADO pendiente, cinco puertos WB, conflictos,
32 registros, bóveda y DMEM final completa. Los campos inhabilitados se
normalizan a cero; no se afirma equivalencia de valores basura sin validez.

La comparación de funciones llama las funciones de ronda/rotación del RTL y
usa además el modelo Python independiente. Las secuencias de bóveda, permisos,
memoria y reset se verifican en el pipeline completo, sin alterar Arquitectura.
Las sondas de admitidos/anulados siguen el contrato del simulador; no son
contadores nuevos incorporados al RTL.

Para sanitizadores, usar copias de fuentes separadas para evitar que `make balc`
de una suite reemplace el ejecutable de otra. Repetir S1–S6 con:

```sh
CC='cc -g -fno-omit-frame-pointer -fsanitize=undefined' \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  python3 tests/vliw/test_sim_crypto.py
CC='cc -g -fno-omit-frame-pointer -fsanitize=address,undefined' \
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  python3 tests/vliw/test_sim_crypto.py
```

La suite compila con `-std=c11 -Wall -Wextra -Wpedantic -Werror -O2` y registra
los comandos completos C, iverilog, vvp y BALC. Sin Arquitectura o herramientas,
el contraste se marca omitido, nunca PASS.

## Resultados de validación

Línea base y estado final: todos los comandos siguientes devolvieron 0.

| Comando / comprobación | Línea base | Final |
|---|---|---|
| `python3 tests/vliw/test_sim_cargador.py` | 5 PASS | 5 PASS |
| `python3 tests/vliw/test_sim_decode.py` | 13 PASS, 224 vectores RTL | Igual |
| `python3 tests/vliw/test_sim_pipeline.py` | 14 PASS | 14 PASS |
| `python3 tests/vliw/test_sim_lsu.py` | 14 PASS, 1184 vectores, 363 ciclos, 32 top | Igual |
| `python3 tests/vliw/test_sim_bru.py` | 17 PASS, 1262 vectores, 884 ciclos, 55 top | Igual |
| `python3 tests/vliw/test_sim_crypto.py` | No existía | 17 PASS, 256 comprobaciones C, 2104 vectores, 3983 ciclos, 210 top |
| `make -B -k test` en Compiladores | PASS | PASS |
| `make sim test-assembler test-programs archivo` en Arquitectura | PASS | Repetido con `make -B`: PASS |

S1–S6 suman 80 tests Python; no se omitieron contrastes. Cada ejecución top es
un programa/variante parametrizada, no 210 fuentes diferentes. Los 3983 ciclos
son flancos de ejecución; los flancos adicionales de reset no se suman a ciclos.
La comprobación unitarias C devuelve exactamente `256 comprobaciones unitarias PASS`.
No se suman otra vez las mismas pruebas repetidas bajo sanitizadores al total.

Arquitectura ejecutó 24 testbenches: **24 PASS, 0 FAIL, 0 NO-COMPILA,
0 SIN-CHEQUEO**. Incluye crypto_unit, decoder_crypto, key_vault, pipeline_cripto,
memory, regfile, wb, pipeline_integracion y top. Además pasaron 21 pruebas de
assembler, 13 ISA, 4 bundler, 6 bundle_control y 14 cpu_program.
`make archivo` dio 7 verificaciones PASS y `cmp` confirmó los 386 bytes originales.

También se ejecutó:

```sh
make descifrar ARCHIVO=build/archivo/cifrado.bin \
  LLAVE=0f1e2d3c,4b5a6978,8796a5b4,c3d2e1f0
```

Código 0. Produce 392 bytes porque trabaja con bloques completos: se comprobaron
los 386 bytes originales y exactamente seis bytes cero de relleno. Este caso
no se presenta como igualdad de longitud; `make archivo` sí recorta y compara
el archivo original completo.

UBSan y ASan+UBSan (`detect_leaks=0`) repitieron S1–S6: todos sus comandos
terminaron con 0, sin omisiones ni hallazgos. La compilación estricta también
terminó con 0. **LeakSanitizer no verificado**: una ejecución explícita con
`ASAN_OPTIONS=detect_leaks=1` terminó con 1 y el mensaje de error fatal
`LeakSanitizer does not work under ptrace`. No se cuenta como PASS.

El SHA256 de BALC reconstruido antes/después es idéntico:
`3079dfc930c3b77bebb029db0fc41e3a0a14b9e9020ff725cb2e26e800283c8f`.

Durante la preparación de pruebas se corrigieron errores de fixture: un acceso
al miembro inexistente `Bundle.slots` en vez de `instructions`; la ventana del
handler que permitía volver a B0 por aliasing de IMEM; y la base por defecto
0x4000 del contenedor BALB al inyectar datos de prueba que el RTL recibía desde
cero. Se corrigieron fixtures y expectativas, sin alterar ISA, RTL o compilador.
El contraste final no presenta divergencias pendientes.

La evidencia de esta ejecución se conserva fuera del repositorio en
`/tmp/ce1108-s6-8zazttcj/`: logs baseline/final, manifiestos de comandos y códigos,
`evidencia-final.json`, `evidencia-ubsan.json`, `evidencia-asan.json`, tablas JSON,
`verificacion-adicional.json` y diff. Son artefactos temporales de revisión;
los tests y semillas versionables permiten regenerarlos.

## Diferencias deliberadas, decisiones y límites

- S2 impone pares Feistel pares desde x2, fuentes pares de ell y off 0/2.
  El decoder físico no verifica todos esos campos; la suma del segundo índice
  de bóveda en RTL envuelve módulo4. No se amplía S2 para aceptar reservados.
  La equivalencia se delimita a las palabras legales del contrato común.
- Búsqueda finita drena al final del archivo; búsqueda física conserva aliasing,
  NOP no cargados y exige presupuesto. Finalización/límite/diagnósticos de API
  son controles del anfitrión, no instrucciones ni excepciones del hardware.
- No se modelan retardos analógicos ni valores X/Z; se comparan flancos digitales
  con estado conocido. Las lecturas o buses sin habilitación no son efectos.
- El modelo es el procesador original del Proyecto I, sin caches ni stalls.
  No se incorporan avances de otras ramas ni se cambia top/decoders/ISA.
- `SimPipeline` no debe copiarse como un objeto de imagen para compartir estado:
  cada instancia tiene su memoria/bóveda privada y requiere inicialización.
- El calendario sigue siendo responsabilidad del programa: no se corrigen RAW
  ni permisos anticipadamente. Esta semántica deja una base comprobable para
  un calendarizador posterior, que está fuera del alcance de S6.

Los cambios a pruebas S3/S4/S5 sustituyen CRIPTO antes no soportada por IDs
realmente reservados y conservan todas las aserciones de rechazo/atomicidad;
solo agregan el enlace con sim_crypto.c. No se modifican S1, S2, ALU, LSU, BRU,
compilador, parser, lexer, generador, Makefile, CLI, IDE ni Arquitectura.
