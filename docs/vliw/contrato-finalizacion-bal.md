# Diagnóstico de finalización BAL (B4)

B4 identifica el salto final del arranque y observa su ejecución. No define una
instrucción HALT, altera el pipeline ni sustituye la terminación de S6/B1/B2.
El único resultado certificado es la correspondencia del código y los hechos
públicos de ejecución que aparecen en el reporte.

## Contratos inspeccionados y hallazgo estático

En HEAD `13ccaf508f470503a9c7eb6f92b4706fa8fb2c87`,
`arranque_lineal` en `src/enlace_funciones.c` construye constantes GP/SP,
inicializa globales, carga la dirección de main en x13 y emite:

```asm
syer x1, x13, 0
__arranque_fin:
sye x0, __arranque_fin
```

`src/resolucion_saltos.c` ubica cada instrucción con dos NOP posteriores,
resuelve destinos y relajaciones y convierte las etiquetas a comentarios.
El `.asm` real generado contiene, por ejemplo:

```asm
nop | nop | syer x1,x13,0 | nop
nop
nop
# __arranque_fin:
nop | nop | sye x0,0 | nop  # -> __arranque_fin
nop
nop
# __fn_main:
```

Por eso una búsqueda de etiquetas activas perdería el marcador. Un comentario
no constituye un símbolo del ensamblador ni autentica procedencia. B4 registra
si se trata de etiqueta real o anotación del resolvedor, exige que sea única
y conserva hashes y la compilación que produjo ese archivo.

`cb_ensamblar` vuelve a ensamblar el ASM completo con el componente aprobado.
Sus `CbBundle.linea` permiten encontrar el primer bundle en/después de la línea
del marcador. No se usan números de línea como direcciones. Se comprueban todos
los slots de todos los bundles contra IMEM cargada por S1 desde BALB. Se valida
cantidad de bundles y coincidencia completa del código, no solo un opcode.

En el bundle señalado, `sim_decode_palabra` debe reconocer en BRU `sye`, rd=x0,
inmediato=0; ALU/LSU/CRIPTO deben ser NOP. No se escribe otro ensamblador ni
se codifican campos ISA dentro de B4. Las verificaciones de formatos BALB/MEM
de B3 se reutilizan; la carga real sigue correspondiendo a S1.

El marcador ausente o una instrucción incompatible producen FIN_NO_DEMOSTRADO
si el análisis dinámico fue posible. Etiqueta duplicada, ASM inválido,
discordancia ASM/BALB, carga o infraestructura fallidas producen ERROR.
El análisis independiente de artefactos puede invocar la sonda directamente;
la CLI Python trabaja con fuentes BAL y compilaciones propias aisladas.

## Diseño de las herramientas

`tools/vliw/analizar_fin_bal.py` coordina copias temporales de BAL y sus módulos
hermanos, `balc -s`, validaciones B3 y la sonda externa. Reutiliza aislamiento,
transporte de procesos y JSON estricto de B3/B2 sin modificar esos módulos.
No integra frontend, IR, scheduler, optimizaciones ni IDE.

`tools/vliw/sonda_fin_bal.c` usa CB/S1 para correspondencia y S6/M1 para observar
cada flanco. Instancia S6 en almacenamiento propio estable; no consulta bóveda
privada. Una segunda sesión pública M2 ejecuta la misma imagen al presupuesto
completo y permite contrastar los resultados.

Construir en una **copia aislada** del proyecto:

```bash
# Resuelve el codificador del árbol plano o de develop sin copiarlo.
BINARIO=$(PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tools/vliw python3 -c 'from pathlib import Path; from rutas_vliw import fuente_compartida; print(fuente_compartida(Path.cwd(), "src/codificacion_binaria.c"))')

make -B balc
b4dir=$(mktemp -d /tmp/ce1108-b4-ejemplo.XXXXXX)
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  tools/vliw/sonda_fin_bal.c "$BINARIO" \
  src/Simulador_VLIW/sim_{ejecucion,metricas,pipeline,alu,lsu,bru,crypto,decode,cargador}.c \
  -o "$b4dir/sonda_fin_bal"
python3 tools/vliw/analizar_fin_bal.py \
  --balc /ruta/copia/balc --sonda "$b4dir/sonda_fin_bal" \
  --fuente /ruta/proyecto/examples/factorial.bal \
  --fuente /ruta/proyecto/examples/whale.bal \
  --fuente /ruta/proyecto/examples/ejercicio3_puntajes.bal \
  --presupuesto 10000 --salida "$b4dir/diagnostico"
```

No se modifica Makefile. Las herramientas se construyen una vez, no por caso.
Salida obligatoria y nueva, con padre existente fuera de carpetas fuente.
No se sobrescriben reportes anteriores. Cada proceso usa cwd temporal propio,
argumentos separados, stdin cerrado y timeout. El ensamblador auxiliar se
escribe únicamente junto a la copia BAL. Artefactos originales de la compilación
se preservan y sus hashes se comprueban después de la sonda; no se parchean.

Sonda directa: `sonda_fin_bal programa.asm programa.bin linea_sentinela linea_main presupuesto`.
Las líneas son 1-based y cero indica marcador ausente. Estos argumentos deben
proceder del localizador de etiquetas/anotaciones; la sonda no autentica por sí
misma que una línea suministrada arbitrariamente representa el marcador BAL.
No recibe ni ejecuta comandos desde BAL o un manifiesto.

## Observación por flanco y control en EX

La sonda compara `sim.ciclos` antes y después de `sim_avanzar_ciclo`. Solo si
cambia hay un flanco aceptado y se observa M1 antes de avanzar otro. El último
flanco que vacía el pipeline también se observa; un intento rechazado no repite
la última traza ni agrega eventos.

M1 MEMORIA permite dos reintentos del mismo flanco, sin ejecutar nuevamente
la CPU. Si persiste o devuelve otra falla, el análisis se marca ERROR y no
publica métricas como si la observación fuera completa. Es un uso diagnóstico
de APIs públicas, no una nueva biblioteca de sesiones de producción.

`SimCiclo` describe ocupación y señales anteriores al flanco. `ex.pc` pertenece
al bundle que realmente ejecutó BRU en EX; `bru_ex_valido`, `ex_bru.tomado`,
`ex_bru.destino` y `redirect_pc` identifican redirecciones, en vez de inferirlas
por repetición de PC de IF. `excepcion` separa el flush criptográfico del salto.
El banco, ocupación pendiente y snapshots M1 capturados son posteriores al flanco.

WB anterior puede escribir en el mismo flanco que redirige EX. El salto queda
en EX/WB, aunque tenga destino x0 y su enlace se descarte después. Se registran
las solicitudes WB públicas ALU/LSU/BRU/CRIPTO y los registros posteriores.
M1 proporciona los contadores e IPC; no se duplican ni se recalculan.

La sonda mantiene el presupuesto completo y registra, cuando caben en él,
los dos flancos posteriores al primer salto final observado. Esto proporciona
evidencia temporal, **no** establece que dos flancos basten para certificar
un estado final o que carezcan de efectos. Un presupuesto que termina en el
propio salto no inventa flancos posteriores. No se selecciona un corte de
métricas para mejorar resultados.

## Hechos, candidatos y condiciones no demostradas

| Identificador | Significado |
|---|---|
| PATRON_ESTATICO_VALIDO | Marcador único, mapeo CB, código completo coincidente y sye x0,0 aislado en BRU. No implica ejecución. |
| REDIRECCION_A_SENTINELA_OBSERVADA | Una BRU tomada desde otro PC redirige exactamente a la sentinela. No certifica retorno. |
| RETORNO_A_CONTINUACION_CANDIDATO | Tras observar la llamada inicial, aparece syer x0,x1,0 hacia su enlace; es compatible con ret, no identifica la llamada dinámica. |
| BUCLE_FINAL_OBSERVADO | Se ejecuta en EX el sye verificado de la sentinela hacia sí mismo. |
| FIN_NO_DEMOSTRADO | No se observó el salto final con el contrato estático requerido. |
| ERROR | Análisis incompleto, error S6/M1/IO/protocolo o discrepancia con M2. |

