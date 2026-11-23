/*
 * Pruebas del historial del editor (src/IDE/ide_historial.c): Ctrl+Z y
 * Ctrl+Y sin abrir ventanas.
 *
 * Se simula el editor con una cadena: cada cambio se aplica a la cadena Y
 * se anota en el historial, como hacen las señales de GTK en ide_gtk.c.
 * Después se deshace y se rehace aplicando los pasos que devuelve el
 * historial, y se revisa que el texto quede exactamente como antes.
 *
 * Se ejecuta con: make test-ide-historial
 */
#include "ide_historial.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cuántas verificaciones fallaron. */
static int fallos = 0;

/* Si la condición es falsa, muestra la línea que falló y cuenta el fallo. */
#define VERIFICAR(condicion) do { \
    if (!(condicion)) { \
        fprintf(stderr, "FALLO en la linea %d: %s\n", __LINE__, #condicion); \
        fallos++; \
    } \
} while (0)

/* El "editor": el texto y su historial. */
static char texto[4096];
static IdeHistorial h;

/* Byte donde empieza el carácter número "pos" (UTF-8). */
static size_t byte_de(size_t pos)
{
    size_t b = 0, n = 0;
    while (texto[b] != '\0' && n < pos) {
        ++b;
        while (((unsigned char)texto[b] & 0xC0u) == 0x80u) ++b;
        ++n;
    }
    return b;
}

/* Inserta "t" en el carácter "pos" del texto (sin anotar). */
static void poner(size_t pos, const char *t)
{
    size_t b = byte_de(pos), l = strlen(t);
    memmove(texto + b + l, texto + b, strlen(texto + b) + 1);
    memcpy(texto + b, t, l);
}

/* Borra "caracteres" caracteres desde "pos" (sin anotar). */
static void quitar(size_t pos, size_t caracteres)
{
    size_t b = byte_de(pos), f = byte_de(pos + caracteres);
    memmove(texto + b, texto + f, strlen(texto + f) + 1);
}

/* Lo que hace la persona: escribir o borrar, como una acción de GTK. */
static void escribir(size_t pos, const char *t)
{
    ide_historial_empezar(&h);
    ide_historial_insertar(&h, pos, t, -1);
    poner(pos, t);
    ide_historial_terminar(&h);
}

static void borrar(size_t pos, size_t caracteres)
{
    char copia[256];
    size_t b = byte_de(pos), f = byte_de(pos + caracteres);
    memcpy(copia, texto + b, f - b);
    copia[f - b] = '\0';
    ide_historial_empezar(&h);
    ide_historial_borrar(&h, pos, copia, -1);
    quitar(pos, caracteres);
    ide_historial_terminar(&h);
}

/* Escribe una palabra letra por letra, como al teclear. */
static void teclear(size_t pos, const char *palabra)
{
    for (size_t i = 0; palabra[i] != '\0'; ++i) {
        char letra[2] = {palabra[i], '\0'};
        escribir(pos + i, letra);
    }
}

/* Ctrl+Z: aplica el paso al revés, del último cambio al primero. */
static int deshacer(void)
{
    const IdeHistPaso *p = ide_historial_deshacer(&h);
    if (p == NULL) return 0;
    for (size_t i = p->cantidad; i-- > 0;) {
        const IdeHistCambio *c = &p->cambios[i];
        if (c->tipo == IDE_HIST_INSERTAR) quitar(c->posicion, c->caracteres);
        else poner(c->posicion, c->texto);
    }
    return 1;
}

/* Ctrl+Y: aplica el paso tal cual, en orden. */
static int rehacer(void)
{
    const IdeHistPaso *p = ide_historial_rehacer(&h);
    if (p == NULL) return 0;
    for (size_t i = 0; i < p->cantidad; ++i) {
        const IdeHistCambio *c = &p->cambios[i];
        if (c->tipo == IDE_HIST_INSERTAR) poner(c->posicion, c->texto);
        else quitar(c->posicion, c->caracteres);
    }
    return 1;
}

/* Empieza otra prueba con "inicial" en el editor (como abrir un archivo). */
static void abrir(const char *inicial)
{
    strcpy(texto, inicial);
    ide_historial_vaciar(&h);
}

/* Una palabra escrita letra por letra se deshace entera; el espacio y el
 * símbolo que siguen son pasos aparte. */
static void prueba_palabras(void)
{
    abrir("");
    teclear(0, "suma");
    teclear(4, " ");
    teclear(5, "total");
    teclear(10, ";");
    VERIFICAR(strcmp(texto, "suma total;") == 0);
    VERIFICAR(h.cantidad == 4);  /* "suma", " ", "total", ";" */
    VERIFICAR(deshacer() && strcmp(texto, "suma total") == 0);
    VERIFICAR(deshacer() && strcmp(texto, "suma ") == 0);
    VERIFICAR(deshacer() && strcmp(texto, "suma") == 0);
    VERIFICAR(deshacer() && strcmp(texto, "") == 0);
    VERIFICAR(!deshacer());  /* No hay nada más. */
    /* Rehacer todo deja el texto como estaba. */
    while (rehacer()) {}
    VERIFICAR(strcmp(texto, "suma total;") == 0);
    VERIFICAR(!rehacer());
}

