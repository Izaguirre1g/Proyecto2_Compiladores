/*
 * Niveles de optimización -O0 y -O2 desde el IDE (botón "-O0 / -O2").
 *
 * Compila el texto del editor con los dos niveles que ofrece balc, ejecuta
 * los dos binarios en el simulador VLIW y los compara:
 *
 *   -O0 (balc -O0): el backend tradicional, el mismo del botón Ensamblador:
 *       AST -> ensamblador VLIW, sin pasadas de optimización.
 *   -O2 (balc -O2): la ruta de la IR optimizada de los compañeros
 *       (ir_vliw_programa, src/ir_vliw_programa.c): IR de tres direcciones
 *       -> loop unrolling (Meibel) -> renombramiento -> DCE (José,
 *       dce_eliminar_con_observables) -> reordenamiento (Meibel) ->
 *       spilling con 3 registros (Dylan) -> cadena física VLIW (Javier).
 *
 * Para cada nivel se mide: tamaño (bundles y bytes), tiempo de compilación
 * (mediana de MET_COMPILACIONES), ciclos, IPC e instrucciones en el
 * simulador. Además se lee el VALOR FINAL de las variables de main en la
 * memoria del simulador: el resultado de un programa BAL. Si -O2 deja los
 * mismos valores que -O0, la optimización respetó la regla de oro (el
 * programa hace lo mismo).
 *
 * No modifica ningún módulo de los compañeros: solo llama a sus funciones
 * públicas, igual que visitante_o2 en src/Linea_Comandos/cli.c.
 * No usa GTK: se prueba con tests/test_ide_optimizacion.c.
 */
#ifndef IDE_OPTIMIZACION_H
#define IDE_OPTIMIZACION_H

#include <stddef.h>

#include "ide_ensamblador.h"
#include "metricas.h"

/* Cómo terminó el análisis. */
typedef enum {
    IDE_OPT_LISTO = 0,        /* Se intentaron los dos niveles (cada uno dice cómo le fue). */
    IDE_OPT_CON_ERRORES = 1   /* El código tiene errores: hay que corregirlos antes. */
} IdeOptEstado;

/* Lo que se obtuvo con un nivel (-O0 o -O2). */
typedef struct {
    int compilado;            /* 1 si se generó el binario. */
    char *motivo;             /* Si compilado es 0: por qué no (en español). */
    char *ensamblador;        /* El .asm del nivel. */
    size_t bundles;           /* Bundles del programa. */
    IdeAsmTiempos tiempos;    /* Tiempo de compilación por fase (mediana). */
    MetMedicion simulacion;   /* Métricas del simulador hasta que main termina. */
    size_t marco_main;        /* Bytes del marco de main (para leer sus variables). */
    IdeVarMain *variables;    /* Variables locales de main... */
    size_t cantidad_variables;
    char **valores;           /* ...y su valor final, como texto ("120", "[16, 15]"). */
} IdeOptNivel;

typedef struct {
    IdeOptEstado estado;
    IdeOptNivel o0, o2;
    /* Lo que hizo cada pasada de -O2 (lo mismo que imprime "balc -O2"). */
    size_t dce, unroll, renombradas, reordenadas;
    size_t war_eliminadas, waw_eliminadas;   /* Dependencias que quitó el renombramiento. */
    size_t spills, cargas_spill, guardados_spill, generaciones;
    char *ir_antes, *ir_despues, *ir_spilling;   /* La IR en cada etapa de -O2. */
    /* Comparación del resultado de main:
     *   1: las mismas variables con los mismos valores en -O0 y -O2;
     *   0: alguna variable tiene otro valor;
     *  -1: no se pudo comparar (algún nivel no compiló o no terminó). */
    int mismo_resultado;
} IdeOptimizacion;

/*
 * Compila y ejecuta el texto del editor con -O0 y con -O2.
 *   texto, largo: el contenido del editor.
 *   ruta:         la ruta del archivo (para los "bring"), o NULL si no se ha guardado.
 * Devuelve 0 si se pudo intentar (el estado dice cómo terminó) o -1 si
 * falló algo interno (archivo temporal o memoria).
 */
int ide_optimizacion_analizar(const char *texto, size_t largo, const char *ruta,
                              IdeOptimizacion *r);

void ide_optimizacion_liberar(IdeOptimizacion *r);

#endif
