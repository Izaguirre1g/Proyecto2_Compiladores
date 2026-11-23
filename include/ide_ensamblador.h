/*
 * Ensamblador del código del editor (botón "Ensamblador" del IDE).
 *
 * Genera el MISMO ensamblador que "balc -s", pero desde el texto del editor
 * (aunque no esté guardado) y sin escribir archivos:
 *
 *   análisis semántico -> memoria (mem_asignar_proyecto)
 *   -> generación y enlace con el arranque (ef_enlazar)
 *   -> codificación de cada bundle a binario (cb_ensamblar)
 *
 * Usa el mismo mapa de memoria que balc (include/cli.h), así que el texto
 * es idéntico al .asm que genera "balc -s". La prueba
 * tests/test_ide_ensamblador.c lo compara con examples/factorial.asm.
 *
 * No usa GTK: así se puede probar sin abrir ventanas.
 */
#ifndef IDE_ENSAMBLADOR_H
#define IDE_ENSAMBLADOR_H

#include <stddef.h>

/* Cómo terminó. */
typedef enum {
    IDE_ASM_LISTO = 0,        /* Se generó el ensamblador. */
    IDE_ASM_CON_ERRORES = 1,  /* El código tiene errores: hay que corregirlos antes. */
    IDE_ASM_SIN_BACKEND = 2   /* El código es válido, pero el backend no lo pudo
                               * generar (ver "motivo"). */
} IdeAsmEstado;

/*
 * Tiempo de compilación, en milisegundos, por fase. Se mide con el reloj
 * monótono del sistema (no cambia si alguien ajusta la hora).
 *   analisis:     léxico + sintáctico + semántico (sem_visitar_proyecto hace
 *                 las tres fases juntas, incluidos los módulos de "bring").
 *   generacion:   asignar la memoria, generar el ensamblador y enlazarlo
 *                 con el arranque (el backend).
 *   codificacion: pasar cada bundle a binario y armar el .bin (BALB).
 *   total:        todo, incluido escribir y borrar el archivo temporal.
 * Si el código tiene errores, solo "analisis" y "total" tienen valor.
 */
typedef struct {
    double analisis;
    double generacion;
    double codificacion;
    double total;
} IdeAsmTiempos;

/*
 * Una variable local de main, para leer su valor en la memoria del
 * simulador cuando main termina. El resultado de un programa BAL es el
 * valor final de las variables de main (BAL no tiene print), así que con
 * esto se comprueba que dos binarios (por ejemplo -O0 y -O2) hacen lo mismo.
 * Su dirección es: tope de la pila (0x10000) - marco de main + desplazamiento.
 */
typedef struct {
    char nombre[64];
    long desplazamiento;     /* Desde sp dentro del marco de main. */
    size_t elementos;        /* 1 si es escalar; si es arreglo, cuántos elementos. */
    size_t bytes_elemento;   /* 4 (int, boolean) o 1 (char). */
    int es_boolean;          /* Para escribir declare_true / declare_false. */
    int es_char;             /* Para escribir el carácter. */
} IdeVarMain;

/* Llena la lista de variables locales de main de una tabla de símbolos con
 * la memoria ya asignada (int, boolean y char; escalares y arreglos). No
 * incluye text ni listas: su valor no está en el marco. Devuelve 0 bien o
 * -1 sin memoria. Se libera con free(*variables). */
struct SimTabla;
int ide_variables_de_main(const struct SimTabla *tabla, IdeVarMain **variables, size_t *cantidad);

typedef struct {
    IdeAsmEstado estado;
    IdeAsmTiempos tiempos; /* Cuánto tardó la compilación (ver arriba). */
    size_t marco_main;     /* Bytes del marco de main (0 si no se generó). */
    IdeVarMain *variables; /* Variables locales de main (ver arriba). */
    size_t cantidad_variables;
    char *motivo;          /* Con IDE_ASM_SIN_BACKEND: lo que dijo el backend. */
    char *ensamblador;     /* El texto del .asm (bundles: 4 slots separados por '|'). */
    char *binario;         /* Cada bundle con su codificación (como "balc -p"). */
    unsigned char *balb;   /* El .bin completo (BALB: cabecera, código y datos de las
                            * globales), byte por byte igual al de "balc". Lo usa
                            * el simulador para las métricas (src/Metricas/). */
    size_t largo_balb;     /* Bytes de "balb" (puede tener ceros: no es texto). */
    size_t bundles;        /* Cantidad de bundles. */
    size_t funciones;      /* Funciones generadas. */
    size_t saltos;         /* Saltos resueltos. */
    size_t reescritos;     /* Saltos reescritos por alcance (condicional + salto largo). */
} IdeEnsamblador;

/*
 * Genera el ensamblador del texto del editor.
 *   texto, largo: el contenido del editor.
 *   ruta:         la ruta del archivo (para los "bring"), o NULL si no se
 *                 ha guardado.
 * Devuelve 0 si se pudo intentar (el estado dice cómo terminó) o -1 si
 * falló algo interno (archivo temporal o memoria).
 */
int ide_ensamblador_generar(const char *texto, size_t largo, const char *ruta,
                            IdeEnsamblador *salida);

void ide_ensamblador_liberar(IdeEnsamblador *asm_ide);

#endif
