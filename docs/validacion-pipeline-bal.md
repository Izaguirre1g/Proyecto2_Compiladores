# Validación independiente BAL → VLIW (B3)

B3 ejecuta el compilador BAL real y consume las herramientas públicas M2/M1.
No enlaza el frontend, interpreta BAL, decodifica ISA ni modifica código generado.
B1 conserva la comparación estricta y B2 la organización de benchmarks.

## Contratos comprobados

En HEAD `e90558517d268078fbdd666b6d5727eaaea4331c`, la CLI admite
`balc -s -o salida.bin fuente.bal`. Produce `salida.bin` y `salida.mem`;
`-s` agrega `fuente.asm` junto al fuente. No ofrece `-O0`, `-O1`, `-O2`.
README y lenguaje_actual conservan párrafos históricos que describen un backend
pendiente; el código CLI y las pruebas actuales sí generan binarios.

BALB v1 tiene encabezado de 48 bytes: firma y once uint32 little-endian,
seguidos por código y datos. Código: 1–1024 bundles de 16 bytes, ALU primero.
MEM: un entero hexadecimal de 128 bits por línea, CRIPTO primero en texto.
`int(linea,16).to_bytes(16,'little')` debe coincidir con el código BALB.
El tamaño textual de MEM no es el tamaño ejecutable.

M2 se invoca como `sim_vliw_metricas balb|mem archivo finita presupuesto json`.
Devuelve 0 para TERMINADO, 2 para LIMITE con reporte M1, 1 ante error y 64
por uso incorrecto. M1 v1 proporciona ciclos aceptados, bundles completados,
instrucciones útiles, IPC, GPR, PC, AUTH/INIT, traps y huella DMEM. B3 conserva
el documento y el IPC originales; los datos ausentes permanecen null.

## Uso aislado

Construir el compilador **en una copia externa** del proyecto. Construir M2
una vez, desde esa copia, sin modificar Makefile:

```bash
b3dir=$(mktemp -d /tmp/ce1108-b3-ejemplo.XXXXXX)
# En la copia aislada, no en el repositorio de trabajo:
make -B balc
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  tools/sim_vliw_metricas.c \
  src/sim_{ejecucion,metricas,pipeline,alu,lsu,bru,crypto,decode,cargador}.c \
  -o "$b3dir/sim_vliw_metricas"
python3 tools/validar_pipeline_bal.py \
  --balc /ruta/copia/balc --simulador "$b3dir/sim_vliw_metricas" \
  --fuente /ruta/proyecto/examples/factorial.bal \
  --fuente /ruta/proyecto/examples/whale.bal \
  --fuente /ruta/proyecto/examples/ejercicio3_puntajes.bal \
  --presupuesto 10000 --timeout 30 --salida "$b3dir/validacion"
```

`--salida` debe ser un directorio nuevo con padre existente, externo a las
carpetas de fuentes. Nunca se sobrescriben reportes anteriores. Las rutas
relativas de CLI se interpretan desde cwd; ejecutables y fuentes se resuelven
antes de ejecutar. Presupuesto uint64 obligatorio, incluido cero. Timeout
finito en (0,3600] segundos por proceso; 1–64 fuentes distintas.

Por caso se copian todos los `.bal` hermanos: `bring modulo` resuelve
`modulo.bal` junto al importador, incluidas las importaciones transitivas.
No se duplica el parser para calcular dependencias. Se rechazan symlinks,
archivos no regulares, más de 256 fuentes hermanas o más de 16 MiB conjuntos;
cada fuente admite 2 MiB. No se recorren carpetas padre. Un futuro contrato
de importación con otras rutas requerirá revisar esta política explícitamente.

El compilador recibe exclusivamente la copia y destinos absolutos propios.
Cada proceso también utiliza el directorio temporal del caso como cwd.
Así, `-s` no deja ensamblador en el repositorio. Cada caso comienza vacío.
No se reutilizan artefactos de otra ejecución, ni se crean salidas ficticias
cuando falla la compilación. Los procesos se lanzan sin shell, stdin cerrado,
grupo propio y timeout, reutilizando el transporte aprobado de B2.

## Informe y evidencia

El directorio publicado contiene:

- `reporte.json`: versión 1, herramientas con SHA-256, presupuesto, casos,
  limitaciones y disponibilidad de pares B2.
- `anfitrion.json`: comandos exactos, códigos, motivos y duraciones de pared.
  Las rutas temporales de los comandos se conservan como evidencia histórica;
  las copias y artefactos equivalentes permanecen bajo cada caso publicado.
- `caso-NNN/fuentes/`: copias BAL y ensamblador auxiliar producido por `-s`.
- `caso-NNN/programa.bin` y `programa.mem`, cuando fueron generados.
- stdout/stderr binarios exactos de compilador y simulaciones por caso.
- `manifiesto-b2.json`: pares demostrativos compatibles o lista vacía.

