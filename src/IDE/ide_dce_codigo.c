/*
 * El código BAL nuevo, sin el código muerto (ver ide_dce_codigo_nuevo en
 * include/ide_dce.h).
 *
 * El DCE trabaja sobre la IR, pero el backend genera el ensamblador desde
 * el código fuente (su AST). Para que el ensamblador y el binario salgan
 * del código SIN el código muerto, lo que eliminó el DCE se lleva de vuelta
 * al código BAL: se borran o se reescriben las sentencias muertas, y el
 * resultado se compila con el compilador normal.
 *
 * Cómo se sabe qué sentencias tocar: cada instrucción de la IR guarda la
 * línea BAL de donde salió. Para una sentencia que ocupa sus propias líneas,
 * sus instrucciones son las de esas líneas; si TODAS se eliminaron, la
 * sentencia completa era código muerto.
 *
 * Pasos:
 *   1. Leer los tokens del código, con su posición en bytes (para editar).
 *   2. Fase 1: para cada declaración o asignación (del AST), encontrar sus
 *      tokens y contar cuántas de sus instrucciones eliminó el DCE.
 *   3. Fase 2: marcar los tokens que desaparecen del código nuevo.
 *   4. Fase 3: una declaración que desaparece pero cuya variable se sigue
 *      usando se une con la asignación siguiente (o se deja igual).
 *   5. Fase 4: anotar los cambios y aplicarlos de atrás hacia adelante
 *      (así las posiciones de los que faltan no se mueven).
 */
#include "ide_dce.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

/* ------------------------------------------------------------------ */
/* Tokens con posición en bytes                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    TokenType tipo;
    char *texto;               /* El lexema (copia propia). */
    size_t linea, linea_fin;   /* Línea donde empieza y donde termina. */
    size_t columna;            /* Columna donde empieza (en caracteres). */
    size_t byte, byte_fin;     /* Bytes del texto que ocupa: [byte, byte_fin). */
} TokenDce;

typedef struct {
    TokenDce *elementos;
    size_t cantidad;
} ListaTokensDce;

static void liberar_tokens(ListaTokensDce *l)
{
    for (size_t i = 0; i < l->cantidad; ++i) free(l->elementos[i].texto);
    free(l->elementos);
    memset(l, 0, sizeof *l);
}

/* Un cursor que avanza por el texto llevando el byte, la línea y la
 * columna (en caracteres, como el lexer), hasta (linea, columna). */
typedef struct {
    size_t byte, linea, columna;
} Cursor;

static void avanzar(const char *texto, size_t largo, Cursor *c, size_t linea, size_t columna)
{
    while (c->byte < largo && (c->linea < linea || (c->linea == linea && c->columna < columna))) {
        unsigned char ch = (unsigned char)texto[c->byte++];
        if (ch == '\n') {
            c->linea++;
            c->columna = 1;
        } else {
            c->columna++;
        }
        /* Los demás bytes de un carácter UTF-8 no cuentan como columna. */
        while (c->byte < largo && ((unsigned char)texto[c->byte] & 0xC0) == 0x80) c->byte++;
    }
}

/* Lee los tokens (sin el TOKEN_EOF). 0 bien, -1 sin memoria. */
static int leer_tokens(const char *texto, size_t largo, ListaTokensDce *l)
{
    size_t capacidad = 0;
    Lexer lexer;
    lexer_init(&lexer, texto, largo, "<dce>");
    Cursor cursor = {0, 1, 1};
    for (;;) {
        Token t = lexer_next(&lexer);
        if (t.error == TOKEN_OUT_OF_MEMORY) {
            token_dispose(&t);
            return -1;
        }
        if (t.type == TOKEN_EOF) {
            token_dispose(&t);
            return 0;
        }
        if (l->cantidad == capacidad) {
            capacidad = capacidad ? capacidad * 2 : 256;
            TokenDce *mas = realloc(l->elementos, capacidad * sizeof *mas);
            if (mas == NULL) {
                token_dispose(&t);
                return -1;
            }
            l->elementos = mas;
        }
        TokenDce d = {t.type, t.lexeme, t.line, lexer.line, t.column, 0, 0};
        t.lexeme = NULL;  /* El texto ahora es de la lista. */
        token_dispose(&t);
        avanzar(texto, largo, &cursor, d.linea, d.columna);
        d.byte = cursor.byte;
        avanzar(texto, largo, &cursor, lexer.line, lexer.column);
        d.byte_fin = cursor.byte;
        l->elementos[l->cantidad++] = d;
    }
}

