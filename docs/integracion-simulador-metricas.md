# Sesiones independientes del simulador y métricas (M2)

La API `sim_ejecucion` coordina S1, S3–S6 y M1. El ejecutor de
`tests/test_sim_metricas.c` es un auxiliar privado de pruebas: mantiene una
segunda simulación de referencia y permite preparar fixtures. No era una API
pública de sesión ni una herramienta de producción. M2 aporta esa interfaz
sin cambiar los módulos aprobados ni integrarse todavía con `balc`.

## Recursos y ciclo de vida

`SimEjecucion` es opaco y se reserva una vez. Posee la imagen cargada por S1,
el simulador y el recolector M1. La dirección del simulador permanece estable
mientras M1 lo referencia. No se copian superficialmente sesiones ni se
reubican con `realloc`. No hay estado global mutable en la biblioteca.

La imagen original y la DMEM mutable de S6 son las dos memorias requeridas
por sus contratos; no se mantiene una tercera copia persistente de DMEM.
El llamador puede solicitar una copia explícita para validación.

`sim_ejecucion_crear` requiere salida inicialmente NULL. Valida argumentos,
carga MEM/BALB mediante S1, inicializa S6, configura FINITA/FISICA e inicia
M1. Publica la sesión únicamente cuando todos los pasos terminan bien.
Un error libera recursos parciales y conserva la salida del llamador.
El canal `FILE *diagnosticos` se toma prestado durante la creación: admite
NULL, recibe las causas de S1 y nunca se cierra ni se conserva.

`sim_ejecucion_liberar` libera primero M1, después el simulador y finalmente
la imagen. Deja NULL y permite repetición. Los alias del llamador dejan de
ser válidos al liberar; ninguna API C puede volver seguro usar un alias
colgante. Una sesión requiere sincronización externa si se comparte entre
hilos. Sesiones distintas poseen recursos independientes.

## Interfaz y estados

| Función | Responsabilidad |
|---|---|
| `sim_ejecucion_crear` | Construcción transaccional MEM/BALB y selección de búsqueda. |
| `sim_ejecucion_avanzar` | Un intento de flanco, seguido de observación si fue aceptado. |
| `sim_ejecucion_ejecutar` | Presupuesto adicional y pausa administrativa de S6. |
| `sim_ejecucion_estado` | Código de coordinación, estado S6, estado M1 y banderas pendiente/fatal. |
| `sim_ejecucion_reintentar` | Reobservar un flanco pendiente sin ejecutar la CPU. |
| `sim_ejecucion_reiniciar` | Reset simple o restauración completa y nueva época. |
| `sim_ejecucion_resultados` | Snapshot M1 por valor, independiente de la sesión. |
| `sim_ejecucion_trap` | Consulta coherente del historial M1 por índice. |
| `sim_ejecucion_exportar` | Delegación transaccional a los formatos M1 existentes. |
| `sim_ejecucion_memoria` | Copia de los 65536 bytes de DMEM pública. |
| `sim_ejecucion_liberar` | Destrucción de recursos propios. |

El código de coordinación OK no implica TERMINADO: consultar `estado.pipeline`.
ACTIVO permite continuar, LIMITE indica pausa reanudable y TERMINADO es
estable hasta reset. Los errores administrativos del simulador devuelven
PIPELINE; sus métricas válidas siguen consultables porque el intento
rechazado no introdujo un flanco. Los errores de creación distinguen
ARGUMENTO, CARGA, MEMORIA, PIPELINE y METRICAS.

## Flancos, presupuestos y errores

Antes de avanzar se conserva `sim.ciclos`. Después de `sim_avanzar_ciclo`,
solo su cambio permite llamar a `sim_metricas_observar`. El último flanco
que devuelve TERMINADO también se observa. Un intento rechazado o una
consulta no suma eventos. No se usa el ejecutor S6 para avanzar varios ciclos
antes de observar.

`sim_ejecucion_ejecutar(s, n)` permite como máximo n intentos adicionales;
se detiene antes si termina o aparece un error. Al agotar el presupuesto
utiliza exclusivamente `sim_pipeline_ejecutar(sim, 0)` para publicar LIMITE.
Con cero no ejecuta instrucciones ni cambia registros, memoria o contadores.
FINITA drena el pipeline; FISICA conserva búsqueda y aliasing de S6, por lo
que el cliente siempre proporciona un presupuesto explícito. La ejecución
10+20+30+40 conserva el mismo estado que 100 ciclos continuos o 100 llamadas
con presupuesto 1 cuando se alcanzan los mismos flancos.

