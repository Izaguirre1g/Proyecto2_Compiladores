# S5: BRU, redirección y flush del pipeline VLIW

S5 agrega las seis operaciones BRU al slot 2. Conserva ALU en slot 0, LSU en
slot 1 y NOP en slot 3. El pipeline sigue siendo IF–ID–EX–WB: no incorpora MEM,
forwarding, scoreboard, stalls automáticos ni calendarización dinámica.

## Relación con S1–S4 y organización

S1 conserva la carga MEM/BALB y la imagen prestada e inmutable; S2 conserva la
decodificación y las restricciones estáticas de la ISA. ALU y LSU no cambian.
`sim_bru` evalúa operandos y construye una decisión, sin modificar procesador,
registros o memoria. `sim_pipeline` interpreta esa decisión junto a los efectos
ALU/LSU del mismo bundle y al WB del bundle anterior.

Las funciones públicas existentes mantienen sus firmas. Se amplían ID/EX y
EX/WB con `SimBruId` y `SimEnlacePendiente`. El PC de EX sigue siendo el que
capturó el bundle en IF/ID y después ID/EX; nunca se reconstruye desde el índice
IMEM ni desde el PC global adelantado de IF. La DMEM privada de S4 permanece
al final del estado: se copia solo el prefijo de control por ciclo, no 64 KiB.

## API de la unidad

```c
bool sim_bru_ejecutar(SimOperacion operacion, uint32_t pc,
                     uint32_t a, uint32_t b, int32_t offset,
                     SimResultadoBru *resultado);
```

Recibe operación e inmediato ya interpretados por S2, el PC original de EX y
los valores que ID capturó. El resultado incluye `tomado`, `destino`, `enlace`
y `valor_enlace`. Un error devuelve false sin cambiar la salida: puntero nulo,
operación ajena a BRU u offset fuera del rango de su formato.

La función no conoce el registro destino; el pipeline ya lo conserva. Una
solicitud de enlace hacia x0 se prepara igualmente, pero WB descarta su escritura.
Sin salto tomado, el bus destino conserva PC+16 como valor por defecto del RTL;
ese valor no redirecciona si `tomado` es falso.

## Semántica y aritmética

| Operación | Condición/redirección | Destino | Enlace |
|---|---|---|---|
| igualsi | a == b | PC_EX + offset11 | No |
| igualno | a != b | PC_EX + offset11 | No |
| menora | a < b con signo | PC_EX + offset11 | No |
| mayoroigual | a >= b con signo | PC_EX + offset11 | No |
| sye | Siempre | PC_EX + offset16 | PC_EX + 16 |
| syer | Siempre | (a + offset11) & FFFFFFF0 | PC_EX + 16 |

Las sumas utilizan uint32_t y conversión explícita del offset signed, con
wraparound módulo 2³². Las comparaciones signed invierten el bit 31 y comparan
unsigned para mantener el orden de complemento a dos sin conversiones fuera
de rango a int32_t del host.

Condicionales y syer admiten offsets -1024..1023; sye -32768..32767. La función
BRU aislada no impone alineación estática: procesa el valor de bits que recibe,
como RTL. En instrucciones reales S2 exige la alineación de offsets directos;
por ejemplo, sye con offset 1 se rechaza por S2 antes de llegar a EX.

Solo syer alinea dinámicamente: base 0x43 + 0 produce destino 0x40; 0x43-4
produce 0x30. Sye y los condicionales no aplican esa máscara. `ret` no es un
opcode nuevo: el ensamblador existente lo expande a `syer x0,ra,0`.

## Capturas, flanco y enlace

ID captura los valores BRU del banco anterior, el offset, la operación y el
destino en ID/EX. ALU/LSU leen ese mismo estado anterior. Ni el resultado EX de
otra unidad ni el WB simultáneo se adelantan hacia ID.

En EX se evalúa la comparación o el jump usando esos valores capturados. La
señal tomada es combinacional antes del flanco. Sye/syer preparan un enlace
pendiente en EX/WB, paralelo al registro interno link_* de bru.sv. En el
siguiente flanco WB publica ese enlace. No se escribe durante ID o EX.

