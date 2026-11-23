# S4: LSU, DMEM privada e integración IF–ID–EX–WB

S4 agrega `guardap`, `guardab`, `cargai` y `cargabai` al slot 1 del simulador.
ALU sigue ocupando el slot 0; BRU y CRIPTO continúan admitiendo únicamente NOP.
Se conservan las cuatro etapas, las firmas de S3 y su política de ejecución
acotada. No hay forwarding, stalls automáticos, scheduler ni etapa MEM.

## Separación de responsabilidades

- S1 carga MEM/BALB y proporciona `SimImagen` inmutable.
- S2 entrega operación, registros y offset interpretado; S4 no vuelve a extraer
  campos de instrucciones binarias.
- `sim_lsu` calcula señales combinacionales y prepara una transacción. No conoce
  PC, etapas, archivos ni el banco de registros.
- `sim_pipeline` captura operandos, decide el flanco de publicación y retira las
  escrituras. ALU y LSU tienen controles/resultados independientes en el mismo
  bundle, no son operaciones excluyentes.

## DMEM: organización y propiedad

`SimMemoria` contiene 65536 bytes propios. Cada `SimPipeline` incluye una
instancia por valor; no requiere malloc/free ni un destructor nuevo. Dos
simuladores que comparten imagen tienen memorias diferentes. S3 conserva su
referencia prestada a la imagen para IMEM y restauraciones; el llamador debe
mantenerla viva e inmutable. S4 nunca escribe `imagen->dmem`.

`sim_pipeline_iniciar` crea o reemplaza el estado y copia todos los bytes de la
imagen. Los datos BALB quedan en su base original y el resto conserva el cero
que S1 establece. No se inventa inicialización de SP/GP. Reemplazar la instancia
es una nueva carga administrativa, no un flanco de reset.

La memoria está al final de `SimPipeline`. Los pasos copian/publican solo el
prefijo de control anterior a ella, mediante `offsetof`: no se copian 64 KiB
por ciclo. El temporal de tipo `SimPipeline` reserva ese espacio automático,
pero su miembro `memoria` no se inicializa ni se consulta durante el paso. Los
nuevos campos de control deben agregarse antes de la memoria para preservar
este contrato. Al crear/restaurar sí se copian los datos completos.

## Direcciones y little-endian

La dirección efectiva se calcula como:

```text
direccion = base_capturada + (uint32_t)offset_decodificado
palabra_fisica = direccion & 0x0000fffc
carril = direccion & 3
```

El offset de S2 tiene 11 bits con signo (-1024..1023). La suma unsigned reproduce
el patrón de 32 bits del RTL, incluido overflow. El módulo independiente rechaza
un offset fuera de ese contrato; no es una excepción de memoria del procesador.

`memory.sv` selecciona `addr[15:2]` para 16384 palabras. Por tanto, los bits
superiores se descartan: 0x10000 selecciona la misma palabra que 0. Los dos bits
bajos no hacen que una palabra se lea/escriba entre dos palabras físicas. Por
ejemplo, `guardap` en dirección 3 modifica bytes 0..3, no 3..6. Dirección
0xffffffff selecciona la última palabra; 0xffffffff+1 produce dirección 0.
No se agregan comprobaciones estrictas de rango/alineación ni excepciones.

La representación por bytes evita depender del endianness del anfitrión:
0x12345678 se almacena como 78,56,34,12 en direcciones crecientes.
`sim_memoria_leer_palabra` reconstruye esa palabra mediante desplazamientos
unsigned explícitos y admite direcciones altas/no alineadas igual que RTL.

## Cuatro operaciones LSU

| Instrucción | Selección y efecto |
|---|---|
| `guardap base,dato,off` | BE=1111; escribe los cuatro carriles de la palabra física con el dato capturado. |
| `guardab base,dato,off` | BE=1<<carril; wdata=dato<<(8*carril); cambia únicamente el byte seleccionado. |
| `cargai destino,base,off` | Lee combinacionalmente la palabra física completa y prepara su resultado para WB. |
| `cargabai destino,base,off` | Selecciona el carril de la palabra y extiende con signo el byte a 32 bits. |

El orden textual de stores es **base, dato, offset**; para loads es **destino,
base, offset**, según el ensamblador existente. No se intercambian estos campos.