Cada caso contiene fuente/hash/dependencias, compilación, artefactos/hash/tamaño,
validación de formato, ejecución por formato, métricas, oráculo y resultado.
La compilación distingue COMPILACION_EXITOSA, COMPILACION_FALLIDA,
SALIDA_INCOMPLETA y SALIDA_INVALIDA. Las verificaciones estructurales no
sustituyen la carga de S1 que efectúa M2.

El reporte estable excluye tiempo de pared, comandos con nombres temporales
y stdout del compilador. Estos pueden variar sin cambiar la arquitectura.
Hashes de ejecutables, rutas originales o cambios de fuentes sí cambian la
identidad del experimento. No se promete igualdad byte a byte entre máquinas
o versiones de ejecutables diferentes.

| Resultado | Interpretación |
|---|---|
| VALIDADO | Ambas imágenes terminaron y satisfacen el oráculo declarado, dentro de su alcance. |
| VALIDADO_SIN_ORACULO | Ambas terminaron y reportan M1 válido; no se demostró el resultado semántico. |
| NO_TERMINADO | Alguna ejecución agotó el presupuesto; métricas parciales identificadas. |
| COMPILACION_FALLIDA | El compilador terminó con código no cero. |
| FORMATO_INVALIDO | Falta un artefacto o su estructura/código no corresponde. |
| EJECUCION_FALLIDA | M2 devolvió error; no se fabrican métricas. |
| ORACULO_FALLIDO | Ambas terminaron y al menos un esperado independiente no coincide. |
| ERROR_INFRAESTRUCTURA | Timeout, invocación, IO, protocolo o evidencia inválida. |

Prioridad tras compilar y cargar: infraestructura, ejecución fallida, límite,
oráculo fallido y validación. Un oráculo satisfecho parcialmente durante LIMITE
no convierte el resultado global en VALIDADO. Un fallo de oráculo permanece en
su sección aunque el límite tenga prioridad global. `disponible=false`, objeto
vacío por fase no iniciada y métricas null no significan cero.

Retornos de B3: 64 argumentos inválidos; 1 infraestructura/preparación/publicación;
2 cualquier resultado distinto de VALIDADO/VALIDADO_SIN_ORACULO; 0 validación sin
incidencias de esas categorías. 0 sin oráculo no certifica semántica general.
Tras un fallo individual se conserva evidencia y se continúa el lote.

M2 no distingue con retornos diferentes carga y ejecución. B3 conserva stderr;
el prefijo actual `ejecucion:` permite identificar una carga ya superada,
y los restantes errores se marcan FAIL_O_NO_OBSERVABLE sin inventar causa.
No hay métricas parciales en la CLI tras esos errores. Si cambia ese diagnóstico,
debe revisarse esa atribución; no cambia el contrato C.

## Oráculos independientes y memoria

`--oraculos /ruta/oraculos.json` acepta un contrato declarativo, sin comandos:

```json
{
  "version": 1,
  "fuentes": {
    "SHA256_HEXADECIMAL_DE_64_CARACTERES_DEL_FUENTE": {
      "procedencia": "especificación o prueba independiente identificada",
      "alcance": "propiedad concreta que se verifica",
      "registros": {"0": 0},
      "estado_crypto": 2,
      "traps": 0
    }
  }
}
```

La clave ilustrativa debe reemplazarse por el SHA real. `registros` es obligatorio
pero puede quedar vacío si se verifica crypto o traps. Índices 0–31 y valores
uint32, traps uint64; cero esperados requiere al menos una propiedad. Hash no
registrado produce ORACULO_NO_DISPONIBLE. La procedencia es declarada por quien
aporta el oráculo, no certificada por B3. x0 es solo un invariante ISA, no una
prueba de factorial. Los tests adversariales prueban acierto/fallo del mecanismo
sin convertir respuestas controladas en evidencia semántica del compilador.

La CLI M2 no entrega DMEM completa. El adaptador exclusivo de pruebas
`tests/test_validar_pipeline_bal.c` utiliza crear/ejecutar/memoria/exportar/liberar
de la API pública, sin tocar CPU ni estructuras opacas. Copia 65536 bytes a un
archivo nuevo y conserva M1. No es otro simulador ni un nuevo ejecutor de producción.

Su suite aplica esperados previos de `tests/test_resolucion_saltos.c`:

| Programa | Observación independiente en DMEM, little-endian |
|---|---|
| factorial | [65528]=120, [65532]=6; marco main de 16 bytes. |
| ejercicio3_puntajes | [65520]=24, [65524]=0, [65528]=13, [65532]=37; marco de 20 bytes. |
| whale | Sin contrato semántico adoptado aquí; comparación de formatos solamente. |

Se comparan también los 65536 bytes BALB/MEM, no hashes. Esto es evidencia
parcial a presupuesto explícito; no prueba que el programa haya terminado.
El reporte CLI permanece sin oráculo para esos ejemplos si no se entrega un
contrato de registros; `oraculos-dmem.json` identifica la evidencia adicional.
No se observa ningún secreto privado de la bóveda.

## Hallazgo reproducible con programas reales

Los tres candidatos compilan, generan BALB/MEM válidos y cargan en M2.
En 10000 ciclos ambos formatos dan:

| Fuente | Bundles codificados | Código bytes | Instrucciones retiradas | IPC M1 | Estado |
|---|---:|---:|---:|---:|---|
| factorial.bal | 189 | 3024 | 3332 | 0.333200000 | LIMITE |
| whale.bal | 144 | 2304 | 3332 | 0.333200000 | LIMITE |
| ejercicio3_puntajes.bal | 801 | 12816 | 3317 | 0.331700000 | LIMITE |

Confirmar los tamaños con el reporte del experimento: las cifras documentadas
corresponden al HEAD inspeccionado, no son contratos futuros. El arranque
`src/enlace_funciones.c` llama main y retorna a `__arranque_fin: sye x0,
__arranque_fin`. FINITA no convierte ese bucle en terminación ni existe una
instrucción HALT nueva. Por ello B3 devuelve 2 y NO_TERMINADO; el valor semántico
parcial puede ser correcto, pero no se publica ejecución completa ni rendimiento
validado. El bucle añade instrucciones a las métricas; no representan solo main.
No se parchea el binario ni se cambia S6 para eludirlo.

## Preparación B1/B2

Solo se agrega un par BALB/MEM si la sección de datos BALB completa contiene
ceros: MEM parte de DMEM cero. Código idéntico y flag de inicialización no
bastan cuando hay datos precargados distintos. Se registra la incompatibilidad,
sin convertirla en fallo de compilación. No se normalizan imágenes.

Los pares usan `categoria=demostracion`, rutas relativas al manifiesto y
presupuesto explícito, sin niveles -O declarados. Son artefactos distintos,
no la comparación de un archivo consigo mismo. Se demuestra interoperabilidad
de formatos, nunca optimización ni oráculo semántico independiente.

```bash
python3 tools/benchmarks_vliw.py \
  --manifest "$b3dir/validacion/manifiesto-b2.json" \
  --comparador /ruta/comparar_vliw \
  --json "$b3dir/b2.json" --csv "$b3dir/b2.csv"
# Los tres ejemplos actuales producen INCONCLUSO; B2 devuelve 3.
```

B1 hace su comprobación byte a byte inicial/final y B2 conserva su clasificación.
B3 no reconstruye equivalencia ni deltas. Para optimizaciones y los quince casos
oficiales faltan versiones reales con procedencia, contratos del equipo y una
política de terminación acordada. Ninguna demostración cubre cuotas oficiales.

## Pruebas, trazabilidad y límites

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_validar_pipeline_bal.py
# Opcionales: B3_BALC ejecutable ya construido; B3_ARTEFACTOS carpeta externa;
# B3_EVIDENCIA archivo externo de comandos y resultados.
CC='cc -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie -no-pie' \
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/test_validar_pipeline_bal.py
```

Sin B3_BALC la suite copia el proyecto y construye balc una vez allí. CC también
instrumenta ese compilador y las herramientas C si se deja construir. Python
no está instrumentado por ASan. LSan se intenta por separado y se informa según
su retorno real; un fallo bajo ptrace no demuestra ausencia de fugas.

| Requisito | Funciones | Pruebas |
|---|---|---|
| CLI y recursos | main/publicar/leer | 01–04, 26–27, 39, 41–42, 48–49 |
| Compilación aislada, dependencias | copiar_fuentes/caso | 05–10, 25, 33–34, 40, 43 |
| Estructura BALB/MEM | validar_formato | 11–15, 24, 46–47, 51 |
| Estados y métricas M2 | invocar/validar_metricas | 16–20, 37–38, 45, 52 |
| Oráculos externos | cargar_oraculos/comprobar_oraculo | 21–23, 44, 50 |
| Determinismo/timeout/publicación | invocar/publicar | 28–32, 53 |
| B1/B2 sin duplicación | manifiesto generado | 35, 46 |
| DMEM completa y oráculos previos | adaptador público C | 36 |

Son 53 métodos nuevos, con subcasos adicionales, no 52 optimizaciones.
Regresiones S1–S6/S7/S8-A/S8-C/M1/M2/B1/B2 y `make -B -k test` se ejecutan
por separado en la copia aislada. Las sondas RTL existentes se reutilizan;
no se afirma contraste directo RTL de los tres nuevos recorridos BAL completos.

El transporte B2 limita stdout a 1 MiB y stderr a 64 KiB; timeout mata el grupo
y recoge el hijo directo. Archivos generados leídos hasta 2 MiB; ejecutables
hasta 64 MiB. Los recursos del propio compilador seleccionado no se aíslan en
un sandbox de seguridad: se requiere un ejecutable confiable. Hash antes/después
detecta cambios persistentes de herramientas, no carreras que cambien y restauren
bytes. El inventario externo de esta intervención comprueba ambos repositorios.

La publicación prepara todo en un temporal propio y hace rename del directorio
nuevo al destino, con fsync de los JSON. No hay garantía de durabilidad frente a
caída del sistema sin fsync de cada artefacto/directorio; tampoco exclusión
transaccional de otros procesos que creen el destino entre comprobación y rename.
Una falla elimina solo temporales propios y no borra el destino ajeno.
