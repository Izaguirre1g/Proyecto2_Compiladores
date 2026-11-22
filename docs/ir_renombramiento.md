# Versionado virtual local

`ir_renombrar_local()` es una pasada independiente de `$rN -> xN`.
Entrada IR inmutable, callback de elegibilidad y salida cero. El caller
acredita identidad semántica, localidad, int32 y ausencia de alias; O2 utiliza
su tabla semántica vinculada. La salida es propia y se libera con `ir_liberar`.

Cada definición elegible recibe un `$tN` nuevo. Las lecturas posteriores en
la región usan la última versión. Antes de memoria, control, llamada,
división/residuo/potencia o fin se copian las últimas versiones a los objetos
originales. No cruzan fronteras: no se necesitan phi, dominancia ni supuestos
sobre rutas. Un parámetro/global/byte no se renombra por ese callback.

RAW se conserva como uso de la versión que realmente produjo el valor.
Una lectura anterior sigue usando su versión anterior aunque haya otra
escritura al mismo nombre. Dos escrituras obtienen destinos distintos.
Las publicaciones finales conservan la memoria observable. Los datos de
pruebas cruzan a=3, b=5, copia=a, a=b, b=copia: el resultado sigue siendo 8.

Las cuentas `war_eliminadas`/`waw_eliminadas` describen definiciones con un
lector/escritor previo en la región original. No cuentan pares físicos ni
prometen que el asignador no reutilice registros; el DAG físico se reconstruye
tras spilling y vuelve a comprobar sus riesgos reales.

Máximo 4092 instrucciones de entrada. Reserva a lo sumo 2*N+1 instrucciones,
N versiones activas y 2*N+1 referencias de lectura. Tiempo O(N²) por búsquedas
lineales; espacio O(N), aparte del CFG que reconstruye la API existente.
La expansión de publicaciones cabe porque cada publicación corresponde a
una definición previa. Se comprueba overflow del contador de temporales.
Se publica únicamente tras construir el CFG; fallos liberan todo parcial.

Pruebas: intérprete COPY/suma independiente, distinción de destinos WAW,
relación productor/consumidor RAW, WAR cruzada, raíces DCE, argumentos inválidos,
entrada/salida ocupada y fallos sucesivos de reservas. M2/RTL validan además
los programas que realmente consumen el resultado de esta pasada.

Revisión requerida de Meibel. No cambia su política de unrolling/reordenamiento
ni la asignación/spilling de Dylan. Los nuevos temporales pueden aumentar la
presión; las métricas deben presentar ese costo en lugar de ocultarlo.
