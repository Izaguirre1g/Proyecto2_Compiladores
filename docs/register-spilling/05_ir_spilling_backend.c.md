# Documentación de funciones — `ir_spilling_backend.c`

**Ruta en el proyecto:** `src/ir_spilling_backend.c`  
**Propósito general:** conectar las ranuras lógicas del spilling con el marco de pila del backend. Prepara el tamaño final del marco y ofrece funciones para emitir prólogo, epílogo y accesos LSU.

> Las líneas indicadas son las líneas iniciales de las definiciones de función en el archivo fuente.

## Funciones

| Línea | Función | Qué hace |
|---:|---|---|
| 6 | `preparar` | Valida el tamaño del marco existente, su alineación y la cantidad de parámetros. Calcula un marco que incluya las ranuras de spilling y publica el resultado solo si todas las comprobaciones pasan. El parámetro `expandir` selecciona el cálculo con o sin offsets extendidos. |
| 25 | `ir_spilling_backend_prologo` | Emite el prólogo de función reutilizando `fm_emitir_prologo`, con el tamaño final del marco y la cantidad de parámetros almacenada en el backend. |
| 33 | `ir_spilling_backend_epilogo` | Emite el epílogo con exactamente el mismo tamaño de marco reservado por el prólogo, para restaurar correctamente la pila. |
| 39 | `ir_spilling_backend_acceso` | Convierte una instrucción lógica `IR_SPILL_LOAD` o `IR_SPILL_STORE` en el acceso correspondiente, delegando la emisión y validación a `ir_spilling_emitir_acceso`. |
| 48 | `ir_spilling_backend_preparar` | API pública que prepara el backend usando el límite tradicional de desplazamiento directo LSU. |
| 50 | `ir_spilling_backend_preparar_expandido` | API pública que prepara el backend permitiendo calcular offsets extendidos que el consumidor deberá expandir antes de emitir accesos físicos. |

## Relación con otros módulos

- `ir_spilling_marco.c` calcula el marco y los offsets.
- `ir_spilling_memoria.c` puede construir el backend a partir de una tabla de símbolos ya asignada.
- `funciones_marco.c` proporciona la emisión real del prólogo y epílogo.
