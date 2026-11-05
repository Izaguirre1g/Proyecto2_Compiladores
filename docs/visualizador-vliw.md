# Visualización estática VLIW — S8-C / issue #28

La biblioteca presenta una planificación S7 sin modificar instrucciones, mapas,
bundles, dependencias ni CFG. Ofrece tabla, CSV, JSON y estadísticas separadas.
Consume instrucciones ISA y metadatos externos; no necesita IR, frontend BAL,
CLI, codificador ni simulador para visualizar un plan.

## Interfaz pública y propiedad

`include/vliw_visualizador.h` incluye las interfaces públicas de S7 y S8-A.
La implementación solo enlaza S7; `VliwDestinos` es una anotación opcional.

```c
char *salida = NULL;
VliwVisualDiagnostico diagnostico;
VliwVisualOpciones opciones = {0, true, true}; /* Ancho automático. */
VliwVisualEstado estado = vliw_visualizar(&entrada, plan, NULL,
    VLIW_VIS_TABLA, &opciones, &salida, &diagnostico);
/* Usar salida únicamente cuando estado == VLIW_VIS_OK. */
vliw_visual_liberar(&salida);

VliwEstadisticas metricas;
estado = vliw_estadisticas(&entrada, plan, &metricas, &diagnostico);
```

Entrada, plan, destinos, opciones, arreglos y cadenas son prestados, accesibles e
inmutables durante cada llamada. Las cantidades deben describir almacenamiento
real y los textos deben terminar en NUL; C no permite detectar punteros colgantes.
Los índices originales se distinguen de los `id` externos no consecutivos.

La cadena de salida es propia e independiente de las entradas. Debe recibirse en
un puntero inicialmente NULL; solo se publica después de completar el reporte.
En error se conserva el puntero previo y no se entrega una cadena parcial.
`vliw_visual_liberar` admite NULL y deja el puntero en NULL. Las estadísticas se
calculan en una estructura local y solo reemplazan `resultado` en éxito.

El diagnóstico opcional contiene estado propio, código de S7, instrucción,
dependencia y mensaje literal estático. Los estados distinguen argumento, plan,
anotación de destino, texto UTF-8 inválido, memoria y generación. No se escribe
ningún archivo desde la biblioteca.

## Verificación y recorrido

Cada API llama `vliw_verificar` antes de recorrer el plan. S7 comprueba capacidad,
slots, índices, mapas, conteos, bloques, guardas y restricciones de dependencias.
Un rechazo no produce un reporte que describa el plan como válido. No se vuelve
a ejecutar el selector, no se crea otro grafo y no se reparan metadatos.

Después se recorren bundles y slots en su orden original. Se calculan métricas,
se comprueban anotaciones/textos y se construye privadamente la modalidad elegida.
El buffer comprueba tamaños antes de sumar o crecer; los fallos de reserva y de
`vsnprintf` liberan la salida privada. El formato decimal se obtiene con enteros,
sin cambiar el locale global del cliente. JSON expresa cocientes redondeados a
seis decimales; la estructura pública conserva los cocientes como `double`.

El texto no se analiza como ensamblador. Puede estar ausente o ser descriptivo;
verificarlo como ISA corresponde a S8-A/CB. Los contadores de operaciones provienen
de los índices reales de S7 y no del contenido de una cadena o palabra binaria.

## Bundles, bloques y clasificación

La tabla muestra índice de bundle, PC, bloque, inicio, clase, control y los cuatro
slots en orden ALU/LSU/BRU/CRIPTO. `I0` identifica el índice original 0, no su `id`.
El PC es `16 * bundle`, con origen cero. `B0` es un identificador del reporte,
no una etiqueta de ensamblador nueva. Los comienzos se leen de `inicio_por_bloque`.

Las categorías son:

- `PROTECCION`: los dos primeros bundles exigidos por S7 en cada bloque distinto
  de B0, y también en B0 cuando algún sucesor del CFG apunta a él.
- `VACIO`: otro bundle completamente vacío; no se atribuye a una causa interna.
- `CONTROL`: bundle con transferencia declarada en metadatos.
- `OPERACIONES`: bundle no vacío restante.

La marca de inicio es independiente de la categoría. Las guardas son también
bundles vacíos a efectos de estadísticas; no se suman otra vez al total.
Los vacíos de barreras CRIPTO se presentan como `VACIO` cuando no son guardas.
No se afirma que cada NOP sea una espera RAW ni que el flush reemplace N+3.

Una instrucción sin texto conserva su índice y cuenta de operación. La tabla
muestra `<SIN TEXTO>`; JSON distingue `null` de `""` y marca `texto_ausente`.
CSV utiliza campo vacío más indicador de ausencia. Los slots NOP tienen índice
nulo/vacío y no se confunden con estas instrucciones.