/* Mover el cursor (escribir en otro lugar) empieza un paso nuevo. */
static void prueba_otro_lugar(void)
{
    abrir("x : 1;");
    teclear(4, "2");      /* "x : 21;" */
    teclear(0, "y");      /* No está pegado al anterior: otro paso. */
    VERIFICAR(strcmp(texto, "yx : 21;") == 0);
    VERIFICAR(h.cantidad == 2);
    VERIFICAR(deshacer() && strcmp(texto, "x : 21;") == 0);
}

/* Retroceso y Supr letra por letra también se unen. */
static void prueba_borrar(void)
{
    abrir("contador : 0;");
    for (int i = 0; i < 3; ++i) borrar(7 - (size_t)i, 1);  /* Retroceso: "dor" */
    VERIFICAR(strcmp(texto, "conta : 0;") == 0);
    VERIFICAR(h.cantidad == 1);
    borrar(0, 1);  /* Supr en otro lugar: otro paso. */
    borrar(0, 1);
    VERIFICAR(strcmp(texto, "nta : 0;") == 0);
    VERIFICAR(h.cantidad == 2);
    VERIFICAR(deshacer() && strcmp(texto, "conta : 0;") == 0);
    VERIFICAR(deshacer() && strcmp(texto, "contador : 0;") == 0);
}

/* Una acción con varios cambios (como "Corregir": borra y escribe) es UN paso. */
static void prueba_accion_compuesta(void)
{
    abrir("x : (1 gauss 2];");
    ide_historial_empezar(&h);
    ide_historial_borrar(&h, 14, "]", -1);
    quitar(14, 1);
    ide_historial_insertar(&h, 14, ")", -1);
    poner(14, ")");
    ide_historial_terminar(&h);
    VERIFICAR(strcmp(texto, "x : (1 gauss 2);") == 0);
    VERIFICAR(h.cantidad == 1 && h.pasos[0].cantidad == 2);
    VERIFICAR(deshacer() && strcmp(texto, "x : (1 gauss 2];") == 0);
    VERIFICAR(rehacer() && strcmp(texto, "x : (1 gauss 2);") == 0);
}

/* Escribir después de deshacer borra lo que se podía rehacer, y no se une
 * con el paso anterior. */
static void prueba_nuevo_camino(void)
{
    abrir("");
    teclear(0, "abc");
    teclear(3, " ");
    VERIFICAR(deshacer() && strcmp(texto, "abc") == 0);
    teclear(3, "d");  /* Después de deshacer: paso nuevo, aunque esté pegado. */
    VERIFICAR(strcmp(texto, "abcd") == 0);
    VERIFICAR(!rehacer());  /* El " " deshecho ya no se puede rehacer. */
    VERIFICAR(h.cantidad == 2);
    VERIFICAR(deshacer() && strcmp(texto, "abc") == 0);
}

/* Guardar: deshacer hasta ese punto deja el archivo "sin cambios". */
static void prueba_guardado(void)
{
    abrir("main");
    VERIFICAR(ide_historial_en_guardado(&h));
    teclear(4, "1");
    VERIFICAR(!ide_historial_en_guardado(&h));
    ide_historial_marcar_guardado(&h);       /* Ctrl+S con "main1". */
    VERIFICAR(ide_historial_en_guardado(&h));
    teclear(5, "2");                          /* No se une con el paso guardado. */
    VERIFICAR(h.cantidad == 2);
    VERIFICAR(deshacer() && ide_historial_en_guardado(&h) && strcmp(texto, "main1") == 0);
    VERIFICAR(deshacer() && !ide_historial_en_guardado(&h) && strcmp(texto, "main") == 0);
    /* Si después de deshacer se escribe otra cosa, el estado guardado ya no
     * se puede alcanzar. */
    teclear(4, "x");
    VERIFICAR(!ide_historial_en_guardado(&h));
    VERIFICAR(deshacer() && !ide_historial_en_guardado(&h));
}

/* Las posiciones son caracteres, no bytes: las tildes ocupan 2 bytes. */
static void prueba_tildes(void)
{
    abrir("%% año");
    teclear(6, "s");
    escribir(3, "ñ");
    VERIFICAR(strcmp(texto, "%% ñaños") == 0);
    VERIFICAR(h.pasos[1].cambios[0].caracteres == 1);
    VERIFICAR(deshacer() && strcmp(texto, "%% años") == 0);
    VERIFICAR(deshacer() && strcmp(texto, "%% año") == 0);
}

/* Solo se recuerdan IDE_HISTORIAL_MAXIMO pasos: se olvidan los más viejos. */
static void prueba_maximo(void)
{
    abrir("");
    for (size_t i = 0; i < IDE_HISTORIAL_MAXIMO + 5; ++i) teclear(i, ";");
    VERIFICAR(h.cantidad == IDE_HISTORIAL_MAXIMO);
    size_t deshechos = 0;
    while (deshacer()) ++deshechos;
    VERIFICAR(deshechos == IDE_HISTORIAL_MAXIMO);
    VERIFICAR(strlen(texto) == 5);  /* Los 5 primeros ";" ya no se pueden deshacer. */
    VERIFICAR(!ide_historial_en_guardado(&h));  /* El texto vacío quedó fuera. */
}

int main(void)
{
    prueba_palabras();
    prueba_otro_lugar();
    prueba_borrar();
    prueba_accion_compuesta();
    prueba_nuevo_camino();
    prueba_guardado();
    prueba_tildes();
    prueba_maximo();
    ide_historial_vaciar(&h);
    if (fallos > 0) {
        fprintf(stderr, "test_ide_historial: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_historial: OK");
    return 0;
}