No se calcula `ciclos + presupuesto`: UINT64_MAX es una entrada válida sin
desbordamiento de esa suma. No constituye una promesa de ejecución breve:
un programa físico con ese presupuesto puede consumir tiempo impracticable.
El módulo rechaza antes del siguiente flanco un contador de ciclos que ya
esté en UINT64_MAX, informa METRICAS/DESBORDAMIENTO y marca fatal. Las demás
comprobaciones de desbordamiento se delegan a M1. Alcanzar 2^64 ciclos no se
probó por ejecución; esa guarda se revisó estáticamente. Los presupuestos
máximos sí se prueban con un programa finito que termina en cuatro ciclos.

Si observar devuelve MEMORIA, el flanco ya ocurrió: la sesión marca PENDIENTE
con `estado.metricas == SIM_METRICAS_MEMORIA`. Quedan inhibidos avanzar,
ejecutar y reset. No se revierten CPU ni memoria. `reintentar` vuelve a
observar ese mismo flanco; si vuelve a fallar mantiene el bloqueo. Después
de un reintento correcto puede continuar. Reintentar sin evento pendiente
es inocuo. Otra inconsistencia M1 marca fatal: no se permite seguir ni
publicar métricas de una época incompleta; se admite reset explícito o liberar.

No se modifica ningún miembro interno de M1. Un error de exportación se
comunica por su retorno y no vuelve pendiente un flanco ya observado.

## Reset y épocas

El reset simple aplica `sim_pipeline_reiniciar`, conserva la DMEM resultante
e inicia M1 sobre la misma instancia. S6 puede publicar stores pendientes en
EX durante ese reset. No se cuentan como ciclos o accesos de la nueva época.

Con `restaurar=true`, primero ocurre el reset, después
`sim_pipeline_restaurar_memoria` y finalmente el inicio de M1. Así se eliminan
los efectos residuales de DMEM para repetir la prueba desde la imagen.
Se conserva el modo de búsqueda configurado. Cada reset reemplaza historial
y acumulados y aumenta la época de M1. Copiar/exportar resultados previos
antes de reiniciar; el snapshot por valor sigue siendo independiente.
Una observación pendiente debe resolverse antes de reset para no ocultar
un flanco aceptado. Liberar sí puede descartar una sesión pendiente.

## Reportes y datos públicos

Se reutilizan TEXTO y JSON versión 1 sin otro esquema, exportador o contador.
IPC, instrucciones retiradas, NOP, anulaciones e historial conservan exactamente
el contrato de [M1](metricas-simulador.md). Liberar textos con
`sim_metricas_liberar_reporte` y comparar snapshots mediante
`sim_metricas_comparar`; esta comparación no certifica equivalencia funcional.

La API ofrece PC, GPR, AUTH/INIT y sondas públicas mediante el resumen M1, y
DMEM por copia de lectura. No ofrece un getter de `SimPipeline`, contraseñas,
claves ni estado privado de la bóveda. Los reportes no incorporan DMEM cruda.
Durante PENDIENTE puede consultarse la DMEM arquitectónica real, pero no se
publican resultados, reportes ni historial como si la observación fuera completa.

## Herramienta y ejemplos reproducibles

Compilar desde la raíz de Compiladores sin modificar su Makefile:

```bash
m2dir=$(mktemp -d /tmp/ce1108-m2-ejemplo.XXXXXX)
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  tools/sim_vliw_metricas.c \
  src/sim_{ejecucion,metricas,pipeline,alu,lsu,bru,crypto,decode,cargador}.c \
  -o "$m2dir/sim_vliw_metricas"
printf '%032d\n' 0 > "$m2dir/nop.mem"
"$m2dir/sim_vliw_metricas" mem "$m2dir/nop.mem" finita 100 json
"$m2dir/sim_vliw_metricas" mem "$m2dir/nop.mem" fisica 10 texto
# El segundo comando devuelve 2: presupuesto agotado con reporte válido.
```

Uso: `sim_vliw_metricas mem|balb archivo finita|fisica ciclos texto|json`.
La salida estándar contiene solo el reporte; los diagnósticos van a stderr.
Códigos: 0 TERMINADO; 2 LIMITE; 1 error de carga/ejecución/reporte/salida;
64 argumentos inválidos. Se rechazan negativos, signos, espacios,
desbordamientos y opciones desconocidas. No se escriben archivos por defecto.
La herramienta destruye su sesión al salir; la reanudación está en la API.

Uso mínimo desde otro programa C:

```c
SimEjecucion *s = NULL;
if (sim_ejecucion_crear(ruta, SIM_EJECUCION_BALB,
        SIM_BUSQUEDA_FINITA, &s, stderr) == SIM_EJECUCION_OK) {
    SimEjecucionCodigo c = sim_ejecucion_ejecutar(s, 20);
    if (c == SIM_EJECUCION_PENDIENTE)
        c = sim_ejecucion_reintentar(s); /* Resolver antes de continuar. */
    if (c == SIM_EJECUCION_OK) {
        SimMetricasResumen r;
        if (sim_ejecucion_resultados(s, &r) == SIM_METRICAS_OK) {
            /* r es propio; estado permite decidir si reanudar o terminar. */
        }
    }
}
sim_ejecucion_liberar(&s);
```

## Verificación y trazabilidad

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_sim_ejecucion.py
CC='cc -g -fno-omit-frame-pointer -fno-pie -no-pie -fsanitize=address,undefined' \
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/test_sim_ejecucion.py
```

`ARQUITECTURA_ROOT` permite seleccionar una copia de Arquitectura; por defecto
se consulta el repositorio hermano. `M2_EVIDENCIA` guarda comandos/códigos y
resultados fuera del repositorio. `M2_ARTEFACTOS` guarda fixtures/reportes/DMEM;
`M1_ARTEFACTOS` puede señalar la misma carpeta para conservar las trazas RTL.
Las pruebas reutilizan fixtures/oráculos aprobados como dependencias de prueba;
la biblioteca y la herramienta no dependen de ellos.

| Requisito / invariante | Función | Prueba ejecutable |
|---|---|---|
| Creación transaccional y recursos | crear/liberar | test_01, test_14; calloc 1 y 2 y destrucción repetida. |
| A: un observador por flanco | avanzar/reintentar | run: comparación tras cada flanco y JSON contra M1 directo; fallos en traps 1 y 5. |
| B: rechazos sin ciclos | avanzar/ejecutar | test_11 y test_15: PC finito inválido; terminación y presupuesto cero en test_01. |
| C: fragmentación equivalente | ejecutar | test_12: 100, 10+20+30+40 y 100×1, finito/físico, JSON y DMEM completos. |
| D/J: efectos y temporización originales | avanzar | Diferenciales de todas las ejecuciones run y test_05–08/10 contra RTL. |
| E: épocas y reset | reiniciar | test_01: reset simple, restauración y store en EX contra S6 directo. |
| F: anulados no retirados | delegación M1 | test_06/07/11: saltos, excepción con cuatro slots y error administrativo. |
| G: NOP no útil | delegación M1 | test_02 y test_12: imagen NOP y huecos físicos. |
| H: IPC | resultados/exportar | test_03/09: RAW 12/7 ciclos, tres instrucciones, IPC 0.25/0.428571428. |
| I: independencia | sesiones propias | test_01: dos programas, memoria, avances y reset aislados. |
| Reportes y errores de salida | exportar/herramienta | test_01/13–15: formatos, secreto ausente, /dev/full y realloc. |
| ISA y crypto | S6/M1 sin cambios | test_07/08/10 y regresiones S1–S8-C/M1. |

La suite reutiliza los diez métodos de programas/RTL de M1 con el candidato
M2; reemplaza su prueba C por la del ciclo de vida M2 y agrega cuatro métodos
de coordinación/adversariales. M1 completo se ejecuta también como regresión. Sus oráculos incluyen constantes conocidas,
S6 sin instrumentación, M1 directo, CB/S7/S8-A y RTL original sin modificaciones.
Los eventos EX/WB/flush, GPR y memoria observables se contrastan con las sondas
S6/RTL existentes. Las instrucciones útiles y clasificaciones se derivan de
S2 al retirar el PC observado; no son contadores RTL expuestos directamente.
La comparación por flanco M2/S6 abarca todos los campos públicos pedidos y la
DMEM completa; no inspecciona bóveda privada a través de una API nueva.

La referencia RAW reproduce 12 ciclos/3 instrucciones/IPC 0.250000000 en la
imagen conservadora y 7/3/0.428571428 en la calendarizada. GPR y los 65536
bytes DMEM coinciden; sus PC finales difieren según el layout. M2 no optimiza.

## Límites

No se implementan cachés, stalls de caché, IR, optimizaciones ni integración
con CLI/IDE. La referencia Arquitectura corresponde al RTL original utilizado
por las sondas S6; los resultados no certifican cambios futuros de Fabricio.
No se modifican ISA, latencias N+3, reset, arbitraje WB ni temporización.
Las guardas de errores M1 no recuperables y desbordamiento total se revisan
estáticamente; no se inventan estados internos para hacerlas alcanzables.
Las pruebas de fallos usan envolturas del enlazador GNU solo en el test.
ASan se ejecuta con fugas deshabilitadas como comprobación de accesos;
LeakSanitizer se evalúa separadamente y debe informarse según su resultado real.
La evidencia externa de esta intervención contiene línea base/final, reportes,
revisiones, códigos de salida y comparación de manifiestos protegidos.