## Opciones y formatos

Con opciones NULL, la tabla usa ancho 48 y muestra dependencias/estadísticas.
Un ancho explícito entre 8 y 160 bytes incluye el prefijo de índice. Cero ajusta
cada columna hasta 96 bytes. El truncamiento se indica con `...`. La tabla escapa
controles, bytes no ASCII, barras inversas y separadores; no ejecuta secuencias
ANSI ni permite que un texto añada filas. CSV/JSON no se truncan.

Las banderas de secciones solo afectan la tabla. JSON siempre incluye todas las
secciones; CSV es una tabla plana de bundles, sin intercalar otros encabezados.
Los textos exportados deben ser UTF-8 válido. JSON escapa comillas, barra inversa
y controles; CSV entrecomilla campos y duplica comillas, conservando comas,
CR/LF, tabuladores y texto Unicode. Los registros CSV terminan con CRLF.

Ejemplo RAW, proyectado a las columnas principales de la tabla:

```text
Bundle | PC     | Bloque | Clase       | ALU                | LSU              | BRU | CRIPTO
0      | 0x0000 | B0     | OPERACIONES | sumai x5,x0,7      | cargai x8,x0,0   | NOP | NOP
1      | 0x0010 | B0     | VACIO       | NOP                | NOP              | NOP | NOP
2      | 0x0020 | B0     | VACIO       | NOP                | NOP              | NOP | NOP
3      | 0x0030 | B0     | OPERACIONES | suma x6,x5,x8      | NOP              | NOP | NOP
```

Proyección de sus ocho primeras columnas CSV:

```csv
bundle,pc,bloque,alu,lsu,bru,cripto,operaciones
0,0x0000,0,"sumai x5,x0,7","cargai x8,x0,0",NOP,NOP,2
1,0x0010,0,NOP,NOP,NOP,NOP,0
2,0x0020,0,NOP,NOP,NOP,NOP,0
3,0x0030,0,"suma x6,x5,x8",NOP,NOP,NOP,1
```

El CSV completo agrega `inicio_bloque`, `proteccion`, `control`, `clase`, cuatro
columnas `*_indice` y cuatro `*_texto_ausente`. Esos índices permiten recuperar
la asociación con S7 aunque los textos sean iguales o estén ausentes.

JSON tiene versión 1 y las secciones `resumen`, `bundles`, `bloques`,
`instrucciones`, `dependencias`, `estadisticas`. Exporta los cuatro slots con
índice/texto/ausencia, intervalos y sucesores de bloques, identidad y metadatos de
instrucciones, distancias de cada arista y todas las métricas. Extracto válido
seleccionado del mismo fixture:

```json
{
  "resumen": {"naturaleza": "estatica", "unidad": "bundle_IMEM", "origen_pc": 0, "plan_verificado": true},
  "estadisticas": {"instrucciones_utiles": 3, "bundles_totales": 4, "bundles_vacios": 2, "slots_disponibles": 16, "slots_utilizados": 3, "slots_vacios": 13, "utilizacion_porcentual": 18.75, "operaciones_por_bundle": 0.75, "bytes_imem": 64}
}
```

## Dependencias y destinos opcionales

Se presentan únicamente aristas externas RAW, WAR, WAW, MEMORIA, CONTROL y BARRERA.
Cada una identifica origen/destino originales, tipo, mínimo, bundles asignados,
distancia efectiva y `SATISFECHA`. La distancia es la resta de índices estáticos
verificados, nunca tiempo de ejecución ni una dependencia inferida del texto.

Las anotaciones de destino son parciales y opcionales: se verifican referencias,
tipos enumerados, duplicados, asociación a una transferencia y pertenencia al CFG
cuando el bloque no es `VLIW_VACIO`. También puede mostrarse `NO_RESUELTO`. No se
certifica aquí su forma ISA ni su compatibilidad completa con S8-A.

El texto BRU original se conserva, incluido un offset antiguo. El destino lógico
se muestra aparte en tabla/JSON. S8-A sustituye ese operando por su etiqueta al
emitir. Las pruebas comparan exactamente cada operación normal y esa sustitución
explícita de BRU, además de comprobar etiquetas y mapas. Mostrar los tres formatos
no cambia el ensamblador obtenido antes y después del reporte.

## Estadísticas

`VliwEstadisticas` contiene instrucciones útiles, bundles totales/vacíos/con
operaciones/de protección, operaciones por slot, slots disponibles/utilizados/
vacíos, utilización, operaciones por bundle, bytes IMEM, bloques, textos ausentes
y dependencias de cada uno de los seis tipos.

- Slots disponibles = 4 × bundles; utilizados = operaciones originales de S7.
- Slots vacíos = disponibles-utilizados.
- Utilización porcentual = 100 × utilizados/disponibles.
- Operaciones por bundle = instrucciones útiles/bundles.
- Bytes IMEM = 16 × bundles.

