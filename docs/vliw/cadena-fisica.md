# Cadena consumidora física VLIW

## Responsabilidad y frontera

IMPLEMENTADO: `vliw_cadena_construir` coordina el procesamiento de operaciones
ISA explícitas acreditadas. Pertenece al trabajo consumidor de Javier. No
transforma BAL/IR, no asigna registros, no ejecuta spilling ni utiliza el backend
tradicional como sustituto del productor físico pendiente.

```text
VliwCadenaEntrada
  → I1-A2a/S2 → CFG → DAG → proyección propia → I1-A1
  → S7 → verificador S7 explícito → S8-A → CB → contraste S2/destinos
  → estadísticas y JSON S8-C → VliwCadena propia
```

Cada fase se llama realmente. Que S7 verifique internamente o que la proyección
use I1-A1 no elimina las verificaciones explícitas de la cadena. No se corrige
un plan rechazado ni se sustituyen las dependencias generadas.

## Entrada pública

`VliwCadenaEntrada` recibe `const VliwCfgEntrada *flujo`, efectos adicionales y
cantidad, textos individuales y cantidad, generación, créditos de efectos/ABI
y `VliwGarantiasFisicas`. Hay una palabra/slot/ID físico por operación. CFG
recibe funciones, símbolos, transferencias, fronteras y regiones ya acreditadas.
No recibe nombres virtuales para asignarlos ni direcciones antiguas como símbolos.

La cantidad estructural se acota antes de multiplicar o recorrer. Las cantidades
de textos/efectos deben coincidir con operaciones. Generaciones incompatibles,
obligaciones pendientes, falta de completitud/efectos y reubicaciones pendientes
rechazan antes de construir. Call/ret sin ABI acreditado también rechaza.
Estas declaraciones no se autentican ni certifican equivalencia BAL.

## API y salida

```c
VliwCadenaEstado vliw_cadena_construir(const VliwCadenaEntrada *,
    VliwCadena **, VliwCadenaDiagnostico *);
void vliw_cadena_liberar(VliwCadena **);
```

La salida inicialmente NULL se publica solo tras todas las fases. `VliwCadena`
posee metadatos, CFG, DAG, proyección, plan, ASM, `CbPrograma`, estadísticas y
JSON S8-C. Se retienen CFG/DAG para inspeccionar de dónde provienen exactamente
las tablas copiadas; no hay punteros a temporales del productor. El resultado
puede verificarse después de liberar o sobrescribir sus entradas.

Orden de liberación: visualización, CB, ASM, plan, proyección, DAG, CFG y
metadatos. Los destructores toleran recursos todavía no adquiridos. No se
liberan préstamos ni se duplican liberaciones. Un reintento después de fallo
utiliza salida NULL y los mismos datos sin reparación silenciosa.

El vacío produce plan/ASM/estadísticas vacíos y JSON válido. CB no acepta
fuente vacía: no se agrega un NOP artificial y `codificado=false`. Ese resultado
no es una imagen ejecutable. En programas no vacíos CB se ejecuta realmente.

## Diagnósticos

`VliwCadenaDiagnostico` conserva fase, categoría, estado original, generación,
instrucción/dependencia/bloque/destino aplicables y copia del mensaje. En
proyección conserva además fase y estado anidados; en emisor puede conservar
su estado de verificación interno. VAC identifica índices no disponibles.

Se distinguen ISA, metadatos, control/CFG, dependencias/DAG, ABI incompleto,
reubicación, capacidad, planificación/verificación, emisión/codificación,
memoria y E/S. La fase identifica el dominio del estado original. Un retorno
CB entero se acompaña de su diagnóstico actual; un mensaje de reserva se
clasifica sin cambiar la interfaz CB. No se reclasifican todos los fallos como
argumentos ni se continúa sobre resultados parciales.

## Layout y reubicaciones

S8-A sustituye destinos directos por etiquetas antes de las guardas del bloque.
El contraste final verifica cada slot, incluido NOP, y compara campos S2; para
BRU directa verifica además `PC_bundle + inmediato == inicio_bloque_destino*16`.
No se usa el desplazamiento viejo como destino lógico. En SYER se conserva
el inmediato y no se inventa un destino estático.

La cadena no reubica constantes que materializan direcciones en x13 u otras
ALU. No llama RR/RS ni transforma SYER en SYE. El productor debe declarar y
acreditar ausencia o resolución válida de esas obligaciones para este layout;
si no puede, debe dejar pendientes o reubicaciones_resueltas=false y la entrada
se rechaza. Una llamada barrera no salva GPR destruidos por el callee.

El fixture call/ret probado utiliza SYE con enlace x1, callee compuesto solo
por SYER de retorno y continuación en bucle. No destruye otros registros ni
materializa x13. Esto permite comprobar el layout físico de ese fixture, pero
no acredita el ABI del backend ni retorno semántico de main.

## Estadísticas y evaluación

S8-C informa operaciones, bundles vacíos/protección, slots, utilización,
bytes IMEM y dependencias por tipo. El DAG conserva separación crítica.
Ninguna de esas cifras es un ciclo ejecutado o IPC dinámico.

La suite nueva crea una referencia física manual N+3 y una emisión real de la
cadena: cuatro operaciones ALU/load/ALU/store. CB escribe MEM y binario reales;
se contrastan bytes/longitudes, B1 ejecuta y B2 agrupa como demostración. La
comparación estricta debe ser EQUIVALENTE antes de usar ciclos/IPC. Una prueba
con presupuesto cero exige INCONCLUSO, no una equivalencia o terminación falsa.
Estos fixtures no cumplen cuotas oficiales de optimizaciones BAL.

## Pruebas, complejidad y bloqueos

`test_vliw_cadena_fisica.py` descubre grupos positivos, riesgos, control,
CRIPTO, negativos, límites, recursos, fallos de fases y artefactos/evaluación.
Sus fuentes C reutilizan únicamente los fixtures del nuevo test de proyección;
no incluyen ni alteran pruebas anteriores. La variante de fallos intercepta
reservas y rechazos de consumidores solo cuando el caso lo arma; los positivos
recorren todas las APIs reales.

Las pruebas cercanas a IMEM distinguen 340 barreras CRIPTO, 341 bloques
independientes, rechazo de 342 bloques y condicional largo no relajado. Los
costos incluyen reconstrucciones/validaciones existentes y se miden; no se
promete costo lineal ni planificabilidad de toda entrada de 4092 operaciones.
Instrumentos no prueban ausencia universal de UB. Los comandos, tiempos,
códigos, fallos iniciales y resultados definitivos se conservan externamente.

COMPLETADO CON FIXTURES: cadena consumidora reutilizable y evaluación aplicable.
PENDIENTE DE ACUERDO: productor, ABI, expansiones, símbolos/reubicaciones y
observables. BLOQUEADO: integración BAL productiva y niveles CLI hasta cumplir
el contrato de entrega; no se ha iniciado un backend alternativo.