El enlace es PC_EX+16 aunque el destino sea distinto, el salto retroceda, el
registro sea x0 o la suma del enlace haga wraparound. Un flush no elimina el
enlace ni el resultado ALU/load del propio bundle que lo produjo.

## Orden de cálculo y prioridad sobre errores especulativos

El avance distingue estado anterior y siguiente. S5 cambia el orden de cálculo
para resolver EX antes de admitir IF/ID; esto no es forwarding ni publicación
anticipada. Todos los efectos permanecen preparados hasta validar el flanco.

1. EX calcula ALU, BRU y LSU del ID/EX anterior.
2. Si BRU redirecciona, IF/ID e ID/EX siguientes permanecen inválidos. No se
   consulta IMEM especulativa ni se decodifica/valida el bundle descartado de ID.
3. Si no hay redirect, IF valida/busca según la política y ID utiliza S2 para
   admitir las unidades del bundle anterior IF/ID.
4. WB anterior conserva sus efectos, incluidos los tres puertos de escritura.
5. Tras superar las validaciones se aplica la transacción LSU y se publica el
   prefijo de control, incluido el PC siguiente.

Una codificación reservada, un error de slot o una operación CRIPTO en ID
no detienen la simulación si serán anulados por ese mismo redirect. Tampoco
un PC especulativo inválido de IF puede impedir la redirección. Si el branch
no se toma, esas instrucciones sí se validan y conservan sus errores normales.

Los errores administrativos siguen congelando el flanco completo como S4:
ningún store preparado ni WB pendiente se publica al rechazarse el ciclo. No
se interpretan esos errores como excepciones de CPU.

## Flush, actualización del PC y penalización

Con salto tomado se vacían **solo los dos registros que recibirían IF e ID**:
IF/ID e ID/EX. EX/WB recibe el bundle del salto; el WB anterior también puede
completar. Por tanto, ALU, un store o un load del bundle BRU no se anulan por su
propio salto. Los bundles posteriores no llegan a EX ni generan efectos futuros.

El PC siguiente centraliza la decisión: target BRU si hay redirect; PC_IF+16
en otro caso. Reset domina ambos y restablece PC=0 mediante la API de reset.
El siguiente ciclo busca el destino, luego lo decodifica y después ejecuta:
quedan dos ciclos sin una nueva operación válida en EX. No se agrega una
penalización artificial mediante un contador de stalls.

Cuando existen dos bundles jóvenes válidos, ambos se descartan: el de ID y
el de IF. Si el modo finito ya drenaba, alguno puede ser una burbuja; siguen
invalidándose las mismas dos posiciones, sin inventar bundles descartados.

`SimCiclo.if_`/`id` muestran ocupación anterior al flanco incluso al descartarse.
`flush`, `if_descartado`, `id_descartado`, `id_bru`, `ex_bru` y `wb_bru` permiten
seguir la decisión. Los metadatos ID solo describen capturas aceptadas; no se
utilizan resultados de decodificación de un camino incorrecto.

## Contadores de bundles

`bundles_admitidos` cuenta búsquedas que realmente entraron en IF/ID.
`bundles_completados` cuenta bundles válidos que alcanzaron WB, una vez por
bundle aunque produzcan tres escrituras. `bundles_anulados` cuenta IF/ID válidos
previamente admitidos que flush impidió pasar a ID/EX. La búsqueda IF suprimida
nunca se admitió; se observa en la traza pero no vuelve a contarse como admitida.

Tras cada flanco se comprueba:

```text
admitidos = completados + anulados + bundles válidos aún en las tres etapas
```

Las visitas repetidas de un PC en bucles son admisiones distintas. Un bundle
anulado, incluido uno con codificación inválida, no se completa.

## WB y conflictos ALU/LSU/BRU

Los puertos se aplican en orden ascendente: 0 ALU, 1 LSU, 2 BRU.
**BRU > LSU > ALU** cuando escriben el mismo registro. Todas las solicitudes
quedan visibles, el conflicto se señala y la última prioridad determina el
valor del banco. No se elimina el bundle ni se introduce stall o excepción.