Un programa vacío tiene cero en todos los conteos y cocientes, y reportes con
listas/tablas sin filas. Las guardas y vacíos se incluyen en slots y bytes.
Se conserva el límite S7 de 1023 bundles ordinarios: 16368 bytes, último PC 0x3FE0;
0x3FF0 permanece reservado al vector de excepciones.

Estas métricas son estáticas. Un bucle puede ejecutar una posición muchas veces.
El índice del bundle no equivale al ciclo dinámico; utilización y operaciones
por bundle no son IPC. No se mide aceleración, ciclos transcurridos, stalls ni
excepciones. Tampoco se agrega instrumentación a S1–S6.

## Pruebas y herramienta independiente

Desde la raíz del compilador:

```bash
PYTHONDONTWRITEBYTECODE=1 \
S8C_EVIDENCIA=/tmp/s8c-evidencia.json \
S8C_ARTEFACTOS=/tmp/s8c-ejemplos \
python3 tests/test_vliw_visualizador.py
```

La suite usa biblioteca estándar Python y C11 estricto. Compila ejecutables en
TemporaryDirectory. El modo de pruebas independiente puede compilarse así:

```bash
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  src/vliw_scheduler.c src/vliw_visualizador.c tests/test_vliw_visualizador.c \
  -o /tmp/visualizador
/tmp/visualizador
/tmp/visualizador --tabla /tmp/s8c-ejemplos/raw.fixture 0
/tmp/visualizador --csv /tmp/s8c-ejemplos/raw.fixture
/tmp/visualizador --json /tmp/s8c-ejemplos/raw.fixture
/tmp/visualizador --estadisticas /tmp/s8c-ejemplos/raw.fixture
```

`--estadisticas` muestra el resumen escalar JSON mediante la API independiente;
los desgloses por slot/tipo están en la estructura C y en `--json`/`--tabla`.
El protocolo del fixture contiene cantidades, metadatos, aristas, bloques,
anotaciones y textos codificados en hexadecimal (`-` representa NULL). Es solo
un transporte para las pruebas; no analiza ISA ni BAL. La variante de contraste
se compila con `VLIW_VIS_EMISOR` y enlaza S8-A/CB/S2 exclusivamente en las pruebas.

Las pruebas C cubren argumentos, valores concretos, vacío, empaquetado, N+3,
seis tipos, guardas, CRIPTO/control, ausencia de texto, escapes, texto largo,
UTF-8 inválido, corrupción de planes, propiedad, inmutabilidad y capacidad.
Python utiliza `json` y `csv`, compara todos los campos con el ejecutable original
S7, verifica determinismo y contrasta S8-A antes/después de visualizar.

La inyección GNU/Linux recorre cada reserva real de los tres formatos y de las
estadísticas, incluyendo asignaciones del verificador S7. También falla las dos
fases de `vsnprintf`; se intercepta la variante fortificada de glibc sin desactivar
FORTIFY. La prueba de locale `es_CR.utf8` verifica JSON con punto y conserva la
coma del locale del cliente; si no está instalado se informa NO VERIFICADO.

Resultados finales del entorno de trabajo:

| Verificación | Cantidad | Resultado / código |
|---|---:|---|
| C11 estricto |4 ejecutables de la suite|PASS /0|
| C |18 escenarios,380 comprobaciones|PASS /0|
| Python |10 tests,36 planes S7|PASS /0|
| Contrastes S8-A |32 planes, antes/después de los tres formatos|PASS /0|
| Fallos |36 reservas y 6 errores de generación|PASS /0|
| ASan+UBSan; UBSan independiente |suite completa|PASS /0|
| LeakSanitizer explícito |1 intento|NO VERIFICADO: ptrace /1|
| Regresiones S1–S8-A |102 tests Python antes/después|PASS /0|
| Compilador general |`make -B -k test` antes/después|PASS /0|

Las regresiones se ejecutan en copias desechables para no regenerar artefactos
versionados del repositorio real. Los ejemplos y comandos quedan en los archivos
de evidencia solicitados por las variables de entorno. ASan se ejecuta con
`detect_leaks=0`; no sustituye la comprobación de fugas que LSan no pudo realizar.

## Límites e integración pendiente

Está completa la biblioteca independiente de presentación y estadísticas, con
formatos, validación atómica y herramienta de fixtures. La conexión de la bandera
al compilador `balc` del issue #28 queda pendiente de coordinación; no se modifica
CLI, IDE, IR ni archivos de los compañeros. La calidad semántica de metadatos y
las dependencias sigue perteneciendo a sus productores; el visualizador no hace
RAW/WAR/WAW, renombramiento, reordenamiento, liveness ni optimizaciones.