/* ------------------------------------------------------------------ */
/* Cambios en el texto                                                 */
/* ------------------------------------------------------------------ */

/* Un cambio: reemplazar los bytes [desde, hasta) por "nuevo". */
typedef struct {
    size_t desde, hasta;
    char *nuevo;
} Cambio;

typedef struct {
    Cambio *elementos;
    size_t cantidad;
    size_t capacidad;
} ListaCambios;

static int agregar_cambio(ListaCambios *l, size_t desde, size_t hasta, char *nuevo)
{
    if (nuevo == NULL) return -1;
    if (l->cantidad == l->capacidad) {
        size_t nueva = l->capacidad ? l->capacidad * 2 : 16;
        Cambio *mas = realloc(l->elementos, nueva * sizeof *mas);
        if (mas == NULL) {
            free(nuevo);
            return -1;
        }
        l->elementos = mas;
        l->capacidad = nueva;
    }
    l->elementos[l->cantidad++] = (Cambio){desde, hasta, nuevo};
    return 0;
}

/* Una copia de texto[desde, hasta) seguida de "fin" (por ejemplo ";"). */
static char *copiar_trozo(const char *texto, size_t desde, size_t hasta, const char *fin)
{
    size_t largo = hasta - desde, extra = strlen(fin);
    char *c = malloc(largo + extra + 1);
    if (c != NULL) {
        memcpy(c, texto + desde, largo);
        memcpy(c + largo, fin, extra + 1);
    }
    return c;
}

/* El primer token con este tipo en [desde, hasta], o SIZE_MAX. */
static size_t buscar(const ListaTokensDce *t, TokenType tipo, size_t desde, size_t hasta)
{
    for (size_t i = desde; i <= hasta && i < t->cantidad; ++i) {
        if (t->elementos[i].tipo == tipo) return i;
    }
    return SIZE_MAX;
}

/* Bytes de las líneas completas desde la del token "primero" hasta la del
 * token "ultimo", con su salto de línea final (para borrarlas, junto con un
 * comentario que tengan al final). */
static void lineas_completas(const char *texto, size_t largo, const TokenDce *primero,
                             const TokenDce *ultimo, size_t *desde, size_t *hasta)
{
    size_t d = primero->byte;
    while (d > 0 && texto[d - 1] != '\n') d--;
    size_t h = ultimo->byte_fin;
    while (h < largo && texto[h] != '\n') h++;
    if (h < largo) h++;  /* También el salto de línea. */
    *desde = d;
    *hasta = h;
}

/* ------------------------------------------------------------------ */
/* Qué hacer con cada sentencia                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    ACCION_NADA,         /* No tiene código muerto: queda igual. */
    ACCION_BORRAR,       /* Muerta completa: se borran sus líneas. */
    ACCION_SOLO_LLAMADA, /* Solo murió la copia del resultado: queda "f(a);". */
    ACCION_UNIR,         /* Declaración muerta + la asignación siguiente:
                          * "r * declare_int : 0; r : f(4);" -> "r * declare_int : f(4);" */
    ACCION_CONSERVAR     /* Tiene algo muerto, pero no se puede escribir de otra
                          * forma (por ejemplo, una división que puede fallar). */
} Accion;

typedef struct {
    const IdeDceSentencia *sen;  /* NULL si no se pudo analizar (se deja igual). */
    size_t i, j;                 /* Sus tokens: [i, j]; j es el ";". */
    size_t asignar;              /* El ":" del valor (SIZE_MAX si no tiene). */
    size_t total, eliminadas, copias;
    Accion accion;
    size_t pareja;               /* Con ACCION_UNIR: la asignación que se le une. */
} Analisis;

/* La sentencia analizada que empieza en el token "i", o SIZE_MAX. */
static size_t sentencia_en(const Analisis *a, size_t cantidad, size_t i)
{
    for (size_t k = 0; k < cantidad; ++k) {
        if (a[k].sen != NULL && a[k].i == i) return k;
    }
    return SIZE_MAX;
}

