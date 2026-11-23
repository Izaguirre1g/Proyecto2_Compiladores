# Documentación de funciones — `ir_spilling_memoria.c`

**Ruta en el proyecto:** `src/ir_spilling_memoria.c`  
**Propósito general:** adaptar una tabla de símbolos cuya memoria ya fue asignada a un `IrSpillingBackend` por función. Verifica que los parámetros y locales coincidan con el layout de memoria antes de reservar las ranuras de spilling.

> Las líneas indicadas son las líneas iniciales de las definiciones de función en el archivo fuente.

## Funciones

| Línea | Función | Qué hace |
|---:|---|---|
| 10 | `pertenece_a_funcion` | Comprueba si un ámbito es el ámbito de la función indicada o un descendiente suyo, recorriendo los enlaces de padres. |
| 18 | `es_parametro` | Determina si un símbolo representa un parámetro, usando tanto la marca `es_parametro` como la clase `SIM_PARAMETRO`. |
| 23 | `es_dato` | Determina si el símbolo ocupa memoria de datos porque es variable, constante o lista. |
| 32 | `verificar_dato` | Verifica un símbolo contra el layout esperado: clase de almacenamiento, cantidad de bytes, alineación y desplazamiento exacto. Actualiza el cursor únicamente si el intervalo coincide. |
| 48 | `desde_tabla` | Realiza la validación completa para una función. Busca una función global única y su ámbito, verifica parámetros y variables locales, calcula el final del marco original y prepara el backend con las ranuras de spilling. En caso de error no publica un marco parcial ni modifica la tabla. |
| 121 | `ir_spilling_backend_desde_tabla` | API pública que crea el backend de spilling a partir de la tabla de símbolos con el contrato tradicional de tipos y offsets. |
| 126 | `ir_spilling_backend_desde_tabla_referencias` | Variante que permite el contrato ampliado para parámetros de arreglos unidimensionales pasados por referencia. |

## Orden de validación

1. Encuentra el símbolo global de función y su ámbito.
2. Comprueba los parámetros y sus home slots.
3. Comprueba las variables locales y sus offsets asignados por `memory.c`.
4. Alinea el final del marco original.
5. Añade las ranuras de spilling mediante el backend.
6. Publica el resultado solo si todas las verificaciones fueron exitosas.

## Límite importante

Este módulo consume la tabla ya asignada; no asigna ni modifica offsets de variables. La asignación de memoria del frontend debe ejecutarse antes. El contrato ampliado de referencias no implica que el lowering general de arreglos o llamadas esté completo.
