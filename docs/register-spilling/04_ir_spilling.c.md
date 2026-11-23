# Documentación de funciones — `ir_spilling.c`

**Ruta en el proyecto:** `src/ir_spilling.c`  
**Propósito general:** transformar una IR con temporales virtuales `$tN` en otra IR que usa un conjunto limitado de registros lógicos `$rN` y ranuras de memoria `$spillN`.

> Las líneas indicadas son las líneas iniciales de las definiciones de función en el archivo fuente. Las funciones `static` son auxiliares internas del módulo.

## Funciones

| Línea | Función | Qué hace |
|---:|---|---|
| 32 | `duplicar` | Crea una copia independiente de una cadena para las instrucciones de salida. |
| 43 | `es_temporal` | Comprueba si un operando tiene exactamente el formato de un temporal virtual `$tN`. Evita tratar variables del usuario o etiquetas como temporales. |
| 52 | `registrar_temporal` | Añade un temporal al inventario si todavía no existe y le asigna una ranura lógica `$spillN`. |
| 73 | `buscar_temporal` | Busca un temporal previamente registrado y devuelve su índice interno. |
| 81 | `agregar` | Añade una instrucción a la IR de salida, copiando sus cadenas y ampliando el arreglo dinámico cuando es necesario. |
| 105 | `campos` | Clasifica los campos de una instrucción según sean lecturas o escrituras. Esta clasificación permite saber qué operandos necesitan registros y cuáles deben permanecer como etiquetas o nombres. |
| 130 | `nombre_registro` | Formatea el nombre de un registro lógico a partir de su índice, por ejemplo `$r0`, `$r1`, etc. |
| 136 | `guardar` | Emite `IR_SPILL_STORE` cuando un registro contiene un temporal modificado. No almacena valores que no cambiaron desde su carga. |
| 152 | `elegir_registro` | Selecciona un registro libre o el menos recientemente usado (LRU), evitando los registros protegidos por los operandos de la instrucción actual. |
| 167 | `adquirir` | Obtiene un registro para un temporal. Si el temporal ya está cargado reutiliza el registro; si no, elige uno, guarda el valor desalojado cuando está sucio y emite `IR_SPILL_LOAD` cuando la operación requiere leer el temporal. |
| 201 | `traducir_lectura` | Convierte un operando `$tN` a su `$rN` correspondiente. Los literales, variables escalares y nombres simbólicos se conservan sin cambios. |
| 215 | `vaciar` | Guarda todos los registros modificados. Se utiliza en los límites de bloques y antes de transferencias que requieren preservar el estado. |
| 224 | `invalidar` | Limpia el inventario de registros al entrar en un bloque, porque el flujo de control puede llegar desde distintos predecesores. |
| 234 | `procesar` | Reescribe una instrucción individual. Traduce las lecturas y escrituras de temporales, reserva el registro destino y agrega las instrucciones de carga/almacenamiento necesarias. Antes de una llamada vacía e invalida el banco. |
| 270 | `liberar_contexto` | Libera los nombres de temporales, las ranuras y los arreglos auxiliares usados durante la pasada. |
| 283 | `bloques_validos` | Comprueba que los bloques básicos cubran todo el código de entrada sin huecos, solapamientos ni índices fuera de rango. |
| 299 | `ir_aplicar_spilling` | API principal de spilling. Valida argumentos, CFG y salida vacía; inventaría los temporales; reescribe cada bloque con el banco limitado de registros; vacía los valores en los bordes; reconstruye el CFG y devuelve métricas de registros, ranuras, cargas, almacenes y desalojos. |

## Funcionamiento de la pasada

1. Identifica todos los `$tN` y reserva una ranura lógica `$spillN` por temporal.
2. Mantiene un banco de `$rN` y utiliza una política LRU para reutilizarlos.
3. Emite `IR_SPILL_LOAD` cuando un temporal necesita recuperarse desde su ranura.
4. Emite `IR_SPILL_STORE` cuando un registro sucio debe desalojarse.
5. Vacía los registros en límites de bloques y alrededor de llamadas para que el flujo de control no dependa del estado local del banco.
6. Reconstruye los bloques básicos de la IR transformada.

## Límites

- La pasada admite entre 3 y 32 registros en su API C.
- No acepta una IR que ya contenga operaciones `IR_SPILL_LOAD` o `IR_SPILL_STORE`.
- La numeración de `$spillN` es global para el `IrPrograma`; el contrato de memoria debe reservar el total de ranuras según las reglas documentadas para funciones múltiples.
