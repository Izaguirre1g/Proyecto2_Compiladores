# S2: decodificación de instrucciones VLIW

S2 convierte palabras de 32 bits en `SimInstruccion`. No abre archivos, lee
registros, accede a DMEM, ejecuta instrucciones ni calcula ciclos. S1 conserva
su responsabilidad de carga; `sim_decode_bundle(imagen->imem[b], salida)` recibe
directamente sus cuatro palabras, sin cambiar `SimImagen`.

## Fuentes y diferencias comprobadas antes de implementar

La referencia es el procesador de Arquitectura, commit
`418c92bb73d3f34fb02f2feca384a83b86a589d9`: `rtl/isa_defs.sv`, los cuatro
`decoder_*.sv`, `top.sv`, `alu.sv`, `bru.sv`, `lsu.sv` y `crypto_unit.sv`.
En Compiladores se contrastaron `codificacion_binaria.c`, su encabezado y el
fixture `tests/fixtures/isa_todas.*`. Este módulo no modifica esas fuentes.

Hay diferencias entre reconocimiento ISA, restricciones del ensamblador y
señales físicas. Se documentan para no confundirlas con ejecución del simulador:

| Caso | RTL / codificador existentes | Representación S2 |
|---|---|---|
| ID ALU reservado | `decoder_alu` conserva ADD, incluso con registros extraídos | RESERVADA; no se ejecuta ni se reconoce como suma |
| ID COND reservado | `is_branch=1`, pero `bru` no toma una condición desconocida | RESERVADA, no una instrucción condicional ISA |
| IDs LSU/CRIPTO/JUMP reservados | Señales de actividad o escritura deshabilitadas | RESERVADA, no NOP canónico |
| Operación en otra unidad | Puede quedar físicamente inactiva | SLOT, distinto de NOP |
| Corrimiento inmediato | El ensamblador admite 0..2047; `top` extiende signo de 11 bits y ALU usa solo cinco bits bajos | Se conservan bruto, extendido y cantidad efectiva |
| Saltos directos no alineados | RTL extrae el offset; el ensamblador exige múltiplos de 16 | CAMPOS, distinto de opcode reservado |
| Pares Feistel y campos de `ell` fuera del contrato | El ensamblador los rechaza; el decoder RTL no aplica todas esas restricciones | CAMPOS; sin política de ejecución para la codificación inválida |
| Bits libres de formatos reconocidos | El hardware los ignora, como comprueba su TB cripto | Se ignoran; no se exige cero |

Los testbenches BRU incluyen offsets -4 y +5 para comprobar la extracción;
eso no cambia la alineación que exige el ensamblador para programas válidos.
Las diferencias anteriores se contrastan explícitamente en las pruebas de S2.
Una decisión futura sobre ejecutar binarios fuera del contrato deberá tratarse
aparte: S2 no emula fallback, genera excepciones ni normaliza esos casos a NOP.

## API y datos que recibirá S3

`sim_decode_palabra(palabra, slot, &salida)` reemplaza la salida y devuelve su
estado. El slot debe ser 0 ALU, 1 LSU, 2 BRU o 3 CRIPTO. `sim_decode_bundle`
completa las cuatro salidas aunque haya un error; devuelve el primer error por
orden de slot, NOP si todo el bundle es NOP, u OK en otro bundle válido.

| Estado | Significado |
|---|---|
| `SIM_DECODE_OK` | Operación ISA reconocida, en su slot y con campos válidos |
| `SIM_DECODE_NOP` | Palabra completa cero, admitida en los cuatro slots |
| `SIM_DECODE_RESERVADA` | Tipo/ID no definido, incluido tipo NOP con payload no cero |
| `SIM_DECODE_SLOT` | Tipo/ID reconocido, ubicado en otra unidad |
| `SIM_DECODE_CAMPOS` | Operandos que infringen restricciones estáticas del contrato |
| `SIM_DECODE_ARGUMENTO` | Salida NULL o slot fuera de rango; para bundle, un puntero NULL |

Si coinciden problemas se verifica primero el argumento, luego NOP/tipo/ID,
después slot y finalmente operandos. Un error conserva palabra, slot, tipo e ID
para diagnóstico; deja operación inválida, familia/formato vacíos y operandos
ausentes. Un puntero de salida NULL no puede recibir esa información.

Las entradas y salidas de bundle no deben solaparse. No se reservan recursos ni
se mantiene estado global mutable. `sim_operacion_nombre` devuelve una cadena
estática que no debe liberarse.

Los campos de `SimInstruccion` tienen estas convenciones:

- `rd`, `rd2`, `rs1`, `rs2`: índices 0..31. El valor 32 indica ausencia, sin
  confundirla con x0. Feistel expone ambos destinos y ambas fuentes del par.
- `inmediato_bruto`: bits codificados; `inmediato`: valor extendido como lo
  utiliza RTL. `ancho_inmediato=0` indica ausencia y deja ambos valores en cero.
- `inmediato_con_signo`: true para los inmediatos de 11/16 bits; false para la
  rotación de cinco bits de `camcon` y cuando no hay inmediato.
