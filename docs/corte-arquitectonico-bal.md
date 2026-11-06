# Corte arquitectónico candidato BAL (B5)

B5 captura una observación pública posterior a un retorno compatible con el
arranque y anterior a ejecutar su bucle de permanencia. No certifica el fin
semántico de main, agrega HALT ni cambia los contratos de S6, M1/M2 o B1/B2.

## Contratos revisados

Base inspeccionada: `f4c90e274ba47774583bce7d994f6bc968536255`.
Se revisaron enlace_funciones/resolucion_saltos, cargador, decodificador, BRU,
pipeline IF–ID–EX–WB, M1, M2 y las herramientas/documentos B3/B4/B1.

El arranque carga main en x13 y llama con `syer x1,x13,0`. Su enlace apunta al
bundle siguiente. El resolvedor inserta dos NOP y conserva `__arranque_fin`
como comentario, seguido por `sye x0,0` en BRU. El regreso suele dirigirse a
los NOP de continuación, no directamente a la sentinela.

B4 localiza el marcador, usa CbBundle.linea para mapearlo, compara todo ASM
ensamblado con BALB y verifica su BRU mediante S2. B5 reutiliza esa evidencia;
no añade otro ensamblador ni adivina direcciones. La sonda B5 carga mediante S1
y vuelve a comprobar con S2 la sentinela, la forma de la llamada y el corredor
NOP. Los argumentos de direcciones proceden del reporte B4; invocar la sonda
con direcciones inventadas no autentica procedencia ni etiquetas del compilador.

## Efectos por etapa y criterio de drenado

Una BRU tomada redirige en EX y descarta IF/ID y la captura ID/EX especulativa.
El WB del bundle anterior puede escribir en ese mismo flanco. El bundle de
retorno entra en EX/WB y aún puede contener resultados ALU/load o cambios del
estado criptográfico público pendientes.

Los stores LSU y del puerto criptográfico, así como los efectos de bóveda,
se publican al aceptar EX. Las escrituras GPR y AUTH/INIT se publican en WB.
No existe en S6 una cola asíncrona de memoria más allá de estas etapas.
B5 solo observa esas interfaces: no prepara operaciones, aplica stores,
ejecuta manualmente WB ni lee secretos de la bóveda.

Contrato de observación: `bal-corte-v1-retorno-wb-corredor-nop`.
Se selecciona el primer flanco aceptado que satisface simultáneamente:

1. Sentinela identificada y código coherente según B4; SYE x0,0 aislado en BRU.
2. Llamada inicial compatible observada: PC verificado, SYER x1,x13,0 y destino
   marcado como main. El enlace conduce a un corredor exclusivamente NOP hasta
   la sentinela; puede ser un corredor vacío.
3. Retorno candidato posterior: SYER x0,x1,0 tomado hacia ese enlace, sin
   excepción. Su redirección vació IF/ID e ID/EX y publicó el PC de continuación.
4. El propio bundle del retorno pasó por WB en un flanco posterior. Esta
   condición conserva también su retiro en las métricas M1, incluso si el
   enlace x0 no tendría efectos en el banco.
5. Se admitió la búsqueda esperada de la continuación y sigue en IF/ID. No
   hubo otra redirección ni desvío de esa ruta desde el retorno.
6. Los pendientes IF/ID e ID/EX son NOP del corredor o la sentinela todavía
   sin ejecutar. EX/WB está vacío o contiene un bundle NOP. Ninguna escritura
   GPR/crypto en WB ni operación LSU/crypto en EX permanece pendiente.
7. No se observó ninguna excepción desde reset y no hay una instrucción
   pendiente capaz de generarla en la ruta aceptada. Esta es una condición
   conservadora: no se admiten excepciones previas aunque se hayan manejado.
8. La sentinela no se ejecutó en EX y M1 observó el mismo flanco de captura.

El retiro del retorno permite concluir, por ejecución en orden, que los bundles
anteriores ya pasaron por WB. Las operaciones de memoria anteriores también
publicaron EX. Se inspecciona la ocupación real y los efectos pendientes; no se
presume que detectar un salto tomado equivale a drenado.

