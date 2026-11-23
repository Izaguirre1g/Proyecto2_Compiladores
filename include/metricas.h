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
    uint32_t ancho_fin;       /* Bytes después de direccion_fin que recorre el ciclo
                               * final: 32 en -O0 (3 bundles) y 64 en -O2 (5 bundles).
                               * 0 = MET_ANCHO_FIN_O0. */
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

/* El ciclo final de -O0: el PC pasa por fin, fin+16 y fin+32. */
#define MET_ANCHO_FIN_O0 32u

/* Bytes de la memoria de datos del simulador (64 KB). */
#define MET_BYTES_MEMORIA 65536u

/*
 * Ejecuta un binario BALB ya armado (pasos 3 a 5 de met_medir), para medir
 * un binario que no sale del botón Ensamblador, como el de -O2.
 *   balb, largo_balb: el .bin completo.
 *   ruta:     la del archivo del editor (el .bin temporal va a su lado), o NULL.
 *   memoria:  si no es NULL, MET_BYTES_MEMORIA bytes donde se copia la
 *             memoria de datos en el ciclo en que main termina.
 *   m:        antes de llamar se llenan m->direccion_fin (la dirección del
 *             ciclo final; ver met_buscar_fin) y, si se quiere, m->presupuesto.
 *             Deja el estado, el motivo y las métricas del simulador.
 * Devuelve 0 si se pudo intentar (el estado dice cómo terminó) o -1 si
 * falló algo interno.
 */
int met_simular(const unsigned char *balb, size_t largo_balb, const char *ruta,
                uint8_t *memoria, MetMedicion *m);

/* La dirección del salto final del arranque de -O0 ("sye x0,0") en un
 * ensamblador. 0 si la encontró, -1 si no. */
int met_buscar_fin(const char *ensamblador, uint32_t *direccion);

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
