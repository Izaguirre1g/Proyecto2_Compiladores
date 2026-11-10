# S3: simulación por ciclos del pipeline y ejecución ALU

S3 reproduce IF–ID–EX–WB para programas ALU/NOP. Mantiene bundles de 128 bits
con una operación ALU en el slot 0 y NOP en LSU, BRU y CRIPTO. Las unidades de
esos tres slots siguen pendientes; una operación reconocida en ellos detiene
la simulación explícitamente. No se ejecutan como ALU ni se ignoran.

## Componentes y contratos

S1 carga MEM/BALB en `SimImagen`; S2 decodifica los cuatro slots. S3 utiliza
ambas interfaces sin modificar sus módulos. `sim_alu` calcula un resultado;
`sim_pipeline` controla cuándo se capturan operandos y cuándo se escribe.

```text
SimImagen -> IF -> IF/ID -> ID + banco -> ID/EX -> ALU EX -> EX/WB -> WB
                                 ^                               |
                                 +--------- registros -----------+
```

Las cuatro etapas operan sobre bundles diferentes en un mismo flanco. No se
agrega MEM, forwarding, bypass, scoreboard ni detenciones por dependencias.

`SimPipeline` contiene PC en bytes, número de ciclos, 32 registros, tres
registros de segmentación, estado de ejecución, última escritura efectiva y
traza del último ciclo. Cada instancia es independiente; no hay estado global
mutable. Los contadores de bundles admitidos/completados sirven para observar
el llenado y vaciado, no constituyen un sistema general de rendimiento.

| Estructura | Información y momento de uso |
|---|---|
| `SimIfId` | Bundle de cuatro palabras, PC original y validez capturados por IF. |
| `SimIdEx` | Operación ALU/NOP, palabra, PC, destino y operandos capturados en ID. |
| `SimExWb` | Resultado, operación, destino y PC pendientes de retiro en WB. |
| `SimCiclo` | Etapas, lecturas ID, cálculo EX y escritura WB del flanco observado. |
| `SimEscritura` | Última escritura efectiva: ciclo, PC, destino y valor. Persiste hasta otra escritura o reset. |
| `SimErrorPipeline` | Ciclo intentado, PC, bundle, slot, palabra, estado S2 y causa. |

La imagen se recibe como referencia prestada e inmutable. Debe permanecer viva
sin modificaciones mientras se utiliza el simulador; S3 no la libera, copia ni
modifica. DMEM conserva la representación de S1 y no recibe accesos en S3.
El llamador libera la imagen mediante `sim_liberar_imagen` cuando termina.

## API pública

```c
sim_pipeline_iniciar(&sim, imagen);
sim_avanzar_ciclo(&sim);
sim_pipeline_ejecutar(&sim, presupuesto_adicional);
sim_pipeline_reiniciar(&sim);
sim_alu_ejecutar(operacion, a, b, &resultado);
```

- Inicializar exige imagen no nula, entre 1 y 1024 bundles y entrada cero,
  según S1/RTL. PC, registros, ciclos, etapas y observaciones se limpian.
  No se inventan valores iniciales para SP o GP.
- Avanzar realiza exactamente un flanco si la simulación está activa o pausada
  por límite. En error o terminación devuelve el estado previo sin avanzar.
- Ejecutar utiliza la función de un ciclo, con presupuesto relativo. Si queda
  trabajo al agotarse, devuelve `SIM_PIPE_LIMITE`; permite reanudar. Un
  presupuesto cero no ejecuta un flanco.
- Reiniciar aplica el estado resultante del reset RTL entre pasos, conserva la
  imagen y descarta resultados pendientes. Reinicia el contador a cero. Los
  intervalos de reset no se cuentan como ejecución; no se simula un reloj
  adicional para esta operación administrativa.
- La ALU admite las 18 operaciones. Un argumento de salida nulo, NOP u operación
  ajena devuelve `false` sin modificar el resultado recibido. NOP se trata en
  el pipeline, como bundle válido sin escritura.

Los estados generales distinguen activo, terminado, límite, argumento, PC,
decodificación, operación no soportada e inconsistencia ALU. La estructura
requiere inicialización antes de avanzar o reiniciar. Los campos públicos
permiten inspección; salvo las inyecciones de fallos de los tests, el llamador
no debe alterar etapas, registros ni la imagen durante la ejecución.