Se descartan escrituras hacia x0 antes de reconocer conflictos. Las señales
WB existentes siguen describiendo ALU/LSU; se agregan las BRU. `ultima_escritura`
conserva la última escritura efectiva del orden de puertos, incluso cuando
los destinos son diferentes. La traza conserva los tres resultados por separado.

## Dependencias, llamadas y retorno

ALU→BRU y load→BRU se prueban por separado en N+1/N+2/N+3: para productor B0,
ID consumidor lee en ciclos 3/4/5 y WB productor escribe en ciclo 4. Los dos
primeros capturan el valor antiguo; N+3 captura el actualizado. EX no vuelve
a leer el banco aunque WB ya haya ocurrido desde la captura.

Un consumidor ALU del enlace dentro del mismo bundle sye lee el valor anterior.
También syer utiliza la base anterior si ALU intenta producirla simultáneamente.
No se corrigen esas dependencias.

Hay una particularidad comprobada: el primer bundle del destino de un jump
entra en ID en ciclo 5, mientras el enlace del productor ya se escribió en
ciclo 4. Un ret inmediato en la subrutina sí lee el enlace correcto en RTL.
No es un caso de forwarding: los dos ciclos de flush ya dan la separación.

El validador actual de BALC es más conservador y rechaza ese caso por distancia
RAW en el CFG. Se preserva su diagnóstico y no se modifica. El test contrasta
el ret inmediato con el ensamblador de Arquitectura por instrucción, y utiliza
una variante con dos NOP explícitos para la integración BALC→MEM/BALB→S1→S2→S5.
No se afirma que un ret inmediato necesariamente falle en el procesador.

## Bucles, presupuesto y política de búsqueda

`sim_pipeline_ejecutar` conserva el presupuesto relativo y utiliza la API de
un solo ciclo. Un bucle no se ejecuta dentro de un paso individual. Al agotar
el presupuesto queda LIMITE, con banco, memoria y etapas conservados; reanudar
no agrega flancos ni vuelve a ejecutar efectos ya publicados.

Un redirect limpia `fin_busqueda`, incluso cuando se resuelve después de
alcanzar el final secuencial. Así un salto del último bundle puede volver al
programa y continuar. Finalizar requiere drenado sin nueva redirección.

`sim_pipeline_configurar_busqueda` selecciona, antes del primer paso de una
instancia activa o después de reset, una política administrativa:

| Caso | FINITA, predeterminado | FISICA |
|---|---|---|
| PC alineado dentro del código | Busca el bundle. | Busca índice PC[13:4]. |
| PC exactamente al final | Deja de admitir y drena; un redirect puede reactivar IF. | Busca NOP o alias según índice. |
| Fuera del código, dentro de IMEM | Error administrativo al intentar admitir esa búsqueda. | NOP explícito de la memoria no cargada. |
| Bits altos fuera de IMEM | Mismo error administrativo si no está drenando. | Descarta bits altos, como instruction_memory.sv. |
| PC desalineado | Error administrativo al admitir; un IF anulado no se valida. | El índice descarta bits bajos, como RTL. |
| Wraparound de 32 bits | Aritmética unsigned; se aplica después la política finita. | Continúa según el índice físico resultante. |

La alineación estática de instrucciones directas sigue siendo responsabilidad
de S2 en ambos modos. Syer fuerza alineación por su semántica propia.

El modo físico nunca termina por extensión: necesita presupuesto o reset. El
modo finito conserva el comportamiento de S3/S4 y no representa un HALT. Un
destino BRU fuera del código se publica en su flanco; el modo finito se detiene
antes del flanco posterior al intentar admitir IF. Por política transaccional,
su enlace WB puede quedar pendiente en ese error. El modo físico permite
contrastar el comportamiento real posterior, incluido escribir ese enlace.

No se comparan como equivalentes las ejecuciones posteriores a políticas
administrativas diferentes. Para aliasing, regiones no cargadas y bucles se
utiliza búsqueda física y el mismo presupuesto. En ejemplos finitos, toda
diferencia de validez exige PC fuera de la extensión; PC, flush, banco y memoria
se comparan sin ocultar efectos. Al llenar IMEM completa, una continuación
física puede envolver y ejecutar código nuevo: debe usarse el modo físico
para contrastar esos efectos, no atribuirlos al drenado finito.