- `corrimiento`: solo para `cizqi/cderi/caderi`, con valor efectivo 0..31.
  Por ejemplo bruto 2047, extendido -1 y corrimiento 31 describen la misma
  instrucción. Los corrimientos por registro todavía requieren su valor en EX.
- `salto`: distingue condicional relativo a PC, incondicional relativo a PC e
  indirecto relativo a registro. No contiene una decisión ni destino calculado.
- `lk`, `rk`, `off_boveda`: campos de cripto. Los campos pequeños ausentes,
  incluido `corrimiento`, valen 255. `ell` usa offset, no un RK inventado.
- `direccion`/`tiene_direccion`: dirección absoluta unsigned de `vcr/camcon`.
  La bandera distingue dirección cero de ausencia.
- `requiere_auth`/`requiere_init`: requisitos estáticos del opcode, sin consultar
  ESTADO ni producir una excepción.

S3 deberá comprobar el estado de cada slot antes de consumir operandos. Esta
representación no certifica ausencia de riesgos RAW/WAR/WAW, disponibilidad N+3,
validez dinámica de direcciones ni destino dentro del programa. Un bundle con
cuatro palabras válidas puede requerir validaciones adicionales posteriores.

## Formatos interpretados

Todas las familias usan tipo `[31:25]` e ID `[24:21]`. El catálogo concreto de
las 34 operaciones está en la tabla de `src/Simulador_VLIW/sim_decode.c`; la semántica completa
permanece en la documentación ISA de Arquitectura.

| Formato | Campos de operandos |
|---|---|
| REG | rd `[20:16]`, rs1 `[15:11]`, rs2 `[10:6]` |
| IMM | rs1 `[20:16]`, rd `[15:11]`, inmediato signed `[10:0]` |
| LSU | base `[20:16]`, destino load o dato store `[15:11]`, offset signed `[10:0]` |
| COND | rs1 `[20:16]`, rs2 `[15:11]`, offset signed `[10:0]` |
| SYE | rd `[20:16]`, offset signed `[15:0]` |
| SYER | rd `[20:16]`, base `[15:11]`, offset signed `[10:0]` |
| FSL/FSLI | LK `[20:19]`, RK `[18:17]`, rd `[16:12]`, inicio fuente `[11:7]` |
| ELL | LK `[20:19]`, off `[18:17]`, rs1 `[16:12]`, rs2 `[11:7]` |
| VCR/CAMCON | dirección unsigned `[20:5]`; CAMCON agrega rotación unsigned `[4:0]` |
| SETPWD | rs1 `[4:0]` |

La extensión signed de 11 bits cubre -1024..1023 y la de 16 bits,
-32768..32767. Los saltos directos válidos usan múltiplos de 16: máximo positivo
1008 o 32752, respectivamente. `syer` admite cualquier offset signed de 11 bits;
el enmascaramiento de su destino pertenece a ejecución. Las direcciones cripto
son bytes 0..65535 sin imponer alineación que el codificador no exige.

Feistel requiere inicios de par pares entre x2 y x30; el segundo índice se
deriva como inicio+1. `ell` admite fuentes pares desde x0 y off 0 o 2.

## Pruebas reproducibles

No se agrega un target ni se modifica el Makefile o la CLI. Desde la raíz:

```bash
python3 -m unittest discover -s tests/vliw -p test_sim_decode.py -v
python3 -m unittest discover -s tests/vliw -p test_sim_cargador.py -v
make -B -k test
```

La suite nueva compila el módulo aislado y la sonda C con C11, advertencias
como errores y sin dependencias externas nuevas. La sonda solo enlaza S1 para
los casos de imágenes; sus modos no son opciones del compilador.

Para contrastar con Arquitectura, se busca por defecto el repositorio hermano
`Proyecto2_Arquitectura1`. También puede indicarse su ruta:

```bash
ARQUITECTURA_ROOT=/ruta/Proyecto2_Arquitectura1 \
  python3 -m unittest discover -s tests/vliw -p test_sim_decode.py -v
```

El contraste necesita Icarus (`iverilog` y `vvp`), ya usado en Arquitectura.
Si faltan RTL o herramientas, esos métodos informan SKIP con el motivo;
un SKIP no equivale a compatibilidad comprobada. Las pruebas compilan los
decodificadores originales con `tests/vliw/rtl/tb_sim_decode_ref.sv` en un temporal,
sin escribir en el repositorio de Arquitectura. También regeneran el fixture
con su `assembler.py` y comparan MEM/binario crudo sin sustituir referencias.

La cobertura incluye campos completos de las 34 operaciones, 8192 combinaciones
tipo/ID/slot, NOP, extremos de registros e inmediatos, errores de operandos,
bits libres, bundles mixtos y conexión S1→S2 por MEM/BALB. El fixture de 43
bundles produce 172 instrucciones decodificadas por formato de archivo.
El contraste RTL incluye AUTH/INIT en sus cuatro combinaciones para verificar
metadatos de privilegio, sin ejecutar la unidad criptográfica en S2.

No se comprueban resultados de ejecución, ciclos, accesos, excepciones dinámicas
ni calendarización. Las pseudoinstrucciones se observan como la operación ya
expandida por el ensamblador; no se pretende recuperar el texto original.