## Un ciclo: estado anterior y siguiente

`sim_avanzar_ciclo` calcula un estado siguiente pequeño que comparte la
referencia a las memorias. Todas las etapas leen exclusivamente el estado
anterior; se publica el estado siguiente conjuntamente al final.

1. **IF:** valida alineación a 16 bytes y extensión antes de indexar IMEM.
   El índice es `PC/16`. Captura bundle, PC y validez en el siguiente IF/ID.
2. **ID:** consume el IF/ID anterior. S2 revisa los cuatro slots antes de
   aceptar el bundle. Lee los registros anteriores y elige rs2 o inmediato.
   Guarda esos valores en el siguiente ID/EX.
3. **EX:** consume exclusivamente el ID/EX anterior. Calcula ALU sin volver a
   leer el banco. El resultado entra al siguiente EX/WB, sin escribir registros.
4. **WB:** consume el EX/WB anterior. Completa el bundle y publica su resultado
   al banco siguiente si es ALU con destino distinto de x0.
5. Publica PC+16, ciclos+1, etapas y banco. Una etapa sin entrada válida deja
   una burbuja inválida en su registro siguiente.

Así, aunque WB actualice un registro en el flanco actual, ID ha capturado su
valor anterior. Cambiar el orden de publicación a write-first introduciría un
bypass inexistente en el RTL. EX tampoco puede aprovechar una escritura que
ocurrió después de su lectura ID.

`SimCiclo` describe ocupación y señales **antes** del flanco numerado. El PC,
los registros y los registros de segmentación de `SimPipeline` ya representan
el estado **después**. La última escritura solo cambia cuando existe una
escritura efectiva; para saber si hubo escritura en el ciclo actual se consulta
`ultimo_ciclo.wb_escribe`.

## ALU de 32 bits

Se admiten `suma`, `resta`, `sumai`, `restai`, `and`, `or`, `xor`, `andi`,
`ori`, `xori`, `cizq`, `cder`, `cader`, `cizqi`, `cderi`, `caderi`, `mrq` y `myq`.

Suma y resta usan `uint32_t` para obtener aritmética módulo 2³² sin overflow
signed. Todos los inmediatos ALU de 11 bits, incluidos los lógicos, llegan de
S2 con extensión de signo. ID convierte ese valor a su patrón de 32 bits.
No vuelve a interpretar ni decodificar la instrucción.

Los corrimientos usan `b & 31`: 32 equivale a cero posiciones y 33 a una.
El inmediato bruto 2047 es -1 extendido, con cantidad efectiva 31. El corrimiento
aritmético se construye con desplazamiento unsigned y relleno explícito de
unos si el bit 31 era uno. El caso cero evita un desplazamiento por 32.

`mrq`/`myq` comparan complemento a dos mediante inversión del bit de signo y
comparación unsigned. Devuelven 0/1 sin depender de conversiones fuera de rango
ni del desplazamiento de enteros negativos del host.

## NOP, x0 y validez

Un NOP explícito tiene bundle y PC válidos; avanza hasta WB y aumenta los
bundles completados, pero no solicita escritura. Una burbuja o etapa inválida
por reset no representa un bundle y no completa ni escribe. ALU hacia x0 sí
calcula y completa; la escritura se descarta y x0 permanece cero.

S2 utiliza 32 como registro ausente. La sonda normaliza ese metadato a x0 solo
al compararlo con las salidas físicas del decoder para NOP/inmediatos: el RTL
expone cero en esos campos no usados. No se cambia la instrucción ni su efecto.

## Demostración N+3 contrastada con RTL

Programa: B0 `sumai x2,x0,7`; B1 `suma x5,x2,x0`; B2 `suma x6,x2,x0`;
B3 `suma x7,x2,x0`. PC de B0/B1/B2/B3: 0/16/32/48 bytes. Inicialmente todos
los registros valen cero.

