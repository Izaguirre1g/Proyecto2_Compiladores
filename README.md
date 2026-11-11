# Analizadores léxico y sintáctico

Todos los comandos de abajo se ejecutan desde la raíz del repositorio. Se necesita GCC para C11,
Flex y Bison (el parser actual usa Bison y el lexer se genera con Flex). El Makefile usa GNU Make
4.3 o posterior. Las pruebas del parser están en C y usan funciones POSIX disponibles en Ubuntu/WSL.

## Opción 1: WSL

Abre Ubuntu en WSL y entra en el repositorio. Las carpetas de la unidad `C:` de
Windows aparecen bajo `/mnt/c/`. Sustituye la ruta del ejemplo por la tuya:

```bash
sudo apt update
sudo apt install build-essential flex bison uthash-dev
cd "/mnt/c/ruta/al/repositorio"
flex -o src/Analisis_Lexico/lexer_flex.c src/Analisis_Lexico/lexer.l
bison -Wall -Werror -d -o src/Analisis_Sintactico/parser.c src/Analisis_Sintactico/gramatica.y
make
./balc -v examples/factorial.bal
```

Para ejecutar las pruebas del lexer sin `make`:

```bash
flex -o src/Analisis_Lexico/lexer_flex.c src/Analisis_Lexico/lexer.l
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude -Isrc/Analisis_Lexico \
  tests/test_lexer.c src/Analisis_Lexico/lexer.c src/Analisis_Lexico/lexer_flex.c src/Analisis_Lexico/token.c -o tests/test_lexer
./tests/test_lexer
```

## Opción 2: terminal de Ubuntu

Abre una terminal, entra en la raíz del repositorio y compila:

```bash
sudo apt update
sudo apt install build-essential flex bison uthash-dev
cd "/ruta/al/repositorio"
make
make test
```

`make` genera `src/Analisis_Lexico/lexer_flex.c` desde `src/Analisis_Lexico/lexer.l` y conserva la interfaz pública
`lexer_init`/`lexer_next`. Por eso el parser y el AST no necesitan cambios para usar
el nuevo lexer.