`retorno_main_certificado` siempre permanece false. Esto no niega el retorno
normal de los ejemplos: expresa el alcance de la evidencia pública disponible.
Un salto directo inesperado a la sentinela puede producir
REDIRECCION_A_SENTINELA_OBSERVADA y BUCLE_FINAL_OBSERVADO sin retorno candidato.
Incluso una instrucción con forma de ret puede redirigir usando x1 sin pertenecer
al frame esperado. No hay una pila alternativa ni seguimiento fabricado de calls.

La llamada inicial se identifica mediante el bundle no NOP anterior al marcador:
SYER rd=x1, rs1=x13, inmediato 0, otros slots NOP, y únicamente NOP entre esa llamada
y la sentinela. Dinámicamente debe saltar al bundle marcado como __fn_main.
La BRU original calcula su enlace como PC+16; ese enlace apunta a los NOP de
continuación, no necesariamente a la sentinela. La sonda conserva ese valor.

Para los ejemplos reales:

| Programa | Llamada PC / ciclo | Enlace | Sentinela | Ret candidato PC / ciclo | Primer bucle ciclo |
|---|---|---:|---:|---|---:|
| factorial | 720 / 48 | 736 | 768 | 2976 / 3402 | 3407 |
| whale | 720 / 48 | 736 | 768 | 2256 / 246 | 251 |
| puntajes | 1008 / 66 | 1024 | 1056 | 12768 / 2747 | 2752 |

Los tres permanecen en LIMITE a 10000 ciclos. Ninguno se convierte
artificialmente en otro estado. Los snapshots del salto final muestran
pendientes IF/ID=0, ID/EX=0, EX/WB=1; se conservan sus dos flancos siguientes.
Un marcador comentado agregado a mano no puede autenticar que el programa
provino del compilador legítimo; la CLI registra identidad del compilador y
fuentes, pero no es un mecanismo de firma de artefactos.

## Comparación independiente con M2

M2 usa el mismo BALB, FINITA y el presupuesto completo. Se compara el JSON M1
íntegro (incluidos ciclos, GPR, estado, crypto público y traps), el estado S6 y
los 65536 bytes DMEM mediante copia pública M2 y memoria pública S6. No se
consideran suficientes hashes para igualdad. El reporte distingue disponibilidad
si no pudo completarse la segunda ejecución. Las tres igualdades deben ser
verdaderas para dar por correcto el diagnóstico; una discrepancia produce ERROR.

No se exige igualdad del snapshot temprano del retorno con el estado a 10000:
son horizontes diferentes. Tampoco se afirma equivalencia semántica o contraste
RTL directo de cada programa BAL por obtener igualdad S6/M2. Las regresiones
previas reutilizan los testbenches RTL que ya existían, sin cambios.

## Reportes y límites de recursos

Se publican `reporte.json` versión 1, `anfitrion.json`, BAL copiados, ASM/BALB/MEM
generados, stdout/stderr exactos de compilación y sonda. El reporte incluye
identidades SHA-256, marcador/naturaleza/línea, dirección verificada, eventos,
retorno candidato, bucle, snapshots y flancos posteriores, estado, M1,
comparación M2, hechos, inferencias condicionadas y limitaciones.

Tiempo de pared y comandos con nombres temporales van en anfitrion.json.
El reporte estable no incluye timestamps ni duraciones. Hay determinismo para
las mismas entradas, rutas originales, herramientas y presupuesto; no se promete
identidad entre ejecutables o máquinas diferentes. Se conservan los primeros 128
eventos de control y una cuenta del total, con truncamiento explícito. Las
observaciones destacadas se capturan separadamente, aunque el prefijo esté lleno.