| Ciclo | IF | ID | EX | WB | Lectura ID antes del flanco / escritura WB al flanco |
|---|---|---|---|---|---|
| 1 | B0 (0) | — | — | — | Sin lectura ni escritura válidas. |
| 2 | B1 (16) | B0 (0) | — | — | B0 captura x0=0, inmediato=7. |
| 3 | B2 (32) | B1 (16) | B0 (0) | — | B1 captura x2=0; EX de B0 calcula 7. |
| 4 | B3 (48) | B2 (32) | B1 (16) | B0 (0) | B2 captura x2=0; después WB escribe x2=7. |
| 5 | — | B3 (48) | B2 (32) | B1 (16) | B3 captura x2=7; B2 ejecuta con el 0 capturado; WB escribe x5=0. |
| 6 | — | — | B3 (48) | B2 (32) | WB escribe x6=0. |
| 7 | — | — | — | B3 (48) | WB escribe x7=7; termina la prueba con pipeline vacío. |

N+1 lee en ciclo 3; N+2 en ciclo 4, simultáneo con WB pero antes de que cambie
el banco; N+3 en ciclo 5, cuando el resultado ya existe. Las pruebas separadas
comprueban estas tres distancias. No se insertan NOP, stalls ni forwarding
para corregir un programa mal calendarizado.

## Fin de prueba y errores

Al alcanzar exactamente `cantidad_bundles*16`, IF deja de admitir bundles.
Se vacían las tres etapas restantes; el último WB cuenta como ciclo. Para N
bundles secuenciales sin errores se ejecutan N+3 ciclos y el PC termina en
`16*(N+3)`. Durante el vaciado el PC sigue avanzando, sin acceder a IMEM.
Un PC desalineado o superior al final antes de alcanzar ese final secuencial
produce `SIM_PIPE_PC`.

Esta es una política de pruebas finitas, no HALT ni terminación arquitectónica.
El hardware continúa buscando NOP fuera del archivo y eventualmente envuelve
su IMEM física. S3 no vuelve a admitir B0 al terminar 1024 bundles.

Un rechazo de ID conserva todo el estado anterior al flanco intentado: no
admite el nuevo IF, no ejecuta parcialmente el bundle y tampoco publica un WB
anterior en ese intento. Solo cambian el estado de detención y el diagnóstico.
Es una detención de depuración, **no una excepción del hardware**. El diagnóstico
incluye PC, índice, slot, palabra, estado S2 y causa. Errores y finalización son
estables hasta reset; el límite de ciclos sí permite reanudar.

## Comparación ejecutable y comandos

`tests/vliw/rtl/tb_sim_pipeline_ref.sv` instancia el `top` y la `alu` originales.
Usa sondas jerárquicas; el PC de WB se observa mediante un registro auxiliar
que sigue EX/WB sin realimentar RTL. Cada fila conserva señales preflanco y los
32 registros después de las asignaciones no bloqueantes. La prueba compara
PC, validez, operandos ID/EX, resultado EX, destino/dato/habilitación WB y banco.
También compara reset antes de WB y después de escrituras ya publicadas.

Las únicas diferencias previstas son la validez fuera de la extensión cargada
(control finito frente a fetch físico continuo) y los índices ausentes de S2.
Cada diferencia de validez exige PC fuera del programa; nunca se permite
ocultar una diferencia dentro del código cargado. No se altera el RTL ni se
comparan solamente resultados finales.

Los casos con hazards deliberados usan `encode_instruction` y `Program` del
ensamblador existente de Arquitectura para codificar/serializar. Omiten su
validación global de calendarización, no su codificación. El programa válido
con las 18 operaciones sí pasa por `balc -x` y se carga en MEM y BALB.

Desde la raíz de Compiladores, con compilador C, Python, herramientas de la
construcción existente e Icarus Verilog disponibles:

```sh
ARQUITECTURA_ROOT=/ruta/Proyecto2_Arquitectura1 \
  python3 -m unittest discover -s tests/vliw -p test_sim_pipeline.py -v

python3 -m unittest discover -s tests/vliw -p test_sim_cargador.py -v
ARQUITECTURA_ROOT=/ruta/Proyecto2_Arquitectura1 \
  python3 -m unittest discover -s tests/vliw -p test_sim_decode.py -v
make -B -k test
```

