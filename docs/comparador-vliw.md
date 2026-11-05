# Comparador independiente VLIW (B1)

B1 ejecuta dos imágenes mediante M2 y verifica sus resultados públicos antes
de interpretar las diferencias de rendimiento calculadas por M1. No implementa
ISA, pipeline, calendarización, optimizaciones ni opciones -O del compilador.
No depende de IR, CLI balc o IDE y no modifica componentes aprobados.

## Contrato estricto público

Una comparación final solo se realiza si ambas ejecuciones alcanzan
`SIM_PIPE_TERMINADO`. Se cotejan los 32 GPR, los 65536 bytes de DMEM,
AUTH/INIT y la cantidad/secuencia de vectores de traps observados por M1.
La comparación de memoria cuenta bytes y localiza el primero diferente;
una huella coincidente no reemplaza esa operación.

No se comparan como resultados funcionales PC final, ciclos, bundles, NOPs,
IPC ni PC/ciclo de traps: dependen del layout y de su temporización. Estos
últimos campos se conservan como diagnósticos de cada imagen. No se omiten
registros temporales ni de enlace. El contrato puede rechazar una transformación
que cambie registros físicos aunque conserve resultados de más alto nivel:
la correspondencia de renombramiento requiere acuerdos futuros con Dylan y
Meibel. No se implementa todavía otra política.

El estado privado de la bóveda no es observable mediante M2 y queda fuera de
esta garantía. Igualdad de resultados públicos no certifica igualdad de claves,
contraseñas, comportamiento futuro de esas claves ni equivalencia semántica
universal. Tampoco se certifican futuras cachés o stalls de Arquitectura.

## Creación, propiedad y resultados

`SimComparadorOpciones` identifica referencia y candidato (ruta y formato
MEM/BALB), modo común FINITA/FISICA, presupuesto por programa y máximo total
de reintentos por programa (0..16). La CLI usa dos reintentos.

`sim_comparador_ejecutar` recibe una salida inicialmente NULL. Copia los
nombres, crea dos sesiones M2 y consulta sus estados/DMEM iniciales antes de
avanzar ninguna. Exige reset público esperado (PC/ciclos/GPR/contadores en
cero, ACTIVO y estado INIT) y DMEM idéntica byte a byte. No iguala entradas
silenciosamente: MEM no aporta datos, BALB sí puede aportarlos.

Un archivo BALB con datos distintos de un MEM impide comparar la ejecución;
no se agrega una API de inyección ni se cambia el cargador. Cada sesión tiene
su imagen y almacenamiento M2 propios. El estado privado inicial se confía
al contrato de reset de S6, sin inspeccionarlo.

| Resultado arquitectónico | Condición |
|---|---|
| EQUIVALENTE | Ambas terminan y todos los criterios públicos estrictos coinciden. |
| DIFERENTE | Ambas terminan pero difiere algún criterio funcional. |
| INCONCLUSO | Entradas incompatibles o alguna ejecución sigue en LIMITE. |
| ERROR | Falla la carga, ejecución, observación o consulta necesaria. |

Los errores de argumentos o reservas propias B1 devuelven ARGUMENTO/MEMORIA
sin publicar un objeto parcial. Una falla M2 entrega un objeto íntegro de
clasificación ERROR, con lado, causa y banderas de disponibilidad; no se
presenta un estado inicial o una observación pendiente como resultado final.
El código OK de la llamada significa que existe un objeto de comparación,
no que los programas sean equivalentes. La clasificación está en el resumen.

La sesión M2 puede quedar pendiente después de aceptar un flanco. B1 invoca
únicamente `sim_ejecucion_reintentar`: no repite instrucciones ni retrocede
CPU. Si recupera, consulta los ciclos aceptados y consume solo el presupuesto
restante. El límite total de reintentos impide un bucle por falta persistente
de memoria; al agotarlo se conserva ERROR/OBSERVACION. No se reconstruyen
contadores ni se implementa otro mecanismo de observación.

Los dos buffers DMEM son transitorios y se reutilizan para comparar entradas
y resultados. El objeto final solo conserva rutas propias, snapshots por
valor e historiales propios; las sesiones y buffers se liberan antes de
publicarlo. `sim_comparador_resumen` copia el resumen al cliente y
`sim_comparador_trap` copia un evento por lado/índice. Las copias sobreviven
a la liberación del objeto. El historial informa ciclo/PC/vector, aunque solo
cantidad y vectores participan en equivalencia.

Liberar el objeto mediante `sim_comparador_liberar(&c)`: deja NULL y admite
repetición por ese puntero. Otros alias dejan de ser válidos. Liberar cadenas
con `sim_comparador_liberar_reporte(&texto)`, incluso si ya se destruyó el
objeto. La API no devuelve referencias a memoria mutable ni a secretos.

