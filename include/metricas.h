/*
 * Métricas del código del editor (src/Metricas/), para las gráficas del IDE.
 *
 * Compila el texto del editor, ejecuta el binario en el simulador VLIW de
 * Javier (src/Simulador_VLIW/) y devuelve lo que contó su recolector de
 * métricas (sim_metricas.h): ciclos, IPC, bundles útiles y NOP, uso de
 * cada slot, saltos, lecturas y escrituras de memoria...
 *
 *   texto del editor -> ensamblador y .bin (ide_ensamblador.c, igual que balc)
 *                    -> simulador, hasta que main termina
 *                    -> SimMetricasResumen
 *
 * Cuándo termina el programa: el arranque llama a main y, cuando main
 * regresa, se queda para siempre en un salto a sí mismo
 *     __arranque_fin:  nop | nop | sye x0,0 | nop
 * El simulador no sabe que eso es "el fin": seguiría contando ciclos hasta
 * agotar el presupuesto. Por eso se mide hasta el PRIMER ciclo en que el
 * PC llega a ese salto final; lo que viene después (el bucle infinito) no
 * se cuenta.
 *
 * No usa GTK: se puede probar sin abrir ventanas (tests/test_metricas.c).
 * No modifica el simulador: solo usa su API pública (sim_ejecucion.h).
 */
#ifndef METRICAS_H
#define METRICAS_H

#include <stddef.h>
#include <stdint.h>

#include "ide_ensamblador.h"
#include "sim_ejecucion.h"

/* Cuántas veces se compila para medir el tiempo de compilación. Una sola
 * medida de unos pocos milisegundos varía mucho (la primera vez el sistema
 * todavía no tiene los archivos en caché, otro programa ocupa el
 * procesador...). Se compila varias veces y se toma la MEDIANA: la corrida
 * del medio, que no se deja llevar por una muy lenta o muy rápida. */
#define MET_COMPILACIONES 5

/* Presupuesto de ciclos si no se pide otro: si el programa no termina antes,
 * se considera que no terminó (probablemente tiene un ciclo infinito). */
#define MET_PRESUPUESTO_DEFECTO 5000000u

/* Cómo terminó la medición. */
typedef enum {
    MET_LISTO = 0,         /* main terminó: las métricas son del programa completo. */
    MET_CON_ERRORES,       /* El código tiene errores: hay que corregirlos antes. */
    MET_SIN_BACKEND,       /* El código es válido, pero no se pudo generar el binario (ver motivo). */
    MET_SIN_TERMINAR,      /* Se agotó el presupuesto: las métricas son solo hasta ahí. */
    MET_FALLO_SIMULADOR    /* El simulador se detuvo con un error (ver motivo). */
} MetEstado;

typedef struct {
    MetEstado estado;
    char *motivo;             /* Explicación con SIN_BACKEND, SIN_TERMINAR o FALLO_SIMULADOR. */
    size_t bundles;           /* Tamaño del programa: bundles en memoria (métrica estática). */
    uint32_t direccion_fin;   /* Dirección del salto final del arranque (__arranque_fin). */
    uint64_t presupuesto;     /* Ciclos máximos que se permitieron. */
    IdeAsmTiempos compilacion; /* Tiempo de compilación (mediana de MET_COMPILACIONES
                                * corridas), por fase. Válido desde MET_SIN_BACKEND. */
    SimMetricasResumen sim;   /* Métricas dinámicas (válidas con LISTO y SIN_TERMINAR). */
} MetMedicion;

/*
 * Mide el texto del editor.
 *   texto, largo: el contenido del editor.
 *   ruta:         la ruta del archivo (para los "bring"), o NULL si no se ha guardado.
 *   presupuesto:  ciclos máximos (0 = MET_PRESUPUESTO_DEFECTO).
 * Devuelve 0 si se pudo intentar (el estado dice cómo terminó) o -1 si
 * falló algo interno (archivo temporal o memoria).
 */
int met_medir(const char *texto, size_t largo, const char *ruta, uint64_t presupuesto,
              MetMedicion *m);

/*
 * Porcentaje de "parte" sobre "total" (0 si total es 0). Para las gráficas:
 * por ejemplo, qué tanto se usó un slot = instrucciones_por_slot[i] / completados.
 */
double met_porcentaje(uint64_t parte, uint64_t total);

/* Nombre del slot i del bundle (0 ALU, 1 LSU, 2 BRU, 3 CRIPTO). */
const char *met_nombre_slot(int slot);

/*
 * Compara dos mediciones (por ejemplo, antes y después del DCE) con la
 * función de Javier (sim_metricas_comparar). Las dos tienen que estar en
 * MET_LISTO. Devuelve 0 bien, -1 si no se pueden comparar.
 */
int met_comparar(const MetMedicion *antes, const MetMedicion *despues,
                 SimMetricasComparacion *comparacion);

void met_liberar(MetMedicion *m);

#endif