La suite S3 compila las sondas C con `-std=c11 -Wall -Wextra -Wpedantic -Werror
-O2`. Requiere `ARQUITECTURA_ROOT` o el repositorio hermano con el nombre
`Proyecto2_Arquitectura1`. Si falta RTL/Icarus, las pruebas dependientes se
reportan como omitidas, nunca como contraste exitoso. Una referencia presente
que no compila produce error. `S3_TRAZA=/tmp/tabla-s3.json` conserva la traza
del ejemplo sin agregar artefactos al repositorio.

Para ejecutar solo las pruebas C:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude \
  tests/vliw/test_sim_alu.c src/Simulador_VLIW/sim_alu.c -o /tmp/test_sim_alu
/tmp/test_sim_alu
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude \
  tests/vliw/test_sim_pipeline.c src/Simulador_VLIW/sim_pipeline.c src/Simulador_VLIW/sim_alu.c \
  src/Simulador_VLIW/sim_decode.c src/Simulador_VLIW/sim_cargador.c -o /tmp/test_sim_pipeline
/tmp/test_sim_pipeline
```

La sonda de pruebas acepta `mem|balb ruta presupuesto [reset_despues]`, imprime
JSON por flanco y devuelve 0 solo si termina; no agrega opciones al CLI de BALC.
`reset_despues` se mide en pasos previos al reset, que se aplica una sola vez.

## Alcance pendiente

S4 incorporará ejecución LSU y estado de datos escribible con propiedad definida;
S5, BRU, redirects y flush; S6, criptografía, bóveda y excepciones. Tendrán que
ampliar los registros de control/resultados conservando la actualización
simultánea. S3 rechaza esas operaciones y no anticipa su comportamiento.
No se integra al CLI, IDE, scheduler ni sistema de cachés o métricas general.

## Evidencia de esta implementación

Base de Compiladores: `281736bb3f25204e5883818dfe7ddae8e21f81a0`, rama
`feat/simulador-vliw`. Referencia RTL:
`418c92bb73d3f34fb02f2feca384a83b86a589d9`. Las construcciones se ejecutaron
en copias temporales de ambos repositorios para preservar los originales.

| Verificación | Resultado final | Código |
|---|---|---|
| S1, `test_sim_cargador.py` | 5 pruebas OK, igual a la línea base. | 0 |
| S2, `test_sim_decode.py` | 13 pruebas OK; contraste de 224 vectores RTL, igual a la línea base. | 0 |
| S3, `test_sim_pipeline.py` | 14 pruebas OK, ninguna omitida; 3456 vectores ALU contrastados. | 0 |
| Prueba C ALU | 18 operaciones, 29 casos conocidos OK. | 0 |
| Prueba C pipeline | 245 comprobaciones OK. | 0 |
| Pruebas C con UBSan | Mismos resultados, sin diagnósticos. | 0 |
| `make -B -k test` | Regresión general OK antes y después. | 0 |

En la copia de Arquitectura se ejecutó antes y después:

```sh
make -B tb_alu tb_regfile tb_regfile_pipeline tb_pipeline_if_id \
  tb_pipeline_id_ex tb_pipeline_ex_wb tb_pipeline_integracion tb_top
```

Los ocho testbenches devolvieron código 0 y los mismos PASS: 11916, 17822, 10,
921, 9793, 912, 55 y 18 verificaciones respectivamente (41447 en total).
S3 compara además trazas completas del programa del `tb_regfile_pipeline`,
N+1/N+2/N+3, cadenas, escrituras consecutivas, NOP/x0/x31, las 18 operaciones
cargadas desde MEM/BALB, 128 bundles aleatorios y dos reinicios intermedios.

La construcción de `balc` antes y después produjo el mismo SHA-256:
`3079dfc930c3b77bebb029db0fc41e3a0a14b9e9020ff725cb2e26e800283c8f`.
No fue necesario corregir archivos existentes ni modificar RTL. Durante el
desarrollo se corrigieron exclusivamente tres detalles de la prueba nueva:
ruta de inclusión RTL, estímulo inicial para activar la ALU combinacional y
rango de inmediatos de corrimiento del generador aleatorio. La ejecución final
no conserva esos fallos.