## Rendimiento y representación

Se llama a `sim_metricas_comparar` sobre los resúmenes válidos; la convención
es candidato menos referencia para ciclos, bundles, instrucciones e IPC.
No se duplican esos cálculos. En INCONCLUSO por presupuesto pueden existir
métricas parciales, claramente separadas de la certificación final. Ante
inicialización incompatible o error sin dos resúmenes finales disponibles
no se ofrece una diferencia de rendimiento.

Las diferencias enteras conservan magnitud uint64_t y signo separado, sin
forzarlas a int64_t. Una referencia con cero ciclos conserva el porcentaje
no definido de M1 y se representa como null. IPC procede del resumen M1 y
los cocientes no usan tiempo de pared. Los números JSON conservan la
precisión double (17 dígitos significativos); TEXTO usa 12 decimales.
La proyección JSON M1 original puede truncar a nueve decimales: B1 no modifica
ese exportador ni recalcula su IPC.

## Reportes y esquema JSON v1

`sim_comparador_exportar` recibe TEXTO/JSON y una salida inicialmente NULL.
Construye el documento privadamente y solo publica la cadena al completar.
Errores de reserva/generación liberan el buffer y conservan NULL. Un objeto
DIFERENTE, INCONCLUSO o ERROR también puede exportarse válidamente.

El locale numérico C se aplica por hilo mediante newlocale/uselocale de
POSIX; se restaura el anterior y no se modifica el locale global del cliente.
La implementación está dirigida al entorno POSIX del proyecto y compila con
C11 estricto sin bibliotecas externas adicionales.

| Campo JSON | Significado |
|---|---|
| version | 1. |
| contrato | estricto-publico. |
| resultado_comparacion | EQUIVALENTE/DIFERENTE/INCONCLUSO/ERROR. |
| motivo | 0 sin impedimento; 1 iniciales; 2 no terminado; 3 carga; 4 ejecución; 5 observación; 6 consulta. |
| referencia / candidato | Ruta, creada/ejecutada, estado S6 o null, proyección de métricas disponible o null e historial. |
| iniciales_compatibles | Si se verificó reset público y toda DMEM inicial. |
| bytes_iniciales_distintos / primer_byte_inicial | Diagnóstico de entradas incompatibles. |
| diferencias_arquitectonicas | Bandera comparadas, cantidad/primer GPR y byte distintos con valores referencia/candidato, diferencia crypto/traps/vector. |
| error | Lado (-1 sin lado específico, 0 referencia, 1 candidato), código M2 y código M1. |
| diferencias_metricas | Resultado M1 o null si no corresponde. |
| limites | Alcance público, GPR estrictos y PC/ciclo de traps diagnósticos. |

Los índices/valores de primera diferencia solo tienen significado si su
contador o bandera indica una diferencia; de lo contrario quedan en cero.
`comparadas=false` nunca certifica igualdad. Estados S6 mantienen sus valores
públicos: ACTIVO=0, TERMINADO=1, LIMITE=2; códigos restantes según su cabecera.
Las diferencias de enteros son `{negativa: boolean, magnitud: uint64}`; IPC
es numérico, porcentaje_definido es booleano y porcentaje_ciclos es número
o null. La estructura no duplica el JSON completo de M1 ni serializa la bóveda.

Strings escapan comillas, barras y controles. Se conservan nombres UTF-8
válidos; bytes inválidos de nombres de archivo se representan como escapes
individuales \u00xx para garantizar JSON válido. El reporte de texto muestra
rutas, estados, diagnóstico funcional y una tabla referencia/candidato/delta.

## Construcción y uso

Desde la raíz de Compiladores, crear solo artefactos temporales:

```bash
b1dir=$(mktemp -d /tmp/ce1108-b1-ejemplo.XXXXXX)
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  tools/comparar_vliw.c \
  src/sim_{comparador,ejecucion,metricas,pipeline,alu,lsu,bru,crypto,decode,cargador}.c \
  -o "$b1dir/comparar_vliw"
printf '%032d\n' 0 > "$b1dir/nop.mem"
"$b1dir/comparar_vliw" mem "$b1dir/nop.mem" mem "$b1dir/nop.mem" finita 100 json
```

Uso: `comparar_vliw mem|balb referencia mem|balb candidato finita|fisica ciclos texto|json`.
Presupuesto obligatorio, decimal sin signos ni espacios, rango uint64_t.
No se acepta un número inválido como cero. No se escriben archivos por defecto.
stdout contiene el reporte B1 y stderr los diagnósticos.

