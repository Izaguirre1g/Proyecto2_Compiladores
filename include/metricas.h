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

#include "sim_ejecucion.h"

/* Presupuesto de ciclos si no se pide otro: si el programa no termina antes,
 * se considera que no terminó (probablemente tiene un ciclo infinito). */
#define MET_PRESUPUESTO_DEFECTO 5000000u

/* 128 bits de instrucciones por bundle, incluidos NOP y guardas emitidas. */
#define MET_BYTES_BUNDLE 16u

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

/* Tamaño estático de instrucciones: bundles emitidos * 16. No mide el BALB
 * completo ni memoria de datos. Acepta cero bundles; devuelve -1 si bytes es
 * NULL o si el producto excede SIZE_MAX, sin modificar la salida. Es puro:
 * un futuro consumidor O0/O2 debe pasar el conteo emitido por CADA variante. */
int met_calcular_bytes_codigo(size_t bundles, size_t *bytes);

/* Obtiene el tamaño de una medición LISTO o SIN_TERMINAR. La disponibilidad
 * del código es independiente del presupuesto dinámico; errores y ausencia
 * de backend devuelven -1 sin publicar un cero que parezca una medición. */
int met_tamano_codigo(const MetMedicion *m, size_t *bytes);

typedef struct {
    size_t original_bytes;
    size_t optimizado_bytes;
    size_t diferencia_bytes;       /* Magnitud absoluta, sin resta unsigned negativa. */
    int reduccion;                 /* 1 si optimizado < original. */
    int porcentaje_definido;       /* 0 cuando el original tiene cero bytes. */
    double variacion_porcentual;   /* 100*(optimizado-original)/original: negativa al reducir. */
} MetComparacionTamano;

/* Compara únicamente tamaños estáticos de dos mediciones con código disponible;
 * no exige igual presupuesto ni consulta sus métricas de ejecución. Devuelve
 * 0 en éxito, -1 ante argumentos/mediciones inválidos u overflow; la salida
 * queda intacta ante error. No reserva memoria ni establece un nivel O2. */
int met_comparar_tamano_codigo(const MetMedicion *antes, const MetMedicion *despues,
                              MetComparacionTamano *comparacion);

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