Límites: 1–32 fuentes, presupuesto 0–1000000, timeout finito(0,3600]s/proceso,
ASM 2 MiB sin NUL, fuentes/artefactos con límites B3, stdout 1 MiB/stderr 64 KiB vía B2.
La selección de ejecutables requiere confianza; no es sandbox de código ajeno.
No hay un límite global de tiempo del lote. Publicación por rename del directorio
propio a destino nuevo; se sincronizan los JSON. No se garantiza durabilidad de
todos los artefactos ante caída ni exclusión de carreras concurrentes de destinos.

Retornos Python:0 diagnóstico completado sin ERROR,1 algún ERROR o infraestructura,
64 argumentos inválidos. **0 no certifica finalización**: FIN_NO_DEMOSTRADO también
es un diagnóstico válido. La sonda devuelve0 por reporte diagnóstico escrito,
1 cuando no puede producirlo y64 por uso incorrecto; revisar siempre su contenido.

## Pruebas y trazabilidad

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_analizar_fin_bal.py
CC='cc -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie -no-pie' \
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_analizar_fin_bal.py
```

La suite construye balc en copia externa, salvo B4_BALC explícito, y dos sondas
una vez: normal y adversarial GNU --wrap. B4_ARTEFACTOS conserva evidencia externa;
B4_EVIDENCIA guarda comandos/códigos/diagnósticos. Las envolturas de malloc/calloc/
realloc y observar están únicamente en un C temporal de tests; no alteran M1.
Python no está instrumentado por ASan. LSan se intenta por separado y se informa
NO VERIFICADO si falla por ptrace, sin declarar ausencia de fugas.

| Requisitos | Implementación | Métodos de prueba |
|---|---|---|
| Parámetros, aislamiento y limpieza | main/analizar/B3 | 01–02,20–23,32–33,35,37–40 |
| ASM/BALB/marcadores/código | localizar/CB/S1/S2 | 03–09,26–27,41 |
| Eventos BRU y no falsos retornos | sonda/clasificar | 10–13,24–25,30–31,34,36 |
| Excepciones y observación M1 | bucle por flanco | 14,16–18,29 |
| Carga y memoria | S1/recursos | 03,15,22 |
| Comparación íntegra M2 | exportación y memcmp | 17,19 |
| Temporalidad y efectos pendientes | destacar/evento | 28,42 |

Son 42 métodos, con subcasos de inyección de memoria y presupuestos, no programas
oficiales de optimización. Whale y cycle auxiliares son BAL temporales que siguen
en main durante el presupuesto; otro caso permanece dentro de una función.
La entrada inesperada a sentinela se ensambla mediante balc -x, no con ISA Python.
No se confunde ese fixture con el arranque real del compilador.

Se ejecutan separadamente S1–S8-C/M1/M2/B1/B2/B3 y `make -B -k test` en copia
externa, con manifiestos inicial/final de ambos repositorios. No se modifica
ninguna prueba anterior ni el repositorio de Arquitectura.

## Requisitos para un contrato futuro

B4 no modifica B1/B2 ni convierte INCONCLUSO en EQUIVALENTE. Antes de definir
una finalización BAL sería necesario acordar:

- Proveniencia del arranque y un vínculo inequívoco con la invocación inicial
  de main, incluidos returns anidados o redirecciones inesperadas.
- Qué efectos WB/memoria/crypto deben haberse publicado y qué estado observable
  se comparará, sin descartar efectos al tomar el salto.
- Qué excepciones invalidan la certificación y cómo se representarán errores.
- Cómo comparar layouts/registros físicos distintos y qué métricas delimitan
  una ventana válida, excluyendo el bucle de permanencia con una regla común.

Las APIs públicas actuales no ofrecen identidad dinámica de frames ni un evento
semántico main-finalizado. No basta un PC repetido, una forma ret o igualdad de
memoria para introducir ese contrato. Requiere acuerdo y autorización aparte.
