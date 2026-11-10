# S1: carga de programas para el simulador VLIW

El cargador prepara IMEM y DMEM a partir de los archivos del compilador del
Proyecto I. No decodifica operaciones, ejecuta instrucciones, calcula ciclos
ni modifica el compilador. El usuario de la API elige explícitamente el
formato; la extensión del nombre no determina cómo se interpreta el archivo.

## Contratos reutilizados

La referencia de `.mem` es `cb_escribir_mem`, en `src/codificacion_binaria.c`.
BALB v1 se interpreta según `fb_escribir`, en `src/formato_binario.c`, y
`docs/formato_binario.md`. No se agrega un formato ni se modifica el existente.

La organización de Arquitectura (`rtl/isa_defs.sv`) es de cuatro slots de
32 bits por bundle, 1024 bundles en IMEM y 65536 bytes en DMEM. No existe una
cabecera C común con esos límites. Se publican como `SIM_CARGADOR_*` sin
refactorizar componentes de otros integrantes. Los límites privados del lector
BALB (encabezado de 48 bytes y hasta 16384 bytes de datos) reproducen el contrato
del escritor: incluir `formato_binario.h` introduciría dependencias de AST y
símbolos innecesarias para este módulo.

`CbBundle` incluye texto del ensamblador y números de línea; no representa una
imagen de ejecución. Por eso `SimImagen` conserva únicamente palabras de
instrucción, bytes de datos y metadatos, sin duplicar lógica de codificación.

## API y propiedad de los recursos

`include/sim_cargador.h` define:

| Elemento | Responsabilidad |
|---|---|
| `SimImagen.imem[bundle][slot]` | Palabras numéricas: ALU=0, LSU=1, BRU=2, CRIPTO=3 |
| `SimImagen.dmem[direccion]` | Imagen de bytes de la memoria de datos |
| `cantidad_bundles` | Extensión de código cargada, no bundles ejecutados |
| `cantidad_datos`, `base_datos` | Extensión y dirección de carga de los datos |
| `entrada`, `flags` | Metadatos de BALB; cero para `.mem` |
| `sim_cargar_mem` | Lee la imagen hexadecimal de instrucciones |
| `sim_cargar_balb` | Lee y valida el contenedor BALB v1 |
| `sim_liberar_imagen` | Libera la reserva y deja el puntero en NULL |

Ambas funciones reciben una ruta, un `SimImagen **` cuyo valor inicial debe ser
NULL y un `FILE *` opcional para diagnósticos. Devuelven `SIM_CARGA_OK` (0),
`SIM_CARGA_ARGUMENTO`, `SIM_CARGA_IO`, `SIM_CARGA_FORMATO` o `SIM_CARGA_MEMORIA`.

El cargador es propietario del archivo que abre y lo cierra en todos los
caminos. El flujo de diagnósticos pertenece al llamador. No mantiene estado
global ni reservas entre llamadas. Solo entrega la imagen cuando todas las
validaciones y el cierre han terminado correctamente; ante error libera su
reserva y conserva el puntero recibido. Una imagen viva debe liberarse antes
de reutilizar ese puntero para otra carga.

```c
SimImagen *imagen = NULL;
SimCargaEstado estado = sim_cargar_balb("programa.bin", &imagen, stderr);
if (estado == SIM_CARGA_OK) {
    /* La imagen está disponible; S1 no ejecuta sus instrucciones. */
    sim_liberar_imagen(&imagen);
}
```

Ambas memorias se inicializan en cero antes de copiar el contenido. Los ceros
restantes de IMEM no aumentan `cantidad_bundles`. Esta inicialización es una
decisión de la imagen del simulador, no una afirmación sobre memoria física
sin inicializar. El módulo no escribe en el RTL.

## Lectura de `.mem`

Cada línea no vacía debe contener exactamente 32 dígitos hexadecimales
contiguos. Se admiten mayúsculas, minúsculas, márgenes de espacios/tabulaciones,
terminación LF o CRLF y última línea sin salto. Las líneas vacías se ignoran.
No se admiten comentarios, prefijos `0x`, directivas `@`, valores X/Z ni
espacios entre los dígitos: no forman parte de la salida actual del escritor.

El orden textual es CRIPTO, BRU, LSU y ALU. Por ejemplo:

```text
abcdef0189abcdef0123456780000001
```

produce slots `[0x80000001, 0x01234567, 0x89abcdef, 0xabcdef01]`.
Un bundle de 32 ceros se conserva como bundle cargado. Un archivo vacío o solo
de líneas en blanco se rechaza. Se admiten hasta 1024 bundles; las reservas de
vector de excepción y vaciado no pertenecen al cargador.

`.mem` no aporta datos: DMEM, entrada, base de datos y flags quedan en cero.

## Lectura de BALB v1

Se exige firma `BALB`, versión 1, encabezado de 48 bytes, entrada 0 y dirección
de carga de código 0. Se aceptan exclusivamente la disposición contigua y los
campos que escribe la versión actual:

- Código desde el offset 48, no vacío, múltiplo de 16 y hasta 16384 bytes.
- Datos inmediatamente después del código, hasta 16384 bytes.
- Tamaño declarado igual a encabezado + código + datos, sin bytes extra.
- Dirección y longitud de datos dentro de los 65536 bytes de DMEM.
- Flags 0 o 1; el bit 0 indica que el arranque también inicializa los datos.

Los campos y cada palabra del código se leen explícitamente en little-endian,
sin convertir el encabezado a una estructura C ni depender del host. El primer
grupo de cuatro bytes del código es ALU. Los cálculos de tamaños utilizan
sumas de 64 bits para rechazar desbordamientos de los campos de 32 bits.

Los offsets del archivo no son direcciones del procesador. Los datos se copian
byte a byte en su dirección de DMEM, sin exigir alineación de palabra que
`fb_escribir` no exige. Una sección vacía puede tener base 65536; una sección
de un byte puede comenzar en 65535. El flag se conserva como metadato: S1 no
ejecuta el arranque ni aplica una segunda inicialización.

## Pruebas y ejecución

No se modifica el Makefile compartido. Ejecutar desde la raíz del repositorio:

```bash
python3 -m unittest discover -s tests/vliw -p test_sim_cargador.py -v
```

La suite compila el lector y `tests/vliw/test_sim_cargador.c` por separado del resto
del compilador, con C11 y advertencias como errores. Usa el Makefile existente
para disponer de `balc`; sus ejecutables y archivos propios se crean en
directorios temporales que se eliminan al terminar. Requiere el entorno POSIX
que ya utilizan las pruebas del proyecto.

Los cinco métodos de prueba cubren validaciones unitarias, recursos bajo un
límite de 32 descriptores, seis programas BAL del Proyecto I, el fixture de las
34 instrucciones y constantes globales con datos little-endian y signo.
La comparación real verifica toda IMEM y cada byte de la sección de datos.

La prueba C comprueba slots, orden de bytes, NOP completo, imagen restante en
cero, formatos de línea, límites máximos, cada posición de truncamiento de un
contenedor de referencia, corrupción de metadatos, bytes sobrantes, errores de
E/S, argumentos inválidos, salida transaccional y liberación repetida.

Para comprobar errores de reserva/cierre y detectar accesos inválidos o fugas:

```bash
cc -Iinclude -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer -DSIM_PRUEBA_FALLOS \
  tests/vliw/test_sim_cargador.c src/Simulador_VLIW/sim_cargador.c \
  -Wl,--wrap=calloc -Wl,--wrap=fclose -o /tmp/test_sim_cargador_san
ASAN_OPTIONS=detect_leaks=1 /tmp/test_sim_cargador_san
```

Los wrappers pertenecen solo al ejecutable de prueba y requieren el enlazador
GNU. No agregan puntos de inyección al código de producción. La regresión
existente se ejecuta con `make -B -k test`; S1 se ejecuta aparte hasta acordar
su incorporación al Makefile.

## Alcance y limitaciones

El cargador valida integridad estructural, no la semántica ISA. Una palabra
reservada o una dependencia incorrecta se conserva sin decodificar: corresponde
a bloques posteriores. BALB no incorpora checksum; una alteración de payload
que conserve la estructura no puede identificarse como corrupción con S1.

El lector BALB no admite binarios crudos de Arquitectura, versiones futuras,
secciones reubicadas ni huecos entre ellas. Esas entradas se rechazan en lugar
de interpretarlas como el formato actual. No modela latencias, fin de programa,
cachés, excepciones ni efectos criptográficos.