En `guardab`, wdata conserva el desplazamiento del fuente completo que hace el
RTL. BE impide escribir sus demás carriles: solamente los ocho bits bajos del
fuente terminan almacenados. Los restantes bytes de DMEM permanecen intactos.

La extensión de `cargabai` usa una máscara explícita si el octeto tiene bit 7:
00→00000000, 7F→0000007F, 80→FFFFFF80 y FF→FFFFFFFF. No depende de convertir
un entero unsigned fuera de rango a un tipo signed del host.

## API LSU y transacciones

```c
sim_memoria_leer_palabra(&sim.memoria, direccion, &palabra);
sim_lsu_preparar(&sim.memoria, operacion, base, dato, offset, &acceso);
sim_lsu_aplicar(&sim.memoria, &acceso);
```

`SimAccesoLsu` conserva validez, tipo carga/store, dirección efectiva, palabra
física, BE, wdata, palabra leída y resultado de carga. Estas magnitudes se
mantienen separadas para observar las mismas señales del RTL.

Preparar no modifica memoria. Aplicar verifica punteros, validez, alineación y
límites del índice físico y rango de BE antes de tocar bytes. Esas validaciones
protegen una transacción C mal construida; no rechazan las direcciones efectivas
altas o no alineadas que el hardware admite.

Durante un paso del pipeline la transacción se aplica después de validar IF,
los cuatro slots de ID, EX y los destinos WB. Solo entonces se publican memoria
y el prefijo de control. Si hay rechazo administrativo, nada del flanco cambia,
salvo estado/diagnóstico: tampoco un store preparado ni un WB anterior pendiente.
Esto conserva la política transaccional de S3; no representa un trap de hardware.

## Etapas y resultados independientes

**ID:** S2 revisa todo el bundle. Se capturan base, dato de store, offset,
operación y destino LSU en `SimIdEx.lsu`. La ALU conserva sus operandos anteriores.
Ambas unidades leen el banco anterior al flanco, incluso si WB escribirá en él.

Para cargas, S2 marca rs2 ausente. El decoder físico también lee su campo rd
por el segundo puerto, aunque el dato no se usa para cargar. S4 conserva esa
lectura inutilizada para comparar exactamente el bus wdata observado en RTL;
no la convierte en fuente semántica de la carga.

**EX:** ALU calcula sin leer de nuevo el banco. LSU calcula dirección y lee la
memoria anterior al flanco. Un store publica DMEM en ese mismo flanco; un load
captura su resultado en `SimExWb.lsu`. Este campo representa el registro interno
de LSU del RTL, paralelo a EX/WB ALU; no añade otra etapa ni otro ciclo.

**WB:** ALU y un load del mismo bundle pueden escribir en dos puertos. Store no
escribe registros. NOP ALU, NOP LSU, bundle válido y burbuja son estados distintos.
Cada bundle se completa una sola vez en WB, tenga cero, una o dos operaciones.
El vaciado sigue requiriendo N+3 ciclos para N bundles secuenciales.

`SimCiclo` conserva las observaciones ALU de S3 y agrega `id_lsu`, `ex_lsu`,
`wb_lsu`, escritura/descartes LSU y conflicto. Son señales preflanco; el banco,
las etapas y DMEM de `SimPipeline` ya representan el estado postflanco.

## Conflictos WB y dependencias

Puerto 0 es ALU y puerto 1 es LSU. S4 aplica ambos en orden ascendente, como
`regfile.sv`: si apuntan al mismo destino no cero, **gana LSU** y se observa
`conflicto_wb`. No se detienen, eliminan ni reordenan instrucciones. Escrituras
a x0 se descartan antes de identificar conflictos. `ultima_escritura` conserva
la última escritura efectiva de ese orden; los dos puertos están en la traza.

Load→ALU, ALU→base/dato LSU y load→store respetan N+3. Los consumidores N+1 y
N+2 capturan el registro anterior; N+3 ya captura el actualizado. EX nunca
relee registros. ALU y LSU dentro del mismo bundle tampoco se comunican sus
resultados nuevos: comparten únicamente el banco que existía en ID.