No se exige pipeline vacío: IF/ID puede contener un NOP o, si el enlace ya
apunta allí, la sentinela pendiente. El reporte conserva PC y las cuatro
operaciones decodificadas de cada etapa ocupada, junto con banderas de efectos.
Las instrucciones especulativas detrás del retorno fueron anuladas por S6;
B5 no las elimina. No se presupone inocuidad de instrucciones desconocidas:
una continuación con operaciones distintas de NOP queda fuera de este contrato.

En el pipeline actual el primer punto que cumple estas condiciones aparece
un flanco después del retorno candidato, cuando se observa su WB y la búsqueda
de continuación. **La implementación no calcula retorno+1 para elegirlo**:
comprueba los eventos, registros del pipeline y condiciones descritas. Si un
pipeline futuro cambia el comportamiento, debe rechazarse o revisarse el
contrato; no se impone ese desplazamiento como constante arquitectónica.

## Identidad semántica y estados

SYER hacia el enlace de la llamada inicial es compatible con un retorno, pero
no identifica el frame dinámico. No se construye una pila alternativa ni se
inventan identificadores. `retorno_main_certificado` siempre es false.

| Conclusión B5 | Significado |
|---|---|
| CORTE_ARQUITECTONICO_CANDIDATO | Flanco que cumple el contrato y coincide con M2 al mismo horizonte. |
| CORTE_NO_DEMOSTRADO | Patrón/ruta fuera del contrato, excepción o sentinela ejecutada sin corte aceptable. |
| PRESUPUESTO_AGOTADO | Se consumió el horizonte antes de demostrar todos los eventos requeridos. |
| ERROR | Fallo de carga, ejecución, observación, protocolo, IO o contraste M2. |

Un error S6 posterior a una excepción sigue siendo ERROR; no se rebaja a
una observación exitosa. Sin corte, punto/snapshot/métricas de captura son null
y no se publica una copia DMEM como válida.

La sonda avanza con `sim_avanzar_ciclo`, observa cada cambio real de ciclos
mediante M1 y permite dos reintentos ante MEMORIA del mismo flanco antes de
avanzar. Otras fallas o memoria persistente impiden la captura. No se reconstruyen
contadores ni se repite el efecto ya ejecutado.

Al encontrar el corte deja de avanzar. No ejecuta reset, vacía etapas, asigna
estado ni solicita una pausa administrativa. Por ello los ejemplos quedan
ACTIVO en el snapshot; no se declara otra terminación. Al agotar el presupuesto,
PRESUPUESTO_AGOTADO es una clasificación de la herramienta, no un enum nuevo
ni una escritura en el estado de S6.

## Captura y comparación con M2

Se exporta el JSON completo M1 del punto: ciclos, admitidos, completados, útiles,
NOP, anulados, IPC, operaciones ISA/slot, memoria, saltos, excepciones y arquitectura.
Los valores y el IPC se conservan; B5 no los recalcula ni resta ciclos.
El estado público incluye 32 GPR, PC diagnóstico, AUTH/INIT y sondas de traps.

Una sesión M2 independiente carga el mismo BALB y avanza exactamente tantos
flancos como el corte. Se usan avances individuales para conservar el mismo
estado ACTIVO que S6, con reintentos públicos si M1 falta por memoria.
Se compara el JSON M1 íntegro, el estado S6 y los 65536 bytes de DMEM mediante
`sim_ejecucion_memoria` y memcmp. Una huella coincidente no sustituye la comparación.
Solo tras esa igualdad se escribe la copia completa DMEM en un archivo nuevo.

Si se utilizara M2.ejecutar(presupuesto), LIMITE describiría una pausa administrativa
válida al mismo horizonte. B5 no necesita introducir esa pausa: su comparación
usa la misma forma de avance. No compara un estado temprano con otro a 10000
como si fueran equivalentes. Una diferencia o consulta M2 fallida invalida
la captura; el reporte no presenta un snapshot válido.

## Resultados reales y métricas

Los tres programas existentes compilan sobre copias con balc -s, sin alterar
ASM/BALB/MEM. Presupuesto de búsqueda: 10000, sin constantes de corte por programa.
B4 se ejecuta por separado al horizonte completo para conservar el primer
salto final como referencia; B5 no lo ejecuta para capturar el snapshot.

| Programa | Sentinela PC | Llamada ciclo | Retorno candidato | Corte | Primer salto final, ejecución B4 |
|---|---:|---:|---:|---:|---:|
| factorial | 768 | 48 | 3402 | 3403 | 3407 |
| whale | 768 | 48 | 246 | 247 | 251 |
| puntajes | 1056 | 66 | 2747 | 2748 | 2752 |

