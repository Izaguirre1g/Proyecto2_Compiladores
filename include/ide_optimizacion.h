/*
 * Niveles de optimización -O0, -O1 y -O2 desde el IDE (botón "Niveles -O").
 *
 * Compila el texto del editor con los tres niveles que ofrece balc, ejecuta
 * los tres binarios en el simulador VLIW y los compara:
 *
 *   -O0 (balc -O0): el backend tradicional, el mismo del botón Ensamblador:
 *       AST -> ensamblador VLIW, sin pasadas de optimización.
 *   -O1 (balc -O1): el DCE de José sobre la IR (ide_dce_analizar), llevado
 *       de vuelta al código BAL (el código sin el código muerto), que se
 *       compila con el mismo backend tradicional. Si el DCE no elimina nada,
 *       se compila el código original. Es lo mismo que hace
 *       vliw_opt_fuente_preparar en tools/vliw/optimizacion_cli.c.
 *   -O2 (balc -O2): la ruta de la IR optimizada de los compañeros
 *       (ir_vliw_programa, src/ir_vliw_programa.c): IR de tres direcciones
 *       -> loop unrolling (Meibel) -> renombramiento -> DCE (José,
 *       dce_eliminar_con_observables) -> reordenamiento (Meibel) ->
 *       spilling con 3 registros (Dylan) -> cadena física VLIW (Javier).
 *
 * Para cada nivel se mide: tamaño (bundles y bytes), tiempo de compilación
 * (mediana de MET_COMPILACIONES), ciclos, IPC e instrucciones en el
 * simulador. Además se lee el VALOR FINAL de las variables de main en la
 * memoria del simulador: el resultado de un programa BAL. Si -O1 y -O2
 * dejan los mismos valores que -O0, las optimizaciones respetaron la regla
 * de oro (el programa hace lo mismo).
 *
 * Una variable de main que -O1 ya no tiene NO es una diferencia: el DCE la
 * quitó porque nadie la lee (es código muerto, no un resultado). Se compara
 * por nombre solo lo que existe en los dos niveles.
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

/* Lo que se obtuvo con un nivel (-O0, -O1 o -O2). */
typedef struct {
    int compilado;            /* 1 si se generó el binario. */
    char *motivo;             /* Si compilado es 0: por qué no (en español). */
    char *ensamblador;        /* El .asm del nivel. */
    size_t bundles;           /* Bundles del programa. */
    size_t instrucciones;     /* Instrucciones del programa sin contar los NOP (conteo
                               * estático, en el ensamblador: lo que ocupa código). */
    IdeAsmTiempos tiempos;    /* Tiempo de compilación por fase (mediana). */
    MetMedicion simulacion;   /* Métricas del simulador hasta que main termina. */
    size_t marco_main;        /* Bytes del marco de main (para leer sus variables). */
    IdeVarMain *variables;    /* Variables locales de main... */
    size_t cantidad_variables;
    char **valores;           /* ...y su valor final, como texto ("120", "[16, 15]"). */
} IdeOptNivel;

typedef struct {
    IdeOptEstado estado;
    IdeOptNivel o0, o1, o2;
    /* Lo que hizo el DCE de -O1 (lo mismo que imprime "balc -O1"). */
    size_t o1_eliminadas_ir;        /* Instrucciones de la IR eliminadas. */
    size_t o1_borradas, o1_cambiadas, o1_conservadas;  /* Sentencias del código BAL. */
    int o1_cambio;                  /* 1 si el código que se compiló es distinto del original. */
    char *o1_codigo;                /* El código BAL que compila -O1 (sin el código muerto). */
    /* Instrucciones de la IR antes y después de optimizar (conteo estático). */
    size_t o1_ir_antes, o1_ir_despues;           /* -O1: antes y después del DCE. */
    size_t o2_ir_antes, o2_ir_despues;           /* -O2: antes y después de las pasadas 1 a 4. */
    size_t o2_ir_spilling;                       /* -O2: después del spilling (pasada 5). */
    /* Lo que hizo cada pasada de -O2 (lo mismo que imprime "balc -O2"). */
    size_t dce, unroll, renombradas, reordenadas;
    size_t war_eliminadas, waw_eliminadas;   /* Dependencias que quitó el renombramiento. */
    size_t spills, cargas_spill, guardados_spill, generaciones;
    char *ir_antes, *ir_despues, *ir_spilling;   /* La IR en cada etapa de -O2. */
    /* Comparación del resultado de main con el de -O0 (1 igual, 0 alguna
     * variable con otro valor, -1 no se pudo: el nivel no compiló o no terminó). */
    int mismo_o1, mismo_o2;
    /* Los tres juntos: 1 si -O1 y -O2 dan lo mismo que -O0; 0 si alguno da
     * otra cosa; -1 si alguno no se pudo comparar. */
    int mismo_resultado;
} IdeOptimizacion;

/*
 * Compila y ejecuta el texto del editor con -O0, -O1 y -O2.
 *   texto, largo: el contenido del editor.
 *   ruta:         la ruta del archivo (para los "bring"), o NULL si no se ha guardado.
 * Devuelve 0 si se pudo intentar (el estado dice cómo terminó) o -1 si
 * falló algo interno (archivo temporal o memoria).
 */
int ide_optimizacion_analizar(const char *texto, size_t largo, const char *ruta,
                              IdeOptimizacion *r);

void ide_optimizacion_liberar(IdeOptimizacion *r);

#endif
