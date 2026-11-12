# Propuesta de niveles de optimización

## Estado actual comprobado

La CLI actual ofrece análisis IR con --dce, --unroll[=k], --reorder[=L] y
--reorder-json[=L]. Permite unroll antes de reorder/JSON; DCE se ejecuta aparte.
Las opciones IR no producen ensamblador/binario optimizado mediante S7/S8-A.
El IDE DCE puede reescribir BAL y recompilarlo con el backend tradicional.
Ninguno de esos caminos conecta automáticamente la IR optimizada con la cadena
física nueva. No se añaden opciones -O a la CLI ni se cambia Makefile.

## Matriz de capacidades

| Funcionalidad | Estado actual | Integrabilidad/requisito | Responsable/contrato | Prueba necesaria |
|---|---|---|---|---|
| Compilación tradicional BAL | Existente | Sigue siendo referencia, sin reempaquetar su salida con direcciones obsoletas | Backend compartido | Regresión semántica/CB/ejecución |
| DCE | Existente en IR y rutas IDE/CLI | No conectado a productor físico | José; observables/errores y correspondencia | Equivalencia definida y casos de efectos/excepciones |
| Unrolling | Existente sobre IR | Requiere procedencia, capacidad y renovación de análisis | Meibel; clones, límites/CFG | Residuo, funciones, explosión de código, capacidad |
| Reordenamiento virtual | Existente sobre IR | No reemplaza DAG/plan físicos | Meibel/equipo; orden respecto de asignación | Registros vivos/spills y preservación de efectos |
| Asignación/spilling | Rama separada | BLOQUEADO para esta rama | Dylan; pool, ABI, marcos y expansiones | Casos de presión, clobbers, offsets y CFG |
| Productor ISA | Pendiente | BLOQUEADO para BAL real | Backend/equipo; exportación estructurada | Palabra/texto/S2, símbolos y generación |
| Cadena física | Implementada con entradas explícitas | Disponible cuando el productor cumpla contrato | Javier; VliwCadenaEntrada | I1-A1/S7/verificador/S8-A/CB/S2/S8-C |
| Enlace/reubicación para layout S7 | Pendiente | BLOQUEADO para direcciones materiales no resueltas | Equipo; fragmentos, convergencia y alcance | SYER/x13, expansiones y saltos largos |
| Evaluación | B1/B2 existentes, demostración consumidora | Necesita comparabilidad acreditada por versión | Javier/equipo; observables | EQUIVALENTE antes de rendimiento, INCONCLUSO separado |

## Configuración propuesta, todavía no implementada

| Nivel conceptual | Propuesta | Condiciones antes de habilitarlo |
|---|---|---|
| -O0 | Referencia tradicional sin transformaciones opcionales | Definir explícitamente observables y ruta de generación; no etiquetar fixtures como su salida |
| -O1 | DCE acordado y calendarización física conservadora tras asignación/lowering | Productor físico, ABI/spills, reubicación y pruebas de equivalencia |
| -O2 | Añadir unrolling acotado y evaluar reordenamiento virtual antes de la fase física | Orden de pasadas acordado, procedencia, presión/crecimiento, invalidación y evaluación real |

PENDIENTE DE ACUERDO: esos conjuntos y su orden. No es una especificación final
adoptada unilateralmente. Un esquema a evaluar sería IR→DCE→unrolling opcional
→limpieza acordada→reordenamiento virtual opcional→asignación/spills→expansión
física cerrada→CFG/DAG→S7/enlace validado. No se afirma que toda combinación
sea segura o rentable; las dependencias deben quedar acreditadas.

Los ciclos estimados del reordenamiento virtual no son IPC dinámico ni speedup.
Cada comparación necesita imágenes reales, iniciales/observables compatibles,
terminación o corte acordado y resultados correctos. Documentar tamaño IMEM,
guardas, presión y fallos de capacidad además del tiempo simulado.

BLOQUEADO: implementar opciones -O productivas hasta tener esos contratos.
IMPLEMENTADO aquí: análisis, matriz y frontera consumidora; no lowering paralelo,
asignación improvisada, nueva ISA, CLI o backend alternativo.