| Salida | Significado |
|---|---|
| 0 | EQUIVALENTE bajo contrato público estricto. |
| 2 | DIFERENTE arquitectónicamente. |
| 3 | INCONCLUSO, sin certificación final. |
| 1 | Error de construcción, ejecución, reporte o entrega de salida. |
| 64 | Uso incorrecto. |

FINITA es el modo de comparación final. FISICA sigue acotada, pero S6 no
termina al final de la imagen: normalmente produce INCONCLUSO. Ambos
programas tienen el mismo presupuesto máximo, no el mismo número esperado
de ciclos. Los códigos de la herramienta M2 permanecen intactos.

## Pruebas y trazabilidad

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_sim_comparador.py
CC='cc -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie -no-pie' \
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/test_sim_comparador.py
```

ARQUITECTURA_ROOT selecciona una copia RTL; por defecto se consulta el
repositorio hermano. B1_EVIDENCIA registra comandos y resultados;
B1_ARTEFACTOS conserva JSON/TEXTO. M2_EVIDENCIA y M2_ARTEFACTOS conservan la
evidencia de sus oráculos heredados, M1_ARTEFACTOS puede señalar esa misma
carpeta para guardar las trazas RTL. Todo debe apuntar fuera del repositorio.
La compilación de estas pruebas no necesita modificar el Makefile.

La suite conserva los quince métodos M2 (incluidos diez programas/166 ciclos
RTL) y agrega doce métodos B1. Prueba 32 GPR sin exclusiones (x0 por su
invariancia y los otros 31 por diferencias), DMEM incluso en el byte 65535,
NOP/layout/saltos, iniciales incompatibles, ambas/una ejecución limitada,
traps con PC/ciclo diferentes, crypto público, errores y reportes. El fixture
RAW reutiliza S7/S8-A/CB: 12 y 7 ciclos, tres instrucciones, diferencia -5,
IPC 0.25 y 3/7; GPR y toda DMEM coinciden. B1 no hace esa transformación.

| Invariante | Verificación |
|---|---|
| A/B: ejecución M2 y sesiones distintas | Inspección de llamadas y test_20; todas las comparaciones contra M2 run. |
| C: iniciales iguales | test_26: MEM/BALB con byte diferente impide avanzar. |
| D/J: terminar antes de certificar | test_27 y test_20: cero, ambos LIMITE, uno TERMINADO, FISICA. |
| E: toda DMEM | test_24: bytes 4, 65532 y 65535 contra comparación independiente. |
| F: GPR estrictos | test_23: diferencias en los 31 registros escribibles; test_31 x0 constante. |
| G: PC no universal | test_21/25/28: layouts, saltos y traps diferentes. |
| H/I: M1 y rendimiento separado | test_22: RAW -5; test_23/24: tiempos parecidos con resultados distintos; test_31: IPC distinto y resultado igual. |
| K: no secretos | Lista blanca del reporte; JSON verificado sin campos password/keys. |
| L: sin efectos de comparación | Oráculo M2 por flanco y reportes; snapshots no cambian al exportar en test_20. |
| M: publicación íntegra | test_20: cada reserva de reporte, vsnprintf y locale fallidos; test_30/31 errores. |
| N: limpieza | test_20: cada malloc/calloc/realloc registrado, reintentos persistentes y liberación repetida; sanitizadores. |

Los resultados B1 se cotejan con M2 run, que compara cada flanco con S6 y
la DMEM completa. Los casos heredados vuelven a ejecutar las sondas originales
S6/RTL. Las instrucciones retiradas por operación se derivan de sus PC WB con
S2: no se atribuyen a un contador RTL inexistente. B1 compara estados finales,
no implementa RTL ni altera ciclos simulados.

S6 solo genera el vector 0x3ff0. La rama que detecta vectores distintos se
revisa estáticamente: no puede activarse con dos programas legales actuales.
Sí se ejecutan casos con distinto número de traps y con un vector igual en
PC/ciclos diferentes. Los límites de SIZE_MAX del historial se revisan sin
fabricar sesiones privadas. No se exponen getters de claves para probarlos.

Las inyecciones usan envolturas GNU del enlazador solo en el test, sin cambios
en M1/M2. Se prueban todas las reservas observadas de una comparación con
historial, errores de generación en las dos fases de vsnprintf y la falta
persistente de memoria con cero/dos reintentos. LSan se evalúa aparte; si
ptrace lo bloquea se registra NO VERIFICADO, no se afirma comprobación de fugas.
La evidencia externa contiene línea base/final, manifiestos, diff e informe.
