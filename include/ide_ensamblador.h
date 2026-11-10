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

typedef struct {
    IdeAsmEstado estado;
    char *motivo;          /* Con IDE_ASM_SIN_BACKEND: lo que dijo el backend. */
    char *ensamblador;     /* El texto del .asm (bundles: 4 slots separados por '|'). */
    char *binario;         /* Cada bundle con su codificación (como "balc -p"). */
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
