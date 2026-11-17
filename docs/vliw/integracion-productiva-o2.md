# Integración productiva O2 desde IR

Estado de implementación en la rama `feat/integracion-productiva-o2-vliw`.
Los resultados y límites de validación se publican en evidencias externas;
este documento describe el contrato del código y conserva los antecedentes.

## Pipeline implementado

`balc -O2 -s fuente.bal` usa el frontend y el visitante semántico reales.
Asigna memoria con la variante optativa de referencias y llama a
`ir_vliw_programa()`. No compila el AST original después de transformar IR.

1. Clona el AST exclusivamente para vincular cada uso a su `SimSimbolo`.
   Reutiliza la búsqueda lexical del adaptador tradicional. Los nombres
   internos `__bal_vN` identifican símbolos; N no es offset ni registro.
2. Genera IR con el generador existente. Las constantes globales se inicializan
   en `__o2_init_global`, llamada desde el arranque antes de main. El nombre
   está reservado en esta API optativa.
3. Anota referencias con stride y ancho antes de perder sus tipos en spilling.
4. Descarta funciones no alcanzables desde main o el inicializador mediante
   llamadas directas. No inventa llamadores para acreditar sus retornos.
5. Ejecuta unrolling sobre el patrón de inducción original; después versionado
   virtual local, DCE con raíces explícitas y reordenamiento. Cada pasada
   reconstruye su CFG. DCE posterior al versionado elimina versiones muertas.
6. Ejecuta `ir_aplicar_spilling` y el validador físico existentes, con tres
   registros en el CLI; la API admite de tres a ocho. `$r0..7` usa x16..23.
7. Expande ISA, argumentos, marcos y control antes del CFG/DAG físico. Cada
   operación se codifica mediante CB y se decodifica mediante S2.
8. Llama a la cadena física existente: metadatos, CFG, DAG, proyección,
   I1-A1, S7, verificador S7, S8-A y CB. No sustituye sus dependencias.
9. Resuelve materializaciones con el plan definitivo, reconstruyendo toda la
   cadena si cambia una palabra. Máximo ocho generaciones; sin convergencia
   rechaza y libera el resultado privado.
10. Publica ASM, BALB, MEM, reporte `.o2.json` e IR antes/después/spilling.

La API de fragmentos `ir_vliw_desde_ir()` mantiene su alcance anterior.
La API nueva recibe una unidad BAL completa, AST/tabla vinculados e inmutables
con memoria ya asignada; el caller debe conservarlos durante la llamada.

## Operaciones implementadas

COPY; suma/resta; comparaciones; booleanos; negación; etiquetas; saltos;
backedges; declaración/acceso a arreglos estáticos; referencias indexadas;
LOAD/STORE con ancho de uno o cuatro bytes; funciones; parámetros; ARG/CALL;
RETURN; SPILL_LOAD/STORE. Multiplicación, división, residuo y potencia reutilizan
las expansiones del generador tradicional, no instrucciones ISA inventadas.

Los errores constantes de división/residuo por cero se rechazan. Las variantes
que fallan dinámicamente conservan las secuencias de control del backend
tradicional; no se convierten en un HALT o trap nuevo. Una región que pueda
fallar no participa en el versionado de escrituras retrasadas.

Se admiten enteros int32, booleanos y caracteres de un byte, con char literal
simple ASCII en esta ruta. Las escrituras subpalabra usan `guardab`; no se
promueven a stores de palabra sobre objetos vecinos. Los parámetros escalares
conservan home slots de cuatro bytes. El stride de matrices estáticas procede
de los tipos, no de nombres de registros ya asignados.

## ABI implementada y propuesta de revisión

Es una convención BAL, no una propiedad adicional del ISA.

- x0 constante; x1 RA; x2 SP descendente; x3 GP.
- Arranque: SP=65536, GP=16384. Globales en [16384,32768).
- Marco particular por función: RA en +0, parámetros desde +4, locales según
  la tabla, ranuras globalmente numeradas de spilling reservadas en **cada**
  marco, dieciséis salvaguardas y slots para argumentos pendientes.
- Argumentos 0..3 en x8..x11. Los restantes se reservan en la pila del caller;
  el callee los lee en `marco_final + 4*(indice-4)`. Retorno en x8.
- x4..x15 son volátiles; las expansiones aritméticas existentes los usan.
- x16..x31 se salvan/restauran mediante LSU real en cada función O2, incluidos
  x28/x29 auxiliares. x29 se salva primero y restaura último porque participa
  en cálculos de dirección lejanos. No se finge preservación con una barrera.
- ARG se acumula en slots del marco: una llamada anidada consume sus últimos
  N argumentos y deja intactos los argumentos exteriores todavía pendientes.
- Arreglos 1D: referencia de 32 bits prestada durante la llamada, sin copia,
  longitud oculta ni análisis de alias. Memoria mantiene su orden conservador.

Las APIs anteriores de memoria, spilling y adaptadores conservan sus rechazos
históricos. Las variantes `*_referencias`/`*_expandido` requieren selección
explícita. El CLI tradicional selecciona referencias, conservando la ruta y
codificación anteriores para programas escalares. O1 conserva DCE y el
reescritor BAL de José: si el verificador IDE histórico no admite referencias,
se validan ambas variantes explícitamente con el backend CLI ampliado.
El IDE mantiene sus APIs y límites anteriores.