En los tres cortes: ACTIVO, traps 0, pendientes M1 [1,0,0], IF/ID con un NOP
admitido de continuación, retorno retirado y ninguna escritura anterior pendiente.
La sonda coincide con M2 en arquitectura, métricas y toda DMEM.

| Programa | Ciclos M1 | Admitidos | Completados | Útiles | NOP completados | Anulados | IPC M1 |
|---|---:|---:|---:|---:|---:|---:|---:|
| factorial | 3403 | 3267 | 3130 | 1134 | 1996 | 136 | 0.333235380 |
| whale | 247 | 238 | 228 | 82 | 146 | 9 | 0.331983805 |
| puntajes | 2748 | 2678 | 2607 | 901 | 1706 | 70 | 0.327874818 |

Estos son **ciclos hasta el corte**, que incluyen arranque, llamada, NOP,
programa, retorno y continuación. No son ciclos exclusivos de main ni tiempo
de pared. La captura excluye la ejecución de la sentinela; no se resta una
estimación del bucle a los contadores. Tampoco se calcula speedup -O0/-O2.

Las pruebas comprueban además los oráculos previos de DMEM: factorial en 65528=120
y 65532=6; puntajes en 65520=24,65524=0,65528=13,65532=37. Whale no recibe un oráculo
semántico nuevo. Esos valores e igualdad con M2 no certifican de manera universal
el programa ni la identidad del retorno.

## Uso reproducible y evidencia

Desde una copia aislada del proyecto, construir una vez:

```bash
make -B balc
b5dir=$(mktemp -d /tmp/ce1108-b5-ejemplo.XXXXXX)
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  tools/sonda_fin_bal.c src/codificacion_binaria.c \
  src/sim_{ejecucion,metricas,pipeline,alu,lsu,bru,crypto,decode,cargador}.c \
  -o "$b5dir/sonda_fin"
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  tools/sonda_corte_bal.c \
  src/sim_{ejecucion,metricas,pipeline,alu,lsu,bru,crypto,decode,cargador}.c \
  -o "$b5dir/sonda_corte"
python3 tools/analizar_corte_bal.py \
  --balc /ruta/copia/balc --sonda-fin "$b5dir/sonda_fin" \
  --sonda "$b5dir/sonda_corte" \
  --fuente /ruta/proyecto/examples/factorial.bal \
  --fuente /ruta/proyecto/examples/whale.bal \
  --fuente /ruta/proyecto/examples/ejercicio3_puntajes.bal \
  --presupuesto 10000 --salida "$b5dir/capturas"
```

La herramienta usa B4.analizar para compilar copias y verificar ASM/BALB, sin
modificar B4 ni duplicar sus comprobaciones. --sonda-fin señala el ejecutable
B4 existente; --sonda señala B5. Las fuentes y herramientas se identifican por
SHA-256. Ninguna opción contiene un comando de shell. Los procesos se ejecutan
con cwd temporal propio y límites de timeout/captura B3/B2.

Salida obligatoria, nueva y fuera de carpetas fuente. Se publican:

- reporte.json versión1 con contrato, programa/imagen, evidencia B4, sentinela,
  llamada, retorno, primer bucle B4, punto, pipeline, snapshot, M1 y comparación.
- caso-NNN.dmem con los 65536 bytes exactos cuando existe captura válida,
  identificados por ruta relativa y SHA-256.
- b4/ con fuentes copiadas, ASM/BALB/MEM originales de esa compilación, reporte
  B4 y stdout/stderr. Los hashes de imagen se comprueban tras la sonda.
- anfitrion.json y salidas binarias de los procesos B5 con comandos exactos,
  retornos, diagnósticos y tiempos de pared separados de la arquitectura.

El reporte estable no incluye duraciones ni timestamps. Las rutas de comandos
históricos pueden apuntar a temporales ya renombrados; los artefactos equivalentes
permanecen en la carpeta publicada. El mismo fuente/ruta, herramientas y presupuesto
producen los mismos resultados arquitectónicos, no una promesa entre máquinas.

Sonda directa: `sonda_corte_bal imagen.bin pc_sentinela|- pc_llamada|- pc_main|- presupuesto salida.dmem`.
Las direcciones se obtienen de B4, son bytes y deben estar alineadas. '-' significa
metadato desconocido y no habilita un corte positivo. Presupuesto 0–1000000.
Solo S1 carga BALB; B5 no acepta contenedor crudo del ensamblador como BALB.

