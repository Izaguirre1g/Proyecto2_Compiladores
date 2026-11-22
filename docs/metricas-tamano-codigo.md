# Tamaño estático del código en el IDE — contribución al issue #30

La ventana **Métricas** muestra `Tamaño del código: XXXX bytes (YY bundles)`.
La fórmula es **bytes_codigo = bundles × 16**, porque un bundle VLIW tiene
cuatro slots de 32 bits. Incluye todos los bundles emitidos, también NOP y
guardas. Es la huella de instrucciones en IMEM, independiente de ciclos,
iteraciones o instrucciones ejecutadas.

`met_medir()` conserva `IdeEnsamblador.bundles` en `MetMedicion.bundles`.
El ensamblador tradicional del IDE obtiene ese conteo de `CbPrograma.cantidad`
tras codificar el programa realmente generado/enlazado. No se cuenta el texto
BAL ni el ensamblador textual. Tampoco se utiliza `largo_balb`: el contenedor
BALB incluye cabecera y datos globales además de las instrucciones. Las
variables y las reservas de memoria no se suman a los bytes de código.

La conversión pura `met_calcular_bytes_codigo(bundles, &bytes)` usa `size_t`,
acepta cero bundles y comprueba `SIZE_MAX / 16` antes de multiplicar. Devuelve
0 o -1, dejando la salida intacta ante argumento NULL u overflow.
`met_tamano_codigo()` aplica esa conversión a una medición LISTO o
SIN_TERMINAR; rechaza errores y ausencia de backend. Un corte dinámico conserva
el tamaño de la imagen completa; «no disponible» no se convierte en cero.

Resumen, tabla y comparación DCE conservan las demás métricas. La gráfica
mantiene la fila de bundles y añade bytes originales/optimizados, diferencia
absoluta y reducción/aumento porcentual. `met_comparar_tamano_codigo()` calcula
la magnitud sin restar unsigned en orden negativo; la variación es
`100 × (optimizado − original) / original`, negativa cuando hay reducción.
Si el original es cero, el porcentaje es **no definido**; la diferencia sí
se muestra. La misma comparación alimenta gráfica y tabla.

Ejemplo medido con `examples/dce/dce_ensamblador.bal` en esta contribución:
original **8448 bytes (528 bundles)**, optimizado por DCE **6960 bytes
(435 bundles)**; reducción **1488 bytes (17,6 %)**. Son dos emisiones reales
del backend tradicional, sin atribuirles una compilación O2.

## Alcance y futura integración O0/O2

Esta ventana sigue compilando con el ensamblador tradicional del IDE y su
reescritura DCE existente. No ejecuta O2 ni etiqueta esos resultados como O2.
La integración O0/O2 es trabajo independiente: debe obtener el conteo del
código **realmente emitido por cada variante** y llamar a la conversión pura
por separado. No debe reutilizar los bundles de O0 para medir O2. Ninguna de
estas funciones genera código, aplica pasadas o demuestra equivalencia.

## Verificación

En una copia externa: `make -B test-metricas`, `make -B balc`,
`make -B -k test` y `make ide`. La suite comprueba 1/varios bundles, NOP,
longitud real de código binario, diferencia con BALB, corte a presupuestos
distintos, DCE real y casos de cero, aumento, NULL, backend ausente y overflow.
Los sanitizadores instrumentan esas pruebas C. La compilación/validación visual
GTK y las capturas solo se acreditan cuando GTK y una pantalla están disponibles.