El máximo de un marco O2 es la zona de pila de 32768 bytes; no es una prueba de
que la recursión o un conjunto de marcos quepan en esa zona. No hay una nueva
protección dinámica contra desbordamiento de pila. Ese límite debe respetarlo
el programa. Las pruebas observan RA/SP/GP y siembran x16..31 con centinelas.

## DCE y observables

`dce_eliminar()` conserva su política histórica. La variante
`dce_eliminar_con_observables()` añade raíces prestadas a OUT en salidas de
función. O2 acredita como observables todos los objetos nombrados de main y
las definiciones globales. La política es uniforme y conservadora: no consulta
IDs, resultados esperados ni categorías del manifiesto de evaluación.

Esto impide perder un `total` final que BAL no vuelve a leer. Aún se eliminan
escrituras reemplazadas, versiones temporales muertas y locales muertos de
otras funciones. No se eliminan llamadas, stores ni operaciones peligrosas.
Las cuentas DCE incluyen instrucciones introducidas por pasadas anteriores.
Revisión requerida de José por el contrato adicional de observación.

## Símbolos y reubicaciones

Cada símbolo conserva posición física de referencia e identidad de función.
El CFG identifica destinos y continuaciones por índices, sin inferir llamadas
por x1. Los posibles destinos de retorno proceden de las llamadas reales.
Una vuelta del CFG no se transforma en una arista retrospectiva del DAG.

Los saltos condicionales largos se expanden en condición inversa hacia una
continuación local más SYE largo. Esto ocurre antes del CFG/DAG, con registros
y efectos reales. Las expansiones aritméticas lineales del backend se importan
antes de calendarizar; sus etiquetas se convierten en referencias estructuradas.
El ensamblador final no se usa para adivinar destinos del programa.

Las direcciones materializadas en x13 ocupan siete operaciones incluso cuando
su valor cabe en un inmediato. Al cambiar bits se invalida y reconstruye la
cadena completa, conservando longitud y procedencia de la expansión.
El reporte relaciona cada índice físico con IR de spilling, bloque y bundle;
el arranque usa origen IR `null`, porque es código sintético de la ABI.

## Capacidad y propiedad

4092 operaciones físicas, 64 funciones incluidas las sintéticas y 1023 bundles
útiles como máximo. Estos límites son distintos: expandir un opcode puede
agotar operaciones, y el camino crítico puede agotar IMEM mucho antes.
Un frame o un conjunto de dependencias grande puede rechazarse. Se conserva
la política global de aristas del DAG y sus límites de recursos.

El resultado inicialmente vacío posee las tres IR, cadena, ASM de la cadena
y JSON. `ir_vliw_programa_liberar()` acepta NULL, libera propiedad y deja cero.
No posee AST ni tabla. Las construcciones parciales se descartan ante fallos.
La publicación C es atómica; la escritura de varios archivos CLI no ofrece una
transacción conjunta frente a fallos del filesystem. Esos errores se informan.

## Validación reproducible

`tests/vliw/test_ir_vliw_programa.py` compila el backend y sondas en temporales,
prueba valores independientes, ámbitos, recursión, cinco argumentos, globales,
bytes adyacentes, marco lejano, capacidad, ocho registros y fallos de reserva.
El pequeño intérprete de pruebas de renombramiento solo sirve como oráculo de
COPY/suma; no reemplaza ninguna ejecución BAL, M2 o RTL.

`tools/vliw/evaluar_integracion_productiva.py --help` describe la campaña
O0/O1/O2. Reutiliza el manifiesto y sus hashes sin sustituir programas. Compara
BIN/MEM, ensamblador ISA, 32 GPR, DMEM completa, CRIPTO y traps en RTL, modos
0 y 2 con L2_MODEL=0, tras flush. Una sonda verifica el retorno real de main.

B1/B2 permanece intacto. Una ejecución con retorno al bucle de arranque sigue
ACTIVA arquitectónicamente: equivalencia semántica y retorno acreditado no
convierten INCONCLUSO en EQUIVALENTE. Tampoco se igualan ciclos de M2 y RTL con
caché. Las métricas se conservan separadas por variante y modo de memoria.

## Límites y revisión pendiente

IMPLEMENTADO: ruta descrita, rechazos explícitos y herramientas de evaluación.
VALIDADO: únicamente lo respaldado por los logs externos de la ejecución.
PENDIENTE DE ACUERDO: revisión de Dylan del ABI/lowering y APIs ampliadas;
Meibel del versionado y orden de pasadas; José de raíces DCE y validación O1.

NO SOPORTADO en O2: módulos/enlace entre unidades, listas, texto, arreglos
parámetro multidimensionales sin contrato de strides, IR_SIZE, arrays dinámicos,
funciones léxicamente anidadas y literales char de más de un byte/escape.
Se rechazan; no se ejecuta el backend tradicional como fallback O2.
La gramática admite llamadas anidadas, no nuevas clausuras o funciones anidadas.

No se introducen niveles BAL O3, asignación nueva de registros, cambios en el
scheduler/simulador/RTL, ni certificación estricta falsa. La integración no
incluye una promesa universal de mejor rendimiento: marcos conservadores,
spilling y salvaguardas pueden aumentar código y ciclos.
