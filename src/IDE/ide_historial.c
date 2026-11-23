/*
 * Historial del editor: deshacer y rehacer (ver include/ide_historial.h).
 *
 * Cómo se guarda:
 *   pasos[0] ... pasos[actual - 1]       -> aplicados (Ctrl+Z los deshace)
 *   pasos[actual] ... pasos[cantidad - 1] -> deshechos (Ctrl+Y los rehace)
 * Un cambio nuevo después de deshacer borra los pasos deshechos: ya no se
 * pueden rehacer, porque el texto tomó otro camino.
 */
#include "ide_historial.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */

/* Cuántos caracteres UTF-8 hay en los primeros "bytes" bytes: se cuentan
 * los bytes que NO son de continuación (los 10xxxxxx). */
static size_t contar_caracteres(const char *texto, size_t bytes)
{
    size_t n = 0;
    for (size_t i = 0; i < bytes; ++i)
        if (((unsigned char)texto[i] & 0xC0u) != 0x80u) ++n;
    return n;
}

/* ¿Es parte de una palabra? Letras, dígitos, '_' y cualquier carácter que
 * no sea ASCII (á, ñ...). Solo las palabras se unen en un paso. */
static int es_de_palabra(const char *c)
{
    unsigned char b = (unsigned char)c[0];
    return b >= 0x80u || b == '_' || (b >= '0' && b <= '9') || (b >= 'a' && b <= 'z') ||
           (b >= 'A' && b <= 'Z');
}

/* ¿El texto es un solo carácter de palabra? */
static int una_letra(const IdeHistCambio *c)
{
    return c->caracteres == 1 && es_de_palabra(c->texto);
}

/* Libera los cambios de un paso y lo deja vacío. */
static void liberar_paso(IdeHistPaso *p)
{
    for (size_t i = 0; i < p->cantidad; ++i) free(p->cambios[i].texto);
    free(p->cambios);
    p->cambios = NULL;
    p->cantidad = p->capacidad = 0;
}

/* Libera los pasos desde "desde" hasta el final. */
static void liberar_desde(IdeHistorial *h, size_t desde)
{
    for (size_t i = desde; i < h->cantidad; ++i) liberar_paso(&h->pasos[i]);
    h->cantidad = desde;
}

void ide_historial_vaciar(IdeHistorial *h)
{
    if (h == NULL) return;
    liberar_desde(h, 0);
    free(h->pasos);
    liberar_paso(&h->abierto);
    h->pasos = NULL;
    h->capacidad = 0;
    h->actual = 0;
    h->guardado = 0;  /* El texto de ahora es el "sin cambios". */
    h->profundidad = 0;
    h->unir_permitido = 1;
}

/* Si falta memoria se vacía todo: perder el historial es mejor que dejarlo
 * con pasos a medias, que al deshacer dañarían el texto. */
static int sin_memoria(IdeHistorial *h)
{
    unsigned profundidad = h->profundidad;
    ide_historial_vaciar(h);
    h->guardado = SIZE_MAX;  /* Ya no se sabe si el texto es el guardado. */
    h->profundidad = profundidad;  /* Las acciones abiertas siguen abiertas. */
    return -1;
}

/* ------------------------------------------------------------------ */
/* Armar los pasos                                                     */
/* ------------------------------------------------------------------ */

/* Agrega un cambio al paso abierto. 0 bien, -1 sin memoria. */
static int agregar_cambio(IdeHistPaso *p, IdeHistTipo tipo, size_t posicion, const char *texto,
                          size_t bytes)
{
    if (p->cantidad == p->capacidad) {
        size_t nueva = p->capacidad ? p->capacidad * 2 : 4;
        IdeHistCambio *c = realloc(p->cambios, nueva * sizeof *c);
        if (c == NULL) return -1;
        p->cambios = c;
        p->capacidad = nueva;
    }
    char *copia = malloc(bytes + 1);
    if (copia == NULL) return -1;
    memcpy(copia, texto, bytes);
    copia[bytes] = '\0';
    p->cambios[p->cantidad++] = (IdeHistCambio){tipo, posicion, contar_caracteres(texto, bytes), copia};
    return 0;
}

/* Junta "b" al final de "a" (o al inicio si "al_inicio"). 0 bien, -1 sin memoria. */
static int juntar_texto(IdeHistCambio *a, const IdeHistCambio *b, int al_inicio)
{
    size_t la = strlen(a->texto), lb = strlen(b->texto);
    char *t = malloc(la + lb + 1);
    if (t == NULL) return -1;
    if (al_inicio) {
        memcpy(t, b->texto, lb);
        memcpy(t + lb, a->texto, la + 1);
    } else {
        memcpy(t, a->texto, la);
        memcpy(t + la, b->texto, lb + 1);
    }
    free(a->texto);
    a->texto = t;
    a->caracteres += b->caracteres;
    return 0;
}

/*
 * Intenta unir el paso nuevo "n" con el último paso aplicado "p", para que
 * una palabra se deshaga entera y no letra por letra. Se unen si los dos
 * tienen UN cambio del mismo tipo, el nuevo es una sola letra de palabra,
 * y está pegado al anterior:
 *   escribir: "su" en 10 y luego "m" en 12      -> "sum" en 10
 *   Retroceso: "a" en 7 y luego "m" en 6        -> "ma" en 6
 *   Supr:     "a" en 7 y luego "b" en 7         -> "ab" en 7
 * Devuelve 1 si se unió, 0 si no, -1 sin memoria.
 */
