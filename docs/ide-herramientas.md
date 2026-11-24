# Herramientas del compilador en el IDE

Construir con `make ide` (GTK3, pkg-config y Python3 ya usados por el proyecto).
Mantener `balc`, `balc-ide` y `tools/` en la raíz del proyecto. Las herramientas
se buscan junto al ejecutable, independientemente del directorio de lanzamiento.

## Proyecto I ▾

El menú contiene exactamente tres acciones:

- **Análisis léxico**: muestra los tokens del lexer real del búfer actual,
  incluidos cambios sin guardar. Columnas: número, lexema, token, categoría,
  línea, columna y diagnóstico. El resumen excluye EOF, comentarios y espacios.
  Las posiciones son las originales del lexer (columnas Unicode, desde 1).
- **Visualizar AST**: elegir dónde guardar el JSON. Se ejecuta `balc --ast-json`
  sobre una instantánea privada del editor. No hace análisis semántico. Si falla
  el análisis léxico o sintáctico, se informa el diagnóstico real y no se publica
  el resultado. Se abre el **original** `tools/visualizar_ast.html`.
- **Ensamblar archivo .asm**: seleccionar un ASM existente. Se valida con
  `balc -x` en una carpeta privada y se guardan `.bin` (BALB) y `.mem` junto al
  ASM original, previa confirmación de cualquier reemplazo. No se modifica el ASM.

## Exportación del ensamblador

**Ensamblador → Exportar archivos...** guarda tres archivos con una misma base:
`.asm`, `.bin` y `.mem`. La ventana actual corresponde al backend tradicional
**O0**; no exporta el O2 de la comparación. Se conserva una copia propia de los
resultados que muestra esa ventana, aunque el editor cambie después.

El ASM se copia sin cambios; BIN contiene exactamente los bytes BALB generados
por ese backend, incluidos cabecera y datos globales. MEM contiene únicamente
instrucciones, en el formato hexadecimal de `cb_escribir_mem`, recodificadas
desde el ASM mostrado con el mismo codificador. Los NOP y guardas se conservan.

Se confirman los archivos existentes antes de escribir. GIO gestiona errores y
publicación individual; **el conjunto de tres archivos no es una transacción**:
si una escritura falla, el mensaje identifica los archivos ya guardados y el
archivo que falló. Las rutas con espacios se pasan como argumentos independientes,
nunca se construyen comandos de shell.

## Spilling de Proyecto II

En **-O0/-O1/-O2**, pulsar **Visualizar Spilling**. Esta acción vuelve a leer el
editor vigente (puede diferir de la comparación ya abierta), pide un destino JSON
y ejecuta el **original** `tools/spilling_json.py` con una instantánea privada.
El script ejecuta O2 con tres registros y contrasta su reconstrucción con la IR y
las métricas reales de `balc -O2` antes de producir el JSON. No se modifica la
asignación de registros ni se utiliza la ruta genérica `--ir-spilling`.

Se abre el **original** `tools/visualizar_spilling.html`; conserva grafos,
variables vivas, bloques, cargas, almacenes y evolución de la asignación. Las
limitaciones del O2 actual siguen vigentes, incluidos módulos sin enlace IR
acreditado y entradas no soportadas; se presentan sus errores sin datos ficticios.

## Cargar los HTML

Ambos HTML usan su selector local **Archivo JSON**. El IDE informa la ruta y la
copia al portapapeles para pegarla en el selector del navegador. No se usa `fetch`
sobre `file://`, ni servidores, ni se cambia el HTML. Los JSON guardados permanecen
hasta que el usuario los elimine; las instantáneas internas se borran al terminar.

Los navegadores confinados (por ejemplo Snap) pueden no ver `/tmp` u otras rutas.
Guardar el JSON en una carpeta permitida y ejecutar el IDE desde una ubicación
accesible para el navegador. Un fallo al abrir el navegador no borra el JSON.
Las herramientas externas se ejecutan de forma síncrona: programas grandes pueden
ocupar el IDE mientras compilan; no se reutiliza el resultado de otra ejecución.

## Pruebas

`make test-ide-herramientas`: dos grupos C (lexer/categorías/posiciones y
MEM/escritura/colisiones/errores) y siete casos Python (AST vigente/errores,
ASM equivalente a CLI/BALB/errores, Spilling O2 vigente/errores y proceso ausente).
Los archivos de prueba se crean en temporales externos. No requiere pantalla.

`make test-ide-herramientas-gtk` requiere pantalla: ejercita la tabla con un
búfer sin guardar y un error, la exportación después de liberar la entrada,
la igualdad BALB/MEM y la cancelación de reemplazos mediante diálogos GTK reales.
No prueba la carga de archivos dentro del navegador.

Regresión: `make -B ide`, `make test-ide-nucleo`, `make test-ide-colores`,
`make test-ide-analisis`, `make test-ide-ensamblador`, `make test-ide-optimizacion`,
`make test-ast-json`, `make test-formato-binario`, `make test-codificacion-binaria`
y `make test`. Ante ejecutables versionados sin permiso de ejecución o antiguos,
reconstruir con `make -B test` en una copia externa antes de repetir `make test`.
No cambiar permisos de los binarios del repositorio para ocultar el problema.

La compilación y las pruebas sin pantalla no sustituyen la inspección gráfica:
comprobar el menú, tabla, diálogos, cancelación y reemplazo, exportación y apertura
de los HTML con los JSON generados. Las evidencias de ejecución se conservan
fuera del repositorio y el informe final distingue lo observado de lo pendiente.
