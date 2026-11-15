# Backend binario (Fase 6) y opciones de balc

`src/Codificacion_Binaria/codificacion_binaria.c` convierte el ensamblador VLIW en texto a palabras
de 32 bits por slot y escribe los archivos que carga el procesador. Sigue el
contrato de Arquitectura (`docs/arquitectura/isa.md` y `assembler.md` en la
rama `docs/contrato-arquitectura`) y se comporta igual que `tools/assembler.py`.

## Uso

```bash
make
./balc programa.bal               # programa.bin y programa.mem
./balc -o salida.bin programa.bal # salida.bin y salida.mem
./balc -s programa.bal            # además programa.asm (bundles en texto)
./balc -p programa.bal            # además el listado de paquetes
./balc -x programa.asm            # ensamblador -> binario
./balc -v -s -t -m -p programa.bal
```

Con un `.bal`, el flujo es: léxico → sintáctico → semántico y memoria →
generación → enlace de funciones y arranque → bundles con saltos resueltos →
codificación. Con `-x` la entrada es un archivo de ensamblador con la misma
sintaxis que el ensamblador de Arquitectura (etiquetas, alias ABI,
pseudoinstrucciones, cripto) y se codifica directamente; `-x` no se combina
con `-s`, `-t` ni `-m`. Un `.asm` sin `-x` se rechaza.

`-v` muestra cada fase y las decisiones: tokens, mapa de memoria (gp, sp,
marcos), dirección de cada función, saltos reescritos por alcance, vueltas de
enlace y bundles. `-t` y `-m` imprimen y siguen compilando; `-m` se muestra
aunque la generación falle y, si termina bien, incluye la dirección de código
de cada función.

Los archivos se crean solo si todo terminó bien. Errores: 1 si el programa no
se puede compilar o el ensamblador no es válido; 2 por opciones o archivos.
`balc --frontend [-v] [-t]` conserva el modo de pruebas del lexer y el parser,
que no genera archivos.

## Formato de salida

| Archivo | Contenido |
|---|---|
| `.asm` | Un bundle por línea: `ALU \| LSU \| BRU \| CRIPTO` o `nop`. Etiquetas como comentarios. |
| `.bin` | Contenedor BALB v1: encabezado de 48 bytes, codigo y datos globales. |
| `.mem` | 32 dígitos hexadecimales por bundle, CRIPTO en los bits altos, para `$readmemh` de la IMEM. |
| `-p` | Por paquete: PC y ensamblador; la palabra en hexadecimal y el binario de cada slot separado por campos (tipo, id, operandos). |

El formato completo se describe en [formato_binario.md](formato_binario.md).
La seccion de codigo conserva 16 bytes little-endian por bundle, ALU primero.
El `.mem` y el arranque conservan la inicializacion por instrucciones para
que el procesador siga funcionando sin precarga de datos.

Mapa de memoria: código desde 0 (máximo 1024 bundles), globales desde
gp = 0x4000 hasta 0x7FFF, pila desde sp = 0x10000 hacia abajo.

## Codificación

| Tipo | Bits [31:25] | Campos | IDs |
|---|---|---|---|
| Registro | `1101010` | rg [20:16], rf1 [15:11], rf2 [10:6] | suma 1000, resta 1001, cader 1010, cizq 1011, and 1100, or 1101, xor 1110, cder 1111, mrq 0101, myq 0110 |
| Inmediato | `1000000` | rf1 [20:16], rg [15:11], inm [10:0] | sumai 0, restai 1, cizqi 2, cderi 3, caderi 4, xori 5, andi 6, ori 7 |
| Memoria | `1001001` | base [20:16], dato/destino [15:11], offset [10:0] | guardap 0, guardab 1, cargai 8, cargabai 9 |
| Control | `1000001` | rf1 [20:16], rf2 [15:11], offset [10:0] | igualsi 1, igualno 2, menora 4, mayoroigual 8 |
| Salto | `1001011` | `sye`: rg [20:16], offset [15:0]; `syer`: rg, rf1 [15:11], offset [10:0] | sye 0, syer 1 |
| Cripto | `0000010` | según instrucción | fsl 0, fsli 1, ell 2, vcr 3, camcon 4, setpwd 5 |

`nop` es 0x00000000. `mov rd, rs` = `suma rd, rs, x0`; `not rd, rs` =
`xori rd, rs, -1`; `ret` = `syer x0, ra, 0`.

## Validaciones

- Cada instrucción en su slot; registros x0–x31 o alias; rangos de cada campo
  (inmediatos −1024..1023, corrimientos 0..2047, offsets de salto de 11 y 16
  bits alineados a 16, campos cripto).
- Destino de cada salto dentro del programa.
- Conflictos WAW y RAW entre slots del mismo bundle.
- Latencia: un valor escrito en el bundle N se lee desde N+3, por ambas rutas
  de los saltos condicionales y por la ruta de `sye`.
- Programa no vacío y de 1024 bundles como máximo.

## Verificación contra tools/assembler.py

`tests/fixtures/isa_todas.asm` cubre las 34 instrucciones, pseudoinstrucciones,
alias, etiquetas y operandos nombrados. Su `.mem` y su `.bin` se generaron con
`tools/assembler.py` (commit 818e1de de Arquitectura).
`make test-codificacion-binaria` exige los mismos bytes y los mismos rechazos.

Además, la seccion de codigo del `.bin` y el `.mem` que genera `balc` resultaron idénticos a los de
`assembler.py` sobre la salida de `balc -s`:

| Programa | Bundles | Resultado |
|---|---|---|
| `factorial.bal` (Ejercicio 1) | 189 | idéntico |
| `ejercicio2_busqueda_matriz.bal` (Ejercicio 2) | 291 | idéntico |
| `ejercicio3_puntajes.bal` (Ejercicio 3) | 801 | idéntico |
| `whale.bal` | 144 | idéntico |
| `cycle.bal` | 270 | idéntico |

Para repetir la comparación (requiere el repositorio de Arquitectura):

```bash
./balc -s examples/factorial.bal   # factorial.asm, factorial.bin y factorial.mem
python3 ../Proyecto1_Arquitectura1/tools/assembler.py \
  --input examples/factorial.asm --output /tmp/ref.mem --binary /tmp/ref.bin
python3 - <<'PYCODE'
import pathlib, struct
b = pathlib.Path("examples/factorial.bin").read_bytes()
offset, cantidad = struct.unpack_from("<II", b, 20)
assert b[offset:offset + cantidad] == pathlib.Path("/tmp/ref.bin").read_bytes()
PYCODE
cmp examples/factorial.mem /tmp/ref.mem
```

## Pendiente

- Ejecutar los `.mem` en el RTL (`tools/run_program.py`, requiere Icarus Verilog).
