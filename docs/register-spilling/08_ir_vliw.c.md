# Documentación de funciones — `ir_vliw.c`

**Ruta en el proyecto:** `src/ir_vliw.c`  
**Propósito general:** implementar un puente experimental entre la IR después de spilling y el consumidor VLIW. Aplica spilling, valida registros lógicos, prepara el marco y traduce un subconjunto de operaciones a instrucciones físicas.

> Las líneas indicadas son las líneas iniciales de las definiciones de función en el archivo fuente.

## Funciones

| Línea | Función | Qué hace |
|---:|---|---|
| 36 | `error` | Escribe diagnósticos del productor IR→VLIW con la línea de origen de la instrucción. |
| 46 | `registro` | Intenta convertir un nombre de registro lógico `$rN` a su registro físico usando `ir_fisico_mapear`, respetando el límite configurado. |
| 50 | `entero` | Convierte un operando textual a un inmediato entero dentro del rango admitido. También interpreta `declare_true` y `declare_false` como 1 y 0. |
| 64 | `agregar` | Construye una instrucción textual para un slot, la ensambla con el codificador real (`cb_ensamblar`), decodifica la palabra resultante y verifica que la operación corresponda al slot y al tipo de control esperado. Guarda la operación validada en la entrada VLIW. |
| 117 | `operando` | Resuelve un operando como registro físico o, si es literal, materializa su valor en un registro auxiliar mediante `sumai`. |
| 129 | `enlace_control` | Comprueba que las etiquetas no estén duplicadas y resuelve los destinos pendientes de los saltos contra los símbolos de etiqueta registrados. |
| 148 | `salto` | Registra una transferencia de control pendiente y asocia el salto generado con el nombre de la etiqueta destino. |
| 157 | `lowering` | Traduce la IR derramada al subconjunto de instrucciones físicas admitido. Maneja inicio y fin de función, etiquetas, saltos, cargas/almacenes de spilling, copias, suma, resta y negación. Rechaza explícitamente cualquier operación u operando no soportado. |
| 262 | `ir_vliw_liberar` | Libera la cadena VLIW y la IR posterior al spilling contenidas en el resultado, y reinicia la estructura. |
| 269 | `ir_vliw_desde_ir` | API principal del puente. Valida argumentos, aplica spilling, verifica la IR asignada, prepara el marco, ejecuta el lowering, construye la entrada de la cadena VLIW y publica el resultado de forma atómica solo si todo termina correctamente. |

## Flujo de `ir_vliw_desde_ir`

1. Ejecuta `ir_aplicar_spilling`.
2. Comprueba la IR resultante con `ir_fisico_validar_programa`.
3. Calcula el marco y los offsets de spilling.
4. Convierte las instrucciones admitidas con `lowering`.
5. Construye la entrada estructurada del consumidor VLIW.
6. Llama a `vliw_cadena_construir`.
7. Si falla cualquier etapa, libera el resultado temporal y no publica una salida parcial.

## Alcance y limitaciones

Este archivo implementa un productor de fragmentos, no un backend BAL completo. La implementación está limitada a una función y a un subconjunto de operaciones. No acredita el lowering general de variables BAL, llamadas, retornos, arreglos, listas ni el enlace ejecutable de programas multifunción.