`make test` ejecuta las pruebas de ambos analizadores y del AST y muestra los tokens del
factorial. Las pruebas del parser se compilan y ejecutan así (con `balc` ya compilado):

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 tests/test_parser.c -o tests/test_parser
./tests/test_parser
```

## Resultado esperado

Las pruebas terminan con `OK: 18 casos del lexer, incluido factorial.bal`.

## Estructura del código

El código de `src/` está dividido en una carpeta por fase del compilador, en el
mismo orden en que un programa `.bal` las atraviesa:

| Orden | Carpeta | Qué hace | Archivos principales |
|---|---|---|---|
| 1 | `src/Analisis_Lexico/` | Convierte el texto en tokens (Flex). | `lexer.l`, `lexer.c`, `token.c` |
| 2 | `src/Analisis_Sintactico/` | Revisa la gramática y construye el AST (Bison). | `gramatica.y`, `puente_lexer.c`, `ast.c` |
| 3 | `src/Analisis_Semantico/` | Tabla de símbolos, tipos, módulos (`bring`) y memoria. | `semantic.c`, `symbol_table.c`, `semantic_modules.c`, `memory.c` |
| 4 | `src/Representacion_Intermedia/` | Código de tres direcciones y bloques básicos. | `ir.c` |
| 4b | `src/DCE/` | Eliminación de código muerto con análisis de variables vivas, sobre la IR (`balc --dce`). Ver [docs/dce.md](docs/dce.md). | `dce.c` |
| 5 | `src/Generacion_Codigo/` | Traduce el AST a ensamblador VLIW (expresiones, control, funciones). | `generacion_*.c`, `funciones_*.c` |
| 6 | `src/Resolucion_y_Enlace/` | Resuelve variables, etiquetas y saltos, y enlaza las funciones. | `resolucion_*.c`, `enlace_funciones.c` |
| 7 | `src/Codificacion_Binaria/` | Convierte cada bundle en palabras binarias y escribe `.bin`/`.mem`. | `codificacion_binaria.c`, `formato_binario.c` |
| — | `src/Linea_Comandos/` | Programa `balc`: opciones de la terminal y diagnósticos en JSON. | `main.c`, `cli.c`, `diagnosticos.c` |
| — | `src/Deteccion_Clones/` | Clones Tipo I y II entre archivos, comparando el camino en la gramática LL(1) (lo usan `balc --clones` y el IDE). | `clones.c` |
| — | `src/IDE/` | Editor GTK: sugerencias, autocierre, errores y correcciones. | `ide_*.c` |
| — | `src/LL1/` | Gramática LL(1), PRIMERO/SIGUIENTE, tabla M y analizador predictivo; el IDE la consulta para sugerir y corregir. Ver [docs/ll1.md](docs/ll1.md). | `ll1_*.c` |

Las interfaces públicas (`.h`) están todas en `include/`. Solo tres
encabezados viven junto a su código porque son internos o generados:
`lexer_flex.h`, `parser.h` y `generacion_interna.h`. Las pruebas están en
`tests/` y los programas de ejemplo en `examples/`. Los tres programas de la
eliminación de código muerto están aparte, en `examples/dce/` (ver [docs/dce.md](docs/dce.md)).

## Uso del compilador

```bash
balc [-o <archivo>] [-v] [-s] [-t] [-m] [-p] <archivo_fuente.bal>
balc -x [-o <archivo>] [-v] [-p] <archivo.asm>
```

`balc` siempre compila hasta el binario; las opciones agregan salidas y se
pueden combinar.

| Opción | Descripción |
|---|---|
| `-o <archivo>` | Nombre del binario de salida. Si se omite, el del fuente con `.bin`. |
| `-v` | Muestra cada fase del compilador y las decisiones importantes. |
| `-s` | Genera el ensamblador (`.asm`) además del binario. |
| `-t` | Imprime el AST. |
| `-m` | Imprime la tabla de símbolos, la memoria asignada y la dirección de cada función. |
| `-x` | Toma un archivo de ensamblador y genera el binario. |
| `-p` | Muestra los paquetes generados en ensamblador y en binario. |

El `.bin` usa el [formato BALB v1](docs/formato_binario.md): encabezado,
codigo y datos globales. Junto a el se genera un `.mem` de instrucciones
compatible con la IMEM de Arquitectura. El encabezado no se carga en la IMEM.

```bash
./balc -v examples/factorial.bal          # factorial.bin y factorial.mem
./balc -s -p examples/ejercicio3_puntajes.bal
```

## Modo de pruebas del frontend

Para revisar solo el lexer o el parser, sin generar archivos:

```bash
./balc --frontend -t examples/factorial.bal
./balc --frontend -v -t examples/factorial.bal
make test-parser
make test-ast
```

Con `--frontend`, `-t` valida la sintaxis y muestra el AST con sangría y
posiciones del fuente; sin `-t` solo analiza tokens y `-v` los lista. La
estructura del árbol y las acciones de Bison se explican en `docs/ast.md`. La integración y sus
límites se explican en `docs/parser.md`; el diseño léxico está en `docs/lexer.md`.

## IDE

El IDE es una ventana de escritorio hecha con GTK 3. Revisa el código mientras
se escribe, usando el mismo léxico, parser y semántico de `balc`.

### Instalar GTK

Solo hace falta la primera vez:

```bash
sudo apt update
sudo apt install libgtk-3-dev pkg-config
```

En WSL la ventana se abre sola si se tiene WSLg (Windows 11 o Windows 10
actualizado). Se puede comprobar con `echo $DISPLAY`: si muestra algo como `:0`,
funciona.

### Compilar y abrir

```bash
make ide
./balc-ide examples/factorial.bal   # abre un archivo
./balc-ide                          # abre un archivo nuevo, sin nombre
```

Para probar la eliminación de código muerto, abre uno de sus ejemplos y pulsa
el botón **Código muerto**:

```bash
./balc-ide examples/dce/dce_llamadas.bal   # llamadas con efectos que se conservan
./balc-ide examples/dce/dce_ciclo.bal      # código muerto en un ciclo (2 pasadas)
./balc-ide examples/dce/dce_division.bal   # divisiones que pueden fallar
./balc-ide examples/dce/dce_ensamblador.bal  # "Aplicar al código" y el ensamblador baja de 528 a 435 bundles
```

Los archivos están en la carpeta `examples/dce/`. También se pueden abrir desde
el IDE con el botón **Abrir** (Ctrl+O):

| Archivo | Qué muestra | Abrir en el IDE | Ver en la terminal |
|---|---|---|---|
| [`examples/dce/dce_llamadas.bal`](examples/dce/dce_llamadas.bal) | Asignaciones muertas mezcladas con llamadas que escriben en un arreglo: las llamadas se conservan. | `./balc-ide examples/dce/dce_llamadas.bal` | `./balc --dce examples/dce/dce_llamadas.bal` |
| [`examples/dce/dce_ciclo.bal`](examples/dce/dce_ciclo.bal) | Código muerto dentro de un ciclo; una variable queda muerta recién en la segunda pasada. | `./balc-ide examples/dce/dce_ciclo.bal` | `./balc --dce examples/dce/dce_ciclo.bal` |
| [`examples/dce/dce_division.bal`](examples/dce/dce_division.bal) | Una división entre una variable se conserva porque puede fallar; una entre un número fijo se elimina. | `./balc-ide examples/dce/dce_division.bal` | `./balc --dce examples/dce/dce_division.bal` |
| [`examples/dce/dce_ensamblador.bal`](examples/dce/dce_ensamblador.bal) | Solo parámetros `int`, así que el backend genera su ensamblador: con **Aplicar al código** el ensamblador y el binario salen sin el código muerto (528 → 435 bundles) y el simulador da el mismo resultado. | `./balc-ide examples/dce/dce_ensamblador.bal` | `./balc --dce examples/dce/dce_ensamblador.bal` |

`make ide` genera el ejecutable `balc-ide`. No se sube al repositorio, porque se
vuelve a generar al compilar.

### Qué hace

| Función | Cómo se usa |
|---|---|
| Colores | El código se colorea como en VS Code (tema oscuro Dark+): control (`whether`, `whale`, `give`…) en morado, palabras del lenguaje y operadores (`create_funk`, `gauss`…) en azul, tipos en verde agua, funciones en amarillo, variables en celeste, números en verde claro, textos en naranja, comentarios en verde y `( ) [ ] { }` con un color por nivel. Usa el mismo lexer del compilador. |
| Errores en vivo | Medio segundo después de dejar de escribir, los errores se subrayan en rojo. Al pasar el mouse encima se ve la explicación. |
| Lista de errores | Está abajo de la ventana. Al hacer clic en un error, el cursor salta a su línea. |
| Sugerencias | Al escribir aparecen las palabras clave y variables que empiezan igual. Se eligen con ↑/↓ y se aceptan con Enter o Tab. Esc las cierra. |
| Autocierre | Al escribir `(`, `[`, `{`, `"` o `$` se agrega su cierre. Enter entre `{}` baja la llave y agrega sangría. |
| Delimitadores | Si sobra o falta un paréntesis, corchete o llave, se indica cuál y dónde se abrió. |
| Mensajes en español | Los errores del parser se traducen, por ejemplo `no se esperaba el nombre 'numero' en este lugar`. |
| Corrección automática | Si el IDE sabe arreglar un error (un `;` que falta, un `)` o `}` sin cerrar, un cierre que sobra), la lista muestra el arreglo y un botón **Corregir**. Ctrl+. corrige el error de la línea del cursor y Ctrl+Z lo deshace. |
| Código muerto | El botón **Código muerto** elimina los dos tipos de código muerto: asignaciones que nadie lee (análisis de variables vivas) y código inalcanzable (después de un `give`, o en una rama cuya condición siempre es falsa, con propagación de constantes). **Aplicar al código** cambia el código por el código sin el código muerto, así que el ensamblador y el binario salen de él. Las líneas eliminadas completas se tachan y las que solo perdieron una parte (por ejemplo, la llamada se conserva) quedan en gris. La pestaña **Código muerto** las lista, y **Ver IR y variables vivas** muestra la IR antes y después y la tabla `use`/`def`/`IN`/`OUT`. Ver [docs/dce.md](docs/dce.md). |
| Ensamblador | El botón **Ensamblador** genera el ensamblador VLIW del código del editor (aunque no esté guardado), el mismo que `balc -s`. La pestaña **Ensamblador (.asm)** muestra cada bundle con sus 4 slots (`ALU \| LSU \| BRU \| CRIPTO`) y **Binario** muestra cada bundle con su PC y su codificación, como `balc -p`. |
| Clones | El botón **Buscar clones** compara el archivo abierto (aunque no esté guardado) con los demás `.bal` de su carpeta. Los clones se pintan de amarillo y la pestaña **Clones** dice dónde está cada copia; un clic lleva al fragmento. |

| Atajo | Acción |
|---|---|
| Ctrl+N | Archivo nuevo |
| Ctrl+O | Abrir archivo |
| Ctrl+S | Guardar |
| Ctrl+Espacio | Mostrar sugerencias |
| Ctrl+. | Corregir el error de la línea del cursor |

Si hay cambios sin guardar, el IDE pregunta antes de cerrar o de abrir otro archivo.

Para revisar el código sin guardarlo, el IDE crea un archivo oculto
`.ide_<nombre>.bal` en la misma carpeta y lo borra enseguida. Va en la misma
carpeta para que los `bring` encuentren sus módulos.

### Código muerto desde la terminal

`balc --dce` muestra la IR, las variables vivas de cada bloque, lo que se
eliminó y la IR después del DCE:

```bash
./balc --dce examples/dce/dce_llamadas.bal
./balc --dce examples/dce/dce_ciclo.bal
./balc --dce examples/dce/dce_division.bal
make test-dce test-ide-dce                  # pruebas
```

### Clones desde la terminal

`balc` también busca clones tipo I (código idéntico) y tipo II (mismo camino en
la gramática con otros nombres o valores) entre varios archivos:

```bash
./balc --clones examples/*.bal              # listado para leer
./balc --clones --json a.bal b.bal          # JSON
./balc --clones --minimo 6 examples/*.bal   # clones más pequeños (por defecto 10 tokens)
```

Para decidir si dos fragmentos son clones se sigue el **camino de la gramática**:
cada archivo se analiza con el analizador LL(1) (`src/LL1/`), y dos fragmentos
son clones si el analizador aplicó las mismas producciones en el mismo orden.

| Fragmentos | ¿Clon? | Por qué |
|---|---|---|
| Iguales salvo espacios, saltos de línea y comentarios | Tipo I | El lexer los descarta: la derivación es la misma. |
| `factorial` copiada con otras variables y valores | Tipo II | Mismas producciones; solo cambian los `id` y `num`. |
| `x : (a gauss 1);` y `x : a gauss 1;` | No | Los paréntesis usan `primario → ( expresion )`: otro camino. |
| El mismo cuerpo en un `whale` y en un `whether` | No (solo el bloque de adentro) | `sentencia → ciclo` frente a `sentencia → condicional`. |

Un archivo con errores de sintaxis no se revisa y se avisa cuál es.

### Diagnósticos en JSON

El IDE también puede usar los errores de `balc` en formato JSON:

```bash
./balc --diagnosticos tests/pipeline_error.bal
```

Termina con `0` si no hay errores, `1` si el fuente tiene errores y `2` si falló algo interno.

### Pruebas

```bash
make test-diagnosticos
make test-ide-nucleo
make test-ide-analisis
make test-ide-mensajes
make test-ide-arreglos
make test-clones
```

Estas pruebas no necesitan GTK y también se ejecutan con `make test`.

## Lenguaje ampliado

El ejecutable se llama `balc`. Las reglas actuales,
las diferencias con el PDF del avance y los límites de esta fase están en
[docs/lenguaje_actual.md](docs/lenguaje_actual.md).

```bash
./balc --frontend -t examples/lenguaje_ampliado.bal
./balc --frontend -t examples/matematicas.bal
```

Incluye declaraciones sin inicialización, lógica, residuo/potencia, cycle,
colecciones, importaciones, constantes y seek/seize en el parser y AST.
No incluye ejecución ni carga/enlace de módulos.

## Generación básica para integración

Se añadió un módulo de traducción de expresiones puras, inicializaciones y
asignaciones escalares al ensamblador de la ISA del equipo. Recibe referencias
de memoria externas y no está conectado todavía a la CLI como compilador completo.

```bash
make test-generacion
make test-cli
./tests/test_generacion --ejemplo
```

El alcance, la conexión con símbolos y las limitaciones están en
[docs/generacion_basica.md](docs/generacion_basica.md).
`make test` incluye las pruebas nuevas. `-s` sigue reservado hasta integrar el backend.

## Documentación del proyecto
La documentación general de todo el proyecto se puede encontrar acá
[Documentación](https://github.com/Izaguirre1g/Proyecto-Parte-1-Compiladores-e-Interpretes-Definici-n-de-Sintaxis-de-Lenguaje/blob/371cc01ac19cb05b255697d965b58717666bdbf7/docs/latex/Documentacion.pdf)
