/*
 * Ventana "Métricas" del IDE (botón "Métricas"), en src/Metricas/.
 *
 * Ejecuta el código del editor en el simulador VLIW (metricas.c) y lo
 * muestra con gráficas:
 *   - Arriba: los números principales (ciclos, IPC, instrucciones
 *     ejecutadas, tamaño del código en bytes y bundles y tiempo de compilación).
 *   - Pestaña "Ejecución": qué tanto se usó cada slot del bundle (ALU, LSU,
 *     BRU, CRIPTO) y cuántos bundles traían trabajo y cuántos eran solo NOP.
 *   - Pestaña "Compilación": cuánto tardó cada fase del compilador
 *     (léxico/sintáctico/semántico, backend, codificación), en milisegundos.
 *   - Pestaña "Antes y después del DCE": el mismo código con y sin el código
 *     muerto (el que da el botón Código muerto), métrica por métrica,
 *     incluidos bytes originales/optimizados, diferencia y variación.
 *   - Pestaña "Tabla": todos los números, para leerlos sin las gráficas.
 * Al pasar el mouse sobre una barra se ve su valor exacto.
 */
#ifndef METRICAS_VENTANA_H
#define METRICAS_VENTANA_H

#include <gtk/gtk.h>

/* Abre la ventana para el código "texto" (no lo guarda: se puede liberar
 * después). "ruta" es la del archivo (o NULL) y "nombre" el que se muestra.
 * Si ya estaba abierta, la reemplaza. */
void ide_mostrar_metricas(GtkWindow *padre, const char *texto, const char *ruta,
                          const char *nombre);

/* Una fila de met_grafica_comparacion. */
typedef struct {
    const char *nombre;
    double valor[3];     /* Una barra por serie (azul, naranja, aqua). NAN = ese dato no
                          * existe (el nivel no compiló): se escribe "no disponible". */
    int decimales;       /* < 0: es un conteo (se escribe con miles). */
    const char *etiqueta_corta[3];  /* Con 3 series: cómo se nombra cada serie en la
                                     * nota del cambio ("-O1", "-O2"); la [0] no se usa. */
} MetFilaComparacion;

/*
 * Una gráfica de barras que compara 2 o 3 series fila por fila (cada fila
 * con su propia escala), como "Antes y después del DCE". Debajo del nombre
 * de cada fila va cuánto cambia cada serie respecto de la primera. Con UNA
 * serie, todas las filas usan la misma escala (se comparan entre sí).
 * Máximo 8 filas. Los textos se copian. La usa la ventana de niveles
 * -O0 / -O1 / -O2.
 */
GtkWidget *met_grafica_comparacion(const char *titulo, const char *subtitulo,
                                   const char *const *leyendas, int series,
                                   const MetFilaComparacion *filas, int cantidad);

#endif
