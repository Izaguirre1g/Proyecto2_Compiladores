/*
 * Historial del editor del IDE: Ctrl+Z (deshacer) y Ctrl+Y / Ctrl+Shift+Z
 * (rehacer).
 *
 * El editor de GTK 3 (GtkTextView) no trae deshacer, así que el IDE guarda
 * aquí cada cambio del texto:
 *   - un CAMBIO es "se insertó este texto en esta posición" o "se borró
 *     este texto de esta posición" (posiciones en caracteres, como GTK);
 *   - un PASO es lo que se deshace con un Ctrl+Z: los cambios de una misma
 *     acción de la persona. Por ejemplo, "Corregir" borra un pedazo y
 *     escribe otro: son dos cambios, pero un solo paso.
 *
 * Para no deshacer letra por letra, las letras escritas seguidas se unen en
 * un solo paso (toda la palabra), igual que en los editores comunes. Un
 * espacio, un símbolo, un salto de línea o mover el cursor empiezan otro
 * paso. Lo mismo al borrar con Retroceso o Supr letra por letra.
 *
 * Deshacer NO aplica nada: devuelve el paso, y quien llama (ide_gtk.c)
 * aplica sus cambios al revés y en orden inverso. Rehacer los aplica tal
 * cual, en orden. Así este módulo no depende de GTK y se prueba con
 * tests/test_ide_historial.c.
 *
 * Además recuerda en qué paso se guardó el archivo: si se deshace hasta
 * ahí, el archivo vuelve a estar "sin cambios" (sin el * del título).
 */
#ifndef IDE_HISTORIAL_H
#define IDE_HISTORIAL_H

#include <stddef.h>

/* Máximo de pasos que se recuerdan; los más viejos se olvidan. */
#define IDE_HISTORIAL_MAXIMO 1000

typedef enum {
    IDE_HIST_INSERTAR = 0,  /* Se escribió "texto" en "posicion". */
    IDE_HIST_BORRAR = 1     /* Se borró "texto", que empezaba en "posicion". */
} IdeHistTipo;

/* Un cambio del texto. */
typedef struct {
    IdeHistTipo tipo;
    size_t posicion;    /* En caracteres desde el inicio del texto (no en bytes). */
    size_t caracteres;  /* Largo de "texto" en caracteres. */
    char *texto;        /* UTF-8, terminado en '\0'. */
} IdeHistCambio;

/* Un paso: los cambios de una acción, en el orden en que pasaron. */
typedef struct {
    IdeHistCambio *cambios;
    size_t cantidad;
    size_t capacidad;
} IdeHistPaso;

/* Se crea vacío así: IdeHistorial h = {0}; */
typedef struct {
    IdeHistPaso *pasos;
    size_t cantidad;     /* Pasos guardados (deshechos incluidos). */
    size_t capacidad;
    size_t actual;       /* Pasos aplicados: Ctrl+Z deshace pasos[actual - 1]
                          * y Ctrl+Y rehace pasos[actual]. */
    size_t guardado;     /* Valor de "actual" cuando se guardó el archivo;
                          * SIZE_MAX si ese estado ya no se puede alcanzar. */
    IdeHistPaso abierto; /* El paso que se está armando (acción sin terminar). */
    unsigned profundidad;  /* Acciones abiertas (pueden anidarse). */
    int unir_permitido;  /* 0 justo después de deshacer o rehacer: lo que se
                          * escriba después empieza un paso nuevo. */
} IdeHistorial;

/* Olvida todo (por ejemplo, al abrir otro archivo). El texto actual queda
 * como "guardado". */
void ide_historial_vaciar(IdeHistorial *h);

/* Empieza y termina una acción de la persona (las señales
 * "begin-user-action" y "end-user-action" de GTK). Los cambios entre las
 * dos forman un solo paso. Devuelven 0 bien o -1 sin memoria (entonces el
 * historial se vacía: se pierde, pero nunca queda mal). */
int ide_historial_empezar(IdeHistorial *h);
int ide_historial_terminar(IdeHistorial *h);

/* Anota que se insertó o se borró "texto" ("bytes" bytes; -1 si termina en
 * '\0') en la posición "posicion" (en caracteres). Fuera de una acción, el
 * cambio es un paso él solo. 0 bien, -1 sin memoria (el historial se vacía). */
int ide_historial_insertar(IdeHistorial *h, size_t posicion, const char *texto, long bytes);
int ide_historial_borrar(IdeHistorial *h, size_t posicion, const char *texto, long bytes);

/* El paso que hay que DESHACER (aplicar al revés, del último cambio al
 * primero), o NULL si no hay nada que deshacer. */
const IdeHistPaso *ide_historial_deshacer(IdeHistorial *h);

/* El paso que hay que REHACER (aplicar tal cual, en orden), o NULL. */
const IdeHistPaso *ide_historial_rehacer(IdeHistorial *h);

/* El archivo se acaba de guardar: este es el estado "sin cambios". */
void ide_historial_marcar_guardado(IdeHistorial *h);

/* 1 si el texto está igual que cuando se guardó (o se abrió). */
int ide_historial_en_guardado(const IdeHistorial *h);

#endif