static int unir(IdeHistPaso *p, const IdeHistPaso *n)
{
    if (p->cantidad != 1 || n->cantidad != 1) return 0;
    IdeHistCambio *a = &p->cambios[0];
    const IdeHistCambio *b = &n->cambios[0];
    if (a->tipo != b->tipo || !una_letra(b) || !es_de_palabra(a->texto)) return 0;
    if (a->tipo == IDE_HIST_INSERTAR) {
        /* La última letra escrita también tiene que ser de palabra. */
        const char *ultima = a->texto + strlen(a->texto) - 1;
        while (ultima > a->texto && ((unsigned char)*ultima & 0xC0u) == 0x80u) --ultima;
        if (!es_de_palabra(ultima) || b->posicion != a->posicion + a->caracteres) return 0;
        return juntar_texto(a, b, 0) == 0 ? 1 : -1;
    }
    if (b->posicion + 1 == a->posicion) {          /* Retroceso: la letra de antes. */
        if (juntar_texto(a, b, 1) != 0) return -1;
        a->posicion = b->posicion;
        return 1;
    }
    if (b->posicion == a->posicion) return juntar_texto(a, b, 0) == 0 ? 1 : -1;  /* Supr. */
    return 0;
}

/* Cierra el paso abierto: lo une al anterior o lo agrega como paso nuevo. */
static int cerrar_paso(IdeHistorial *h)
{
    IdeHistPaso nuevo = h->abierto;
    h->abierto = (IdeHistPaso){0};
    if (nuevo.cantidad == 0) {
        liberar_paso(&nuevo);
        return 0;
    }
    /* Lo deshecho ya no se puede rehacer: el texto tomó otro camino. */
    if (h->actual < h->cantidad) {
        if (h->guardado != SIZE_MAX && h->guardado > h->actual) h->guardado = SIZE_MAX;
        liberar_desde(h, h->actual);
    }
    /* No se une con un paso ya guardado: si no, deshacer volvería a un
     * texto distinto del que está en el disco. */
    if (h->unir_permitido && h->actual > 0 && h->actual != h->guardado) {
        int r = unir(&h->pasos[h->actual - 1], &nuevo);
        if (r != 0) {
            liberar_paso(&nuevo);
            return r < 0 ? sin_memoria(h) : 0;
        }
    }
    /* Si ya hay demasiados, se olvida el más viejo. */
    if (h->cantidad == IDE_HISTORIAL_MAXIMO) {
        liberar_paso(&h->pasos[0]);
        memmove(h->pasos, h->pasos + 1, (h->cantidad - 1) * sizeof *h->pasos);
        h->cantidad--;
        h->actual--;
        h->guardado = (h->guardado == 0 || h->guardado == SIZE_MAX) ? SIZE_MAX : h->guardado - 1;
    }
    if (h->cantidad == h->capacidad) {
        size_t cap = h->capacidad ? h->capacidad * 2 : 16;
        IdeHistPaso *p = realloc(h->pasos, cap * sizeof *p);
        if (p == NULL) {
            liberar_paso(&nuevo);
            return sin_memoria(h);
        }
        h->pasos = p;
        h->capacidad = cap;
    }
    h->pasos[h->cantidad++] = nuevo;
    h->actual = h->cantidad;
    h->unir_permitido = 1;
    return 0;
}

int ide_historial_empezar(IdeHistorial *h)
{
    if (h == NULL) return -1;
    h->profundidad++;
    return 0;
}

int ide_historial_terminar(IdeHistorial *h)
{
    if (h == NULL || h->profundidad == 0) return -1;
    if (--h->profundidad > 0) return 0;  /* Era una acción dentro de otra. */
    return cerrar_paso(h);
}

/* Anota un cambio; fuera de una acción, es un paso él solo. */
static int anotar(IdeHistorial *h, IdeHistTipo tipo, size_t posicion, const char *texto, long bytes)
{
    if (h == NULL || texto == NULL) return -1;
    size_t n = bytes < 0 ? strlen(texto) : (size_t)bytes;
    if (n == 0) return 0;
    int suelto = h->profundidad == 0;
    if (suelto) h->profundidad = 1;
    if (agregar_cambio(&h->abierto, tipo, posicion, texto, n) != 0) {
        if (suelto) h->profundidad = 0;
        return sin_memoria(h);
    }
    if (suelto) return ide_historial_terminar(h);
    return 0;
}

int ide_historial_insertar(IdeHistorial *h, size_t posicion, const char *texto, long bytes)
{
    return anotar(h, IDE_HIST_INSERTAR, posicion, texto, bytes);
}

int ide_historial_borrar(IdeHistorial *h, size_t posicion, const char *texto, long bytes)
{
    return anotar(h, IDE_HIST_BORRAR, posicion, texto, bytes);
}

/* ------------------------------------------------------------------ */
/* Deshacer, rehacer y guardado                                        */
/* ------------------------------------------------------------------ */

const IdeHistPaso *ide_historial_deshacer(IdeHistorial *h)
{
    if (h == NULL || h->actual == 0 || h->profundidad > 0) return NULL;
    h->unir_permitido = 0;  /* Lo que se escriba después es otro paso. */
    return &h->pasos[--h->actual];
}

const IdeHistPaso *ide_historial_rehacer(IdeHistorial *h)
{
    if (h == NULL || h->actual >= h->cantidad || h->profundidad > 0) return NULL;
    h->unir_permitido = 0;
    return &h->pasos[h->actual++];
}

void ide_historial_marcar_guardado(IdeHistorial *h)
{
    if (h != NULL) h->guardado = h->actual;
}

int ide_historial_en_guardado(const IdeHistorial *h)
{
    return h != NULL && h->guardado == h->actual;
}