Retorno Python 0 significa análisis sin ERROR, incluidos casos sin corte;
1 indica error/infraestructura y 64 argumentos inválidos. La sonda devuelve0
por reporte escrito, 1 si no pudo publicarlo,64 por uso. Siempre revisar conclusión.
No se sobrescriben carpetas o copias de memoria anteriores. Los recursos temporales
son propios y se limpian sin eliminar carpetas ajenas.

Límites heredados: 1–32 fuentes, fuentes/artefactos 2 MiB, fuentes hermanas 16 MiB,
256 hermanas, stdout 1 MiB/stderr 64 KiB, timeout finito(0,3600]s por proceso.
B5 conserva hasta 16 muestras posteriores al retorno; el corte aceptado exige
que la última sea la del mismo flanco, sin aceptar evidencia truncada distinta.
Los ejecutables deben ser confiables; no se proporciona sandbox ni cuotas para
el compilador. La publicación usa rename a destino nuevo y fsync de los JSON,
sin garantía de durabilidad total ante caída o exclusión de carreras externas.

## Pruebas, regresiones y trazabilidad

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_analizar_corte_bal.py
CC='cc -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie -no-pie' \
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/test_analizar_corte_bal.py
```

Sin B5_BALC, la suite construye balc una vez en una copia externa. B5_ARTEFACTOS
conserva evidencia externa y B5_EVIDENCIA guarda comandos/resultados/fixtures.
Se construyen las sondas B4/B5 y una variante B5 adversarial una vez por suite.
Las envolturas públicas de reservas, M1 y copia M2 existen solo en un C temporal
de tests. No se fuerzan registros privados del pipeline ni se implementa ISA Python.
Python no está instrumentado por ASan; el compilador y las sondas sí pueden estarlo.
LSan se informa conforme a su ejecución real, NO VERIFICADO si ptrace lo bloquea.

| Requisito | Funciones/contrato | Pruebas |
|---|---|---|
| Argumentos, archivos, aislamiento y publicación | Python/B4/B3 | 01–02,22,27–30,34–35,38,42–44,48 |
| Evidencia estática y bucles ordinarios | B4 + corredor S2 | 04–09,16,18,37,39,47 |
| Drenado ALU/load/crypto y memoria EX | pendientes + WB observado | 11–15,17–18,25,33,41,46 |
| Captura/M1/DMEM e igualdad M2 | exportar + memcmp público | 03,10–14,19–26,31–33,40,45 |
| Regresión real y anidamiento | compilación BAL sin parches | 03,10,26 |

Son 48 métodos con subcasos. Los positivos verifican ruta, retiro, etapas,
escrituras pendientes, M1, GPR y bytes de DMEM, no solo una cadena impresa.
Se prueban ALU/load en el bundle ret, store publicado en EX, setpwd pendiente
hasta WB, WB anterior simultáneo con ret, excepción y errores, memoria/reintentos,
último byte distinto frente a M2 aunque el JSON M1 coincida, y especulativas
que no deben publicar efectos. Los distintos corredores incluyen IF/ID ocupado
por NOP o por la sentinela sin ejecutar. Los BAL/ASM auxiliares son demostraciones
temporales, no los quince programas oficiales de optimización.

Las regresiones completas S1–S8-C/M1/M2/B1/B2/B3/B4 y make -B -k test se ejecutan
en copia aislada antes/después, sin modificar pruebas. Las sondas RTL previas
conservan su alcance; B5 no afirma contraste RTL directo de cada BAL completo.

## Dependencias futuras y límites del contrato

B5 no cambia INCONCLUSO de B1/B2, agrega mapeos de registros ni compara versiones
optimizadas inexistentes. Un contrato futuro debe acordar identidad del retorno,
excepciones admisibles, estado público comparable y métricas entre layouts.
Los estados privados de bóveda permanecen fuera de la comparación pública.

Las garantías de drenado dependen del S6 actual en orden, sin cachés o colas
asíncronas. No certifican futuras latencias de memoria ni el trabajo de otros
integrantes. Una continuación no NOP o una ruta que no pueda justificarse
produce ausencia de corte; no se modifica el pipeline para hacerlo posible.