Store→load tiene otra temporización: store escribe DMEM en EX; el load del
bundle siguiente lee en su EX del ciclo siguiente, cuando el store ya existe.
No necesita esperar N+3 para comunicarse por memoria.

## Ejemplo por ciclos contrastado

Inicial: DMEM[0..3]=78,56,34,12; registros cero.
B0: `nop | guardap x0,x0,0 | nop | nop`.
B1: `nop | cargai x2,x0,0 | nop | nop`.
PC B0=0 y PC B1=16. La tabla muestra etapas/señales anteriores al flanco y
efectos que quedan publicados después.

| Ciclo | IF | ID | EX | WB | DMEM[0] / banco |
|---|---|---|---|---|---|
| 1 | B0 | — | — | — | Palabra 12345678; sin escritura. |
| 2 | B1 | B0 | — | — | ID captura base=0 y dato=0 para store. |
| 3 | — | B1 | B0 | — | EX presenta addr=0, BE=1111, wdata=0; lee 12345678 antes; DMEM queda 0 después. |
| 4 | — | — | B1 | B0 | Load EX lee 0 y lo deja pendiente para WB; store no escribe registros. |
| 5 | — | — | — | B1 | WB escribe x2=0; pipeline vacío. |

La prueba guarda esta traza mediante `S4_TABLA`. Otro caso comprueba store
palabra seguido inmediatamente por load byte con signo (-128) y el mismo
orden de EX/WB. No se compara únicamente el resultado final.

## Inicialización, reset y restauración explícita

| Acción | Banco/pipeline | DMEM |
|---|---|---|
| `sim_pipeline_iniciar` | Limpia y carga PC inicial. | Copia datos originales S1. |
| `sim_pipeline_reiniciar` | Aplica reset, descarta loads pendientes, ciclos=0. | Conserva datos; puede publicar el store que ya estaba activo en EX. |
| `sim_pipeline_restaurar_memoria` | No cambia ciclos, estado ni etapas. | Recopia los bytes originales de la imagen. |

`memory.sv` no tiene reset; `lsu.mem_we` depende de valid/op, no de reset. Si
reset llega cuando un store ya está en EX, ese store escribe en el flanco de
reset antes de que ID/EX se invalide por NBA. La API reproduce ese efecto;
reset antes de que el store llegue a EX no escribe. Loads pendientes se descartan
y todos los registros se limpian; una carga retirada antes de reset no borra DMEM.

Reiniciar representa ese flanco de reset, pero no lo suma a los ciclos de
programa. Para repetir con los datos originales: primero reiniciar y después
restaurar memoria explícitamente. Hacerlo al revés permitiría que el store de
EX modificara la memoria recién restaurada. Para crear otra ejecución nueva
independiente también se puede llamar nuevamente a iniciar.

Una estructura puesta en cero sin inicialización se rechaza con ARGUMENTO,
incluso con presupuesto cero. Esto no permite pasar memoria automática sin
inicializar: C no puede validar con seguridad un objeto indeterminado. El
contrato sigue exigiendo iniciar antes de avanzar/reiniciar.

## Comparación RTL y datos BALB

`tests/vliw/rtl/tb_sim_lsu_ref.sv` instancia LSU+memory y también top originales.
No modifica Arquitectura. Los vectores observan dirección, we, BE, wdata,
rdata, resultados/destinos/habilitación internos LSU y palabra postflanco.
Las trazas top observan PC/etapas, ID de ambas unidades, EX, stores, loads
pendientes, ambos puertos WB, conflictos y los 32 registros en cada ciclo.
Se compara además toda DMEM final, incluidas regiones que debían conservarse.

El RTL recibe una precarga explícita de las mismas 16384 palabras iniciales
que el modelo C. BALB no llega automáticamente al top. Se comprueba un fixture
BALB v1 y una constante -128 producida realmente por BALC: el test conserva ese
payload y sustituye solo el código del contenedor de prueba por ALU/LSU, porque
el arranque completo requiere saltos de S5. No se afirma que S4 ejecute todavía
programas BAL generales con llamadas, saltos o bucles.

Las diferencias de validez durante el drenado se aceptan únicamente fuera de
la extensión cargada, igual que S3: el top sigue buscando, el simulador termina
la prueba finita. Datos no usados de puertos WB se comparan solo cuando son
válidos; direcciones y datos de cada acceso LSU sí se contrastan íntegramente.

