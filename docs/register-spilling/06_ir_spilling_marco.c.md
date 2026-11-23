# Documentación de funciones — `ir_spilling_marco.c`

**Ruta en el proyecto:** `src/ir_spilling_marco.c`  
**Propósito general:** calcular el marco de pila necesario para almacenar temporales derramados, traducir nombres `$spillN` a desplazamientos y emitir los accesos de memoria LSU.

> Las líneas indicadas son las líneas iniciales de las definiciones de función en el archivo fuente.

## Funciones

| Línea | Función | Qué hace |
|---:|---|---|
| 11 | `alinear_palabra` | Redondea una cantidad de bytes al siguiente múltiplo de cuatro y evita desbordamientos. |
| 19 | `calcular_marco` | Calcula el marco final: alinea los bytes ya ocupados, agrega cuatro bytes por ranura y comprueba el límite máximo del marco. En modo directo también verifica que todos los offsets quepan en el inmediato LSU permitido. |
| 39 | `offset_marco` | Analiza el nombre `$spillN`, valida que el índice sea decimal y esté dentro del número de ranuras, calcula su desplazamiento y comprueba los límites del marco y del acceso directo. |
| 62 | `ir_spilling_calcular_marco` | API pública que calcula el marco usando las restricciones de acceso LSU directo. |
| 64 | `ir_spilling_calcular_marco_expandido` | API pública que calcula el marco sin exigir que todos los offsets quepan en el inmediato directo; el consumidor debe expandir las direcciones que lo requieran. |
| 66 | `ir_spilling_offset` | Obtiene el desplazamiento directo de una ranura `$spillN`. |
| 68 | `ir_spilling_offset_expandido` | Obtiene el desplazamiento de una ranura permitiendo offsets que después deberán expandirse. |
| 73 | `ir_spilling_emitir_acceso` | Emite `cargai` para `IR_SPILL_LOAD` o `guardap` para `IR_SPILL_STORE`, usando `x2` como base de pila. Rechaza registros inválidos o reservados y valida el offset antes de escribir ensamblador. |

## Reglas del marco

- Cada ranura `$spillN` ocupa cuatro bytes.
- Las ranuras se colocan después de los bytes ya reservados por el backend: dirección de retorno, parámetros y variables locales.
- El límite de offset directo LSU es 1020 bytes.
- El marco máximo validado por este módulo es 65536 bytes.
- La variante expandida calcula offsets, pero no genera por sí sola las instrucciones adicionales para materializarlos.