## Reset y memoria

Reset limpia PC, etapas, resultados, enlaces, contadores y decisión BRU. Conserva
la configuración de búsqueda y DMEM. Iniciar una instancia nueva sí vuelve al
modo finito y recarga datos de la imagen. Restaurar DMEM sigue siendo la acción
explícita de S4, separada de reset.

Aunque BRU estuviera intentando saltar, reset domina el PC y descarta su enlace.
Un store ya activo en EX aún puede escribir durante ese mismo reset porque
memory.sv no recibe reset. S5 mantiene ese detalle y lo contrasta junto con
reset antes/después de resolver el salto, enlace pendiente/retirado y bucle.

## Ejemplo A: salto tomado, tabla real

B0 (PC0): `sumai x5,x0,7 | guardap x0,x0,0 | sye x31,48 | nop`.
B1/B2 contienen ALU, LSU y BRU que producirían efectos; deben anularse.
B3 (PC48) escribe x9=9. DMEM[0] comienza en 0x80.
Las etapas son preflanco; PC y efectos indican la transición del flanco.

| Ciclo | IF | ID | EX | WB | PC antes→después | Flush | Efecto |
|---|---|---|---|---|---|---|---|
| 1 | B0 | — | — | — | 0→16 | 0 | Admite B0. |
| 2 | B1 | B0 | — | — | 16→32 | 0 | Captura operandos de B0. |
| 3 | B2 | B1 | B0 | — | 32→48 | 1 | Descarta B1/B2; store B0 pone DMEM[0]=0. |
| 4 | B3 | — | — | B0 | 48→64 | 0 | WB conserva x5=7 y enlace x31=16. |
| 5 | — | B3 | — | — | 64→80 | 0 | Segundo ciclo sin EX válido. |
| 6 | — | — | B3 | — | 80→96 | 0 | ALU B3 calcula 9. |
| 7 | — | — | — | B3 | 96→112 | 0 | WB escribe x9=9; termina prueba. |

Los huecos EX de ciclos 4 y 5 son la penalización observada. B1 fue admitido
y anulado; B2 estaba en IF pero no se admitió. Contadores finales: 3 admitidos,
2 completados, 1 anulado. La traza comprueba que no aparezcan los resultados,
loads, stores ni enlaces de B1/B2.

## Ejemplo B: salto no tomado, tabla real

B0 (PC0): ALU escribe x5=7 y `igualno x0,x0,48` es falso.
B1/B2 escriben x6=8 y x7=9; B3 es NOP.

| Ciclo | IF | ID | EX | WB | PC antes→después | Flush | Efecto |
|---|---|---|---|---|---|---|---|
| 1 | B0 | — | — | — | 0→16 | 0 | Admite B0. |
| 2 | B1 | B0 | — | — | 16→32 | 0 | Captura comparación. |
| 3 | B2 | B1 | B0 | — | 32→48 | 0 | No se redirecciona ni se descarta. |
| 4 | B3 | B2 | B1 | B0 | 48→64 | 0 | WB x5=7. |
| 5 | — | B3 | B2 | B1 | 64→80 | 0 | WB x6=8. |
| 6 | — | — | B3 | B2 | 80→96 | 0 | WB x7=9. |
| 7 | — | — | — | B3 | 96→112 | 0 | Completa NOP y termina. |

No hay penalización: EX permanece ocupado tras llenarse hasta el drenado.
Contadores: 4 admitidos, 4 completados, 0 anulados.

## Pruebas, comandos y evidencia

La suite C contiene 181 comprobaciones. La suite Python tiene 17 pruebas,
1262 vectores BRU/PC y 884 ciclos en 55 ejecuciones de top. Cubre las seis
operaciones, signed/extremos, offsets, saltos no tomados/tomados, ret, conflictos,
camino incorrecto inválido/no soportado, bucles, presupuestos, reset y aliasing.