## Pruebas y comandos reproducibles

Ejecutar desde Compiladores, con el repositorio RTL e Icarus disponibles:

```sh
export ARQUITECTURA_ROOT=/ruta/Proyecto2_Arquitectura1
python3 -m unittest discover -s tests/vliw -p test_sim_cargador.py -v
python3 -m unittest discover -s tests/vliw -p test_sim_decode.py -v
python3 -m unittest discover -s tests/vliw -p test_sim_pipeline.py -v
S4_EVIDENCIA=/tmp/evidencia-s4.json S4_TABLA=/tmp/tabla-s4.json \
  python3 -m unittest discover -s tests/vliw -p test_sim_lsu.py -v
make -B -k test
```

En esta validación se usaron copias temporales de ambos repositorios para no
alterar binarios/artefactos versionados. Los tests nuevos compilan C con C11 y `-Wall -Wextra -Wpedantic
-Werror -O2`. En S4, las compilaciones manuales del pipeline descritas para S3
deben enlazar además `src/Simulador_VLIW/sim_lsu.c`; la suite S3 ya incorpora esa dependencia.
No se modifica Makefile ni CLI.

Prueba C y sanitizadores:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude \
  tests/vliw/test_sim_lsu.c src/Simulador_VLIW/sim_pipeline.c src/Simulador_VLIW/sim_lsu.c src/Simulador_VLIW/sim_alu.c \
  src/Simulador_VLIW/sim_decode.c src/Simulador_VLIW/sim_cargador.c -o /tmp/test_sim_lsu
/tmp/test_sim_lsu
CC='cc -fsanitize=undefined -fno-sanitize-recover=all -g' \
  python3 -m unittest discover -s tests/vliw -p test_sim_lsu.py -v
```

La suite S4 incluye 233 comprobaciones C y 14 pruebas Python. Contrasta 1184
vectores, 363 ciclos y 32 ejecuciones de top: bytes en cuatro posiciones,
00/7F/80/FF, offsets extremos, aliasing, no alineados, overflow, simultaneidad,
x0, conflictos, dependencias, memoria independiente, errores transaccionales,
reset y BALB. Incluye 128 bundles aleatorios con DMEM inicial completa aleatoria.
Si falta RTL/Icarus se informa omisión; no cuenta como equivalencia verificada.

## Resultados de esta entrega y límites

Base Compiladores: `b4b4675dd32edfa2a180f0bcd1edb16426e8ba39` en
`feat/simulador-vliw`. RTL: `418c92bb73d3f34fb02f2feca384a83b86a589d9`.
Antes/después: S1 5 OK, S2 13 OK, S3 14 OK, regresión general código 0.
Después: S4 14 OK sin omitidas, UBSan suite completa código 0. Las 245
comprobaciones C de S3 se conservan. Sus dos rechazos LSU se reemplazaron por
BRU ya que LSU ahora es válida; no se eliminaron aserciones.

En la copia de Arquitectura, antes/después se ejecutó con código 0:

```sh
make -B tb_lsu tb_memory tb_decoder_lsu tb_alu tb_regfile \
  tb_regfile_pipeline tb_pipeline_if_id tb_pipeline_id_ex \
  tb_pipeline_ex_wb tb_pipeline_integracion tb_top
```

PASS respectivos: 22,510,62,11916,17822,10,921,9793,912,55,18 (42041).
BALC reconstruido conserva SHA-256
`3079dfc930c3b77bebb029db0fc41e3a0a14b9e9020ff725cb2e26e800283c8f`.
AddressSanitizer+UBSan pasó las 233 comprobaciones C con `detect_leaks=0`.
LeakSanitizer fue NO-EJECUTABLE: reportó incompatibilidad con ptrace; no se afirma
que se hayan verificado fugas con él. El estado nuevo de memoria no reserva heap.

S5 deberá incorporar BRU, redirects y flush. S6 incorporará CRIPTO, bóveda,
excepciones y puerto B funcional. La separación entre preparar/aplicar permite
arbitrar futuras transacciones antes de publicar; aquí no se ejecuta puerto B.
No hay cachés, IDE, optimizaciones ni cambios al compilador original, S1/S2 o RTL.