int ide_dce_codigo_nuevo(const char *texto, size_t largo,
                         const IdeDceSentencia *sentencias, size_t cantidad,
                         const size_t *por_linea, size_t lineas,
                         const DceResultado *r, IdeDce *s)
{
    s->codigo_nuevo = NULL;
    s->sentencias_borradas = s->sentencias_cambiadas = s->sentencias_conservadas = 0;
    s->cabeceras_que_quedan = NULL;
    s->cantidad_cabeceras = 0;

    ListaTokensDce t = {0};
    ListaCambios cambios = {0};
    Analisis *an = calloc(cantidad ? cantidad : 1, sizeof *an);
    unsigned char *quitado = NULL;
    int estado = (an == NULL) ? -1 : leer_tokens(texto, largo, &t);
    if (estado == 0 && (quitado = calloc(t.cantidad ? t.cantidad : 1, 1)) == NULL) estado = -1;

    /* Fase 1: los tokens de cada sentencia y cuánto se eliminó de ella. */
    for (size_t k = 0; k < cantidad && estado == 0; ++k) {
        const IdeDceSentencia *sen = &sentencias[k];
        Analisis *a = &an[k];

        /* Sus tokens: desde el primero hasta el ";" que la termina (fuera de
         * paréntesis y corchetes). */
        size_t i = 0;
        while (i < t.cantidad && !(t.elementos[i].linea == sen->linea &&
                                   t.elementos[i].columna == sen->columna)) i++;
        if (i == t.cantidad) continue;
        /* Termina en el ";" fuera de paréntesis, corchetes y llaves; o, si es
         * una sentencia con bloque (whether, seek), en la "}" que cierra su
         * último bloque. Después de una "}" la sentencia sigue si viene
         * alif, also o seize (otro bloque) o stop/endgame (y su ";"). */
        size_t j = i;
        int nivel = 0;
        for (; j < t.cantidad; ++j) {
            TokenType tipo = t.elementos[j].tipo;
            if (tipo == TOKEN_LPAREN || tipo == TOKEN_LBRACKET || tipo == TOKEN_LBRACE) nivel++;
            if (tipo == TOKEN_RPAREN || tipo == TOKEN_RBRACKET || tipo == TOKEN_RBRACE) nivel--;
            if (tipo == TOKEN_SEMICOLON && nivel == 0) break;
            if (tipo == TOKEN_RBRACE && nivel == 0) {
                TokenType sigue = (j + 1 < t.cantidad) ? t.elementos[j + 1].tipo : TOKEN_EOF;
                if (sigue != TOKEN_ALIF && sigue != TOKEN_ALSO && sigue != TOKEN_SEIZE &&
                    sigue != TOKEN_STOP && sigue != TOKEN_ENDGAME) break;
            }
        }
        if (j == t.cantidad) continue;

        /* Solo sentencias que ocupan sus propias líneas: si otra sentencia
         * comparte una línea, sus instrucciones de la IR se mezclarían. */
        const TokenDce *primero = &t.elementos[i], *ultimo = &t.elementos[j];
        if (i > 0 && t.elementos[i - 1].linea_fin >= primero->linea) continue;
        if (j + 1 < t.cantidad && t.elementos[j + 1].linea <= ultimo->linea_fin) continue;

        a->sen = sen;
        a->i = i;
        a->j = j;
        a->asignar = buscar(&t, TOKEN_ASSIGN, i, j);

        /* Cuántas instrucciones de la IR tenían sus líneas, cuántas se
         * eliminaron, y si entre ellas está la copia final "nombre = ...". */
        for (size_t l = primero->linea; l <= ultimo->linea_fin && l < lineas; ++l) a->total += por_linea[l];
        size_t largo_nombre = sen->nombre ? strlen(sen->nombre) : 0;
        for (size_t e = 0; e < r->cantidad_eliminadas; ++e) {
            const DceEliminada *el = &r->eliminadas[e];
            if (el->linea < primero->linea || el->linea > ultimo->linea_fin) continue;
            a->eliminadas++;
            if (sen->nombre != NULL && strncmp(el->texto, sen->nombre, largo_nombre) == 0 &&
                strncmp(el->texto + largo_nombre, " = ", 3) == 0) a->copias++;
        }

        if (a->eliminadas == 0 || a->total == 0) a->accion = ACCION_NADA;
        else if (sen->es_otra) a->accion = (a->eliminadas >= a->total) ? ACCION_BORRAR : ACCION_NADA;
        else if (a->eliminadas == a->total) a->accion = ACCION_BORRAR;
        else if (sen->valor_es_llamada && a->eliminadas == 1 && a->copias == 1 &&
                 a->asignar != SIZE_MAX) a->accion = ACCION_SOLO_LLAMADA;
        else a->accion = ACCION_CONSERVAR;
    }

    /* Fase 2: qué tokens desaparecen del código nuevo (para saber después si
     * una variable se sigue usando en lo que QUEDA). */
    for (size_t k = 0; k < cantidad && estado == 0; ++k) {
        const Analisis *a = &an[k];
        if (a->sen == NULL) continue;
        size_t hasta = (a->accion == ACCION_BORRAR) ? a->j
                     : (a->accion == ACCION_SOLO_LLAMADA) ? a->asignar : SIZE_MAX;
        if (hasta == SIZE_MAX) continue;
        for (size_t x = a->i; x <= hasta; ++x) quitado[x] = 1;
    }

    /*
     * Fase 3: una declaración que se borra (o que queda solo en la llamada)
     * hace desaparecer la variable. Si la variable se sigue usando en lo que
     * queda, eso no se puede, y tampoco dejarla sin valor: el backend
     * necesita declaraciones con valor ("declaración inicializada"). Entonces:
     *   - si la sentencia siguiente es una asignación viva a la misma
     *     variable, se unen: "r * declare_int : 0; r : f(4);" pasa a
     *     "r * declare_int : f(4);";
     *   - si no, la declaración se deja igual.
     */
    for (size_t k = 0; k < cantidad && estado == 0; ++k) {
        Analisis *a = &an[k];
        if (a->sen == NULL || a->sen->es_otra || !a->sen->es_declaracion ||
            (a->accion != ACCION_BORRAR && a->accion != ACCION_SOLO_LLAMADA)) continue;
        int usada = 0;
        for (size_t x = 0; x < t.cantidad && !usada; ++x) {
            if (quitado[x] || (x >= a->i && x <= a->j)) continue;
            usada = t.elementos[x].tipo == TOKEN_IDENTIFIER && t.elementos[x].texto != NULL &&
                    strcmp(t.elementos[x].texto, a->sen->nombre) == 0;
        }
        if (!usada) continue;

        size_t siguiente = sentencia_en(an, cantidad, a->j + 1);
        if (a->accion == ACCION_BORRAR && a->asignar != SIZE_MAX && siguiente != SIZE_MAX &&
            !an[siguiente].sen->es_otra && !an[siguiente].sen->es_declaracion &&
            an[siguiente].accion == ACCION_NADA &&
            strcmp(an[siguiente].sen->nombre, a->sen->nombre) == 0 &&
            an[siguiente].asignar != SIZE_MAX) {
            a->accion = ACCION_UNIR;
            a->pareja = siguiente;
        } else {
            a->accion = ACCION_CONSERVAR;
        }
    }

    /* Las sentencias con bloque que tienen algo eliminado pero se quedan:
     * su primera línea no se debe tachar en el editor. */
    if (estado == 0) {
        s->cabeceras_que_quedan = calloc(cantidad ? cantidad : 1, sizeof *s->cabeceras_que_quedan);
        if (s->cabeceras_que_quedan == NULL) estado = -1;
    }
    for (size_t k = 0; k < cantidad && estado == 0; ++k) {
        const Analisis *a = &an[k];
        if (a->sen == NULL || !a->sen->es_otra || a->eliminadas == 0 || a->accion == ACCION_BORRAR) continue;
        s->cabeceras_que_quedan[s->cantidad_cabeceras++] = t.elementos[a->i].linea;
    }

    /* Fase 4: los cambios en el texto. Una sentencia que está DENTRO de otra
     * que se borra entera (por ejemplo, lo de adentro de un whether
     * inalcanzable) no necesita su propio cambio. */
    for (size_t k = 0; k < cantidad && estado == 0; ++k) {
        const Analisis *a = &an[k];
        if (a->sen == NULL) continue;
        int adentro = 0;
        for (size_t m = 0; m < cantidad && !adentro; ++m) {
            adentro = m != k && an[m].sen != NULL && an[m].accion == ACCION_BORRAR &&
                      an[m].i <= a->i && a->j <= an[m].j;
        }
        if (adentro) continue;
        const TokenDce *primero = &t.elementos[a->i], *ultimo = &t.elementos[a->j];
        size_t desde, hasta;
        switch (a->accion) {
            case ACCION_NADA:
                break;
            case ACCION_BORRAR:
                lineas_completas(texto, largo, primero, ultimo, &desde, &hasta);
                estado = agregar_cambio(&cambios, desde, hasta, copiar_trozo("", 0, 0, ""));
                s->sentencias_borradas++;
                break;
            case ACCION_SOLO_LLAMADA:
                /* "c * declare_int : f(a);" -> "f(a);" */
                estado = agregar_cambio(&cambios, primero->byte, ultimo->byte_fin,
                                        copiar_trozo(texto, t.elementos[a->asignar + 1].byte,
                                                     ultimo->byte, ";"));
                s->sentencias_cambiadas++;
                break;
            case ACCION_UNIR: {
                /* "r * declare_int : 0;  ...\n    r : f(4);"  ->  "r * declare_int : f(4);"
                 * (lo que había después del ";" de la asignación queda igual). */
                const Analisis *b = &an[a->pareja];
                char *cabeza = copiar_trozo(texto, primero->byte, t.elementos[a->asignar].byte_fin, " ");
                char *valor = copiar_trozo(texto, t.elementos[b->asignar + 1].byte,
                                           t.elementos[b->j].byte, ";");
                char *junto = NULL;
                if (cabeza != NULL && valor != NULL) {
                    junto = malloc(strlen(cabeza) + strlen(valor) + 1);
                    if (junto != NULL) {
                        strcpy(junto, cabeza);
                        strcat(junto, valor);
                    }
                }
                free(cabeza);
                free(valor);
                estado = agregar_cambio(&cambios, primero->byte, t.elementos[b->j].byte_fin, junto);
                s->sentencias_cambiadas++;
                break;
            }
            case ACCION_CONSERVAR:
                s->sentencias_conservadas++;
                break;
        }
    }

    /* Aplicar los cambios de atrás hacia adelante. */
    if (estado == 0 && cambios.cantidad > 0) {
        /* Ordenarlos por posición, de mayor a menor (inserción simple). */
        for (size_t a = 1; a < cambios.cantidad; ++a) {
            Cambio c = cambios.elementos[a];
            size_t b = a;
            while (b > 0 && cambios.elementos[b - 1].desde < c.desde) {
                cambios.elementos[b] = cambios.elementos[b - 1];
                b--;
            }
            cambios.elementos[b] = c;
        }
        size_t capacidad = largo + 1;
        for (size_t a = 0; a < cambios.cantidad; ++a) capacidad += strlen(cambios.elementos[a].nuevo);
        char *nuevo = malloc(capacidad);
        if (nuevo == NULL) {
            estado = -1;
        } else {
            memcpy(nuevo, texto, largo);
            nuevo[largo] = '\0';
            size_t actual = largo;
            for (size_t a = 0; a < cambios.cantidad; ++a) {
                const Cambio *c = &cambios.elementos[a];
                size_t poner = strlen(c->nuevo), quitar = c->hasta - c->desde;
                memmove(nuevo + c->desde + poner, nuevo + c->hasta, actual - c->hasta + 1);
                memcpy(nuevo + c->desde, c->nuevo, poner);
                actual = actual - quitar + poner;
            }
            s->codigo_nuevo = nuevo;
        }
    }

    for (size_t a = 0; a < cambios.cantidad; ++a) free(cambios.elementos[a].nuevo);
    free(cambios.elementos);
    free(an);
    free(quitado);
    liberar_tokens(&t);
    return estado;
}