`tests/vliw/rtl/tb_sim_bru_ref.sv` instancia bru, pc_branch y top originales. Las
sondas guardan buses antes del flanco y registros/memoria después de NBA.
Comparan PC y validez por etapa, flush/destino, operandos aceptados, ALU, LSU,
cargas pendientes, enlaces, tres puertos WB, conflictos y 32 registros.
También comparan toda DMEM final y contadores en modo físico. Un registro de
sonda acompaña el PC EX/WB sin realimentar el procesador.

No hay target específico de PC en el Makefile consultado: pc_branch se verifica
con los vectores de la referencia nueva y con los programas de integración/top.
No se modifica ningún módulo ni testbench de Arquitectura.

Desde una copia de Compiladores con el RTL disponible:

```sh
export ARQUITECTURA_ROOT=/ruta/Proyecto2_Arquitectura1
make -B -k test
python3 -m unittest discover -s tests/vliw -p test_sim_cargador.py -v
python3 -m unittest discover -s tests/vliw -p test_sim_decode.py -v
python3 -m unittest discover -s tests/vliw -p test_sim_pipeline.py -v
python3 -m unittest discover -s tests/vliw -p test_sim_lsu.py -v
S5_EVIDENCIA=/tmp/evidencia-s5.json \
  S5_TABLA_TOMADO=/tmp/tomado-s5.json S5_TABLA_NO_TOMADO=/tmp/no-tomado-s5.json \
  python3 -m unittest discover -s tests/vliw -p test_sim_bru.py -v
```

Las sondas C usan C11, -Wall, -Wextra, -Wpedantic, -Werror y -O2. Ahora se
requiere enlazar también src/Simulador_VLIW/sim_bru.c al compilar pipeline manualmente; las
suites S3/S4 ya agregan esa dependencia sin cambiar Makefile ni CLI.

Sanitizadores, en copias independientes para no compartir artefactos ni flags:

```sh
CC='cc -fsanitize=undefined -fno-sanitize-recover=all -g' \
  python3 -m unittest discover -s tests/vliw -p test_sim_bru.py -v
ASAN_OPTIONS=detect_leaks=0 \
  CC='cc -fsanitize=address,undefined -fno-sanitize-recover=all -g' \
  python3 -m unittest discover -s tests/vliw -p test_sim_bru.py -v
```

Si falta RTL/Icarus, se reporta omisión y no equivalencia. Si una referencia
presente falla al compilar o diverge en un flanco, la prueba falla.

## Resultados y límites de S5

Base Compiladores `4ccc22552d47b0f53beb5e17bbc171c93688cbad`, rama
feat/simulador-vliw; RTL `418c92bb73d3f34fb02f2feca384a83b86a589d9`.
S1/S2/S3/S4 antes y después: 5/13/14/14 pruebas OK. Se conservan las 245
comprobaciones C del pipeline y 233 LSU. Solo se reemplazan rechazos BRU ahora
válidos por CRIPTO no soportada, sin quitar aserciones de errores/transacciones.

S5 estándar, UBSan y ASan+UBSan completos: 17 OK, código 0, sin omitidas ni
diagnósticos. LeakSanitizer fue NO-EJECUTABLE, código 1 por ptrace; la ejecución
ASan usa detect_leaks=0 y no se contabiliza como verificación de fugas.

Regresión general y codificador: código 0. BALC reconstruido sin instrumentación
conserva SHA-256 3079dfc930c3b77bebb029db0fc41e3a0a14b9e9020ff725cb2e26e800283c8f.
En Arquitectura se ejecutó antes/después, código 0:

```sh
make -B tb_bru tb_decoder_bru tb_pipeline_if_id tb_pipeline_id_ex \
  tb_pipeline_ex_wb tb_alu tb_lsu tb_memory tb_regfile tb_wb \
  tb_pipeline_integracion tb_top
```

PASS respectivos: 34,86,921,9793,912,11916,22,510,17822,18587,55,18 (60676).

S6 sigue pendiente: CRIPTO, Feistel, bóveda, AUTH/INIT, excepciones y puerto B
funcional. EX se calcula antes de aceptar IF/ID y publica al final; esa separación
permitirá incorporar prioridades de excepciones y cancelar transacciones sin
reescribir ALU/LSU. Aquí solo existe redirección BRU. No se implementan cachés,
optimizaciones, IDE ni cambios al compilador original.
