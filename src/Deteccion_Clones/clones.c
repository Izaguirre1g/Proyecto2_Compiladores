#include "clones.h"

#include "lexer.h"
#include "ll1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Detección de clones siguiendo el CAMINO DE LA GRAMÁTICA.
 *
 * Cada archivo se analiza con el analizador predictivo LL(1) (src/LL1/), que
 * deja su árbol de derivación: qué producción se aplicó en cada paso y qué
 * token se leyó en cada hoja (ll1_derivar). Dos fragmentos son clones si su
 * subárbol de derivación es el mismo: las MISMAS producciones, en el MISMO
 * orden. Es decir, si el analizador recorrió el mismo camino en la gramática.
 *
 *   Tipo I:  mismo camino y mismos tokens, con el mismo texto. Solo pueden
 *            cambiar los espacios, los saltos de línea y los comentarios
 *            (el lexer los descarta: no llegan a la gramática).
 *   Tipo II: mismo camino; solo cambia el texto de los nombres y valores
 *            (id, num, cadena, car). Los demás terminales (palabras clave,
 *            operadores, tipos, símbolos) son parte del camino: si cambian,
 *            la derivación es otra y ya no es clon.
 *
 * Por ejemplo, "x : (a gauss 1);" y "x : a gauss 1;" NO son clones: los
 * paréntesis hacen que el analizador use  primario → ( expresion ) , un
 * camino distinto. Y un whale con el mismo cuerpo que un whether tampoco:
 * sentencia → ciclo  frente a  sentencia → condicional.
 *
 * Para comparar rápido se usa una HUELLA por nodo, calculada de las hojas
 * hacia arriba (recorrido ascendente, diapositiva 8 de "Optimización de
 * Código"): dos subárboles iguales tienen la misma huella, igual que en el
 * DAG (diapositiva 17), donde dos cálculos iguales caen en el mismo nodo.
 */

/* ------------------------------------------------------------------ */
/* Información de cada nodo                                            */
/* ------------------------------------------------------------------ */

/* Las dos formas de comparar: con nombres y valores (Tipo I) o sin ellos (Tipo II). */
enum { MODO_TIPO_1 = 0, MODO_TIPO_2 = 1 };

/* Un token de un archivo, con su texto y dónde está. */
typedef struct {
    TokenType tipo;
    char *texto;                    /* Copia propia del lexema. */
    size_t linea, columna;          /* Dónde empieza. */
    size_t linea_fin, columna_fin;  /* Dónde termina (justo después). */
} TokenClon;

/* Los tokens de un archivo. */
typedef struct {
    TokenClon *elementos;
    size_t cantidad;
    size_t capacidad;
} ListaTokens;

/*
 * Todos los nodos de derivación de todos los archivos van en un solo
 * arreglo, en PREORDEN (cada nodo antes que sus hijos; así los queda
 * ll1_derivar). Los descendientes de un nodo quedan juntos, justo después:
 *
 *     sentencia → id resto_identificador   [0]  tamano 5
 *       id  (x)                            [1]  tamano 1
 *       resto_identificador → : expr ;     [2]  tamano 3  -> descendientes [3..4]
 *       ...
 */
typedef struct {
    int simbolo;            /* Terminal o no terminal de la gramática LL(1). */
    int produccion;         /* La producción aplicada (-1 en las hojas). */
    const TokenClon *token; /* En las hojas: el token leído. */
    size_t fuente;          /* De qué archivo es (posición en el arreglo de fuentes). */
    size_t tamano;          /* Cuántos nodos tiene su subárbol, contándolo a él. */
    size_t tokens;          /* Cuántos tokens cubre su subárbol. */
    const TokenClon *primero, *ultimo;  /* Primer y último token que cubre (NULL si ninguno). */
    uint64_t huella[2];     /* Huella para el Tipo I y para el Tipo II. */
    int cubierto;           /* 1 si ya es parte de un clon reportado. */
} InfoNodo;

typedef struct {
    InfoNodo *elementos;
    size_t cantidad;
    size_t capacidad;
} ListaNodos;

/* ------------------------------------------------------------------ */
/* Huellas                                                             */
/* ------------------------------------------------------------------ */

/*
 * La huella se calcula con FNV-1a, una forma sencilla de convertir datos en
 * un número de 64 bits: por cada dato, se hace XOR y se multiplica por un
 * primo. Datos iguales dan la misma huella; datos distintos casi siempre
 * dan huellas distintas. "Casi" porque puede haber choques: por eso, cuando
 * dos huellas coinciden, después se comparan los árboles de verdad.
 */
#define FNV_INICIO 14695981039346656037ULL
#define FNV_PRIMO 1099511628211ULL

/* Mezcla un número en la huella. */
static uint64_t mezclar(uint64_t huella, uint64_t dato)
{
    for (int i = 0; i < 8; ++i) {
        huella ^= (dato >> (8 * i)) & 0xFF;
        huella *= FNV_PRIMO;
    }
    return huella;
}

/* Mezcla un texto en la huella (NULL cuenta como texto vacío). */
static uint64_t mezclar_texto(uint64_t huella, const char *texto)
{
    for (const char *c = (texto != NULL) ? texto : ""; *c != '\0'; ++c) {
        huella ^= (unsigned char)*c;
        huella *= FNV_PRIMO;
    }
    /* Un separador, para que "ab"+"c" no dé lo mismo que "a"+"bc". */
    return mezclar(huella, 0xFF);
}

/* ------------------------------------------------------------------ */
/* Paso 1: tokens y árbol de derivación de cada archivo                */
/* ------------------------------------------------------------------ */

static void liberar_tokens(ListaTokens *lista)
{
    for (size_t i = 0; i < lista->cantidad; ++i) free(lista->elementos[i].texto);
    free(lista->elementos);
    memset(lista, 0, sizeof *lista);
}

/* Lee todos los tokens de un texto, hasta el TOKEN_EOF incluido.
 * Devuelve 0 si funcionó o -1 si no hubo memoria. */
static int leer_tokens(const ClonFuente *fuente, ListaTokens *lista)
{
    Lexer lexer;
    lexer_init(&lexer, fuente->texto, fuente->largo, fuente->nombre);
    for (;;) {
        Token t = lexer_next(&lexer);
        if (t.error == TOKEN_OUT_OF_MEMORY) {
            token_dispose(&t);
            return -1;
        }
        if (lista->cantidad == lista->capacidad) {
            size_t nueva = (lista->capacidad == 0) ? 256 : lista->capacidad * 2;
            TokenClon *mas = realloc(lista->elementos, nueva * sizeof *mas);
            if (mas == NULL) {
                token_dispose(&t);
                return -1;
            }
            lista->elementos = mas;
            lista->capacidad = nueva;
        }
        /* Después de lexer_next, el lexer quedó justo al final del token. */
        TokenClon c = {t.type, t.lexeme, t.line, t.column, lexer.line, lexer.column};
        t.lexeme = NULL;  /* El texto ahora es de la lista. */
        token_dispose(&t);
        lista->elementos[lista->cantidad++] = c;
        if (c.tipo == TOKEN_EOF) return 0;
    }
}

/* Pasa el árbol de derivación de un archivo a la lista común de nodos. */
static int agregar_arbol(ListaNodos *lista, const Ll1Derivacion *arbol,
                         const ListaTokens *tokens, size_t fuente)
{
    if (lista->cantidad + arbol->cantidad > lista->capacidad) {
        size_t nueva = (lista->capacidad == 0) ? 1024 : lista->capacidad;
        while (nueva < lista->cantidad + arbol->cantidad) nueva *= 2;
        InfoNodo *mas = realloc(lista->elementos, nueva * sizeof *mas);
        if (mas == NULL) return -1;
        lista->elementos = mas;
        lista->capacidad = nueva;
    }
    for (size_t k = 0; k < arbol->cantidad; ++k) {
        const Ll1Nodo *n = &arbol->nodos[k];
        InfoNodo info = {0};
        info.simbolo = n->simbolo;
        info.produccion = n->produccion;
        info.token = LL1_ES_TERMINAL(n->simbolo) ? &tokens->elementos[n->token] : NULL;
        info.fuente = fuente;
        info.tamano = n->tamano;
        info.tokens = n->fin_token - n->primer_token;
        if (info.tokens > 0) {
            info.primero = &tokens->elementos[n->primer_token];
            info.ultimo = &tokens->elementos[n->fin_token - 1];
        }
        lista->elementos[lista->cantidad++] = info;
    }
    return 0;
}

/*
 * ¿El Tipo II ignora el texto de este terminal? Sí para los nombres y los
 * valores: es lo único que puede cambiar en una copia Tipo II. Los demás
 * terminales tienen siempre el mismo texto (una palabra clave o un símbolo).
 */
static int tipo2_ignora_texto(int simbolo)
{
    return simbolo == TOKEN_IDENTIFIER || simbolo == TOKEN_INTEGER ||
           simbolo == TOKEN_STRING || simbolo == TOKEN_CHARACTER;
}

/* ------------------------------------------------------------------ */
/* Paso 2: huellas, de las hojas hacia arriba                          */
/* ------------------------------------------------------------------ */

/*
 * La huella de un nodo junta: su símbolo, la producción que se aplicó (el
 * paso del camino), y las huellas de sus hijos en orden. En las hojas, el
 * Tipo I agrega el texto del token; el Tipo II, solo si no es un nombre o
 * un valor.
 *
 * Se recorre la lista de atrás hacia adelante: en preorden, los hijos están
 * DESPUÉS del padre, así que se calculan antes (recorrido ascendente). Los
 * hijos de i se encuentran así: el primero en i + 1; el siguiente, saltando
 * el subárbol del anterior; hasta llegar a i + tamano.
 */
static void calcular_huellas(ListaNodos *lista)
{
    for (size_t k = lista->cantidad; k > 0; --k) {
        size_t i = k - 1;
        InfoNodo *info = &lista->elementos[i];

        uint64_t h = mezclar(FNV_INICIO, (uint64_t)(unsigned)info->simbolo);
        h = mezclar(h, (uint64_t)(unsigned)(info->produccion + 1));
        uint64_t h1 = h, h2 = h;
        if (info->token != NULL) {
            h1 = mezclar_texto(h1, info->token->texto);
            if (!tipo2_ignora_texto(info->simbolo)) h2 = mezclar_texto(h2, info->token->texto);
        }
        for (size_t hijo = i + 1; hijo < i + info->tamano; hijo += lista->elementos[hijo].tamano) {
            h1 = mezclar(h1, lista->elementos[hijo].huella[MODO_TIPO_1]);
            h2 = mezclar(h2, lista->elementos[hijo].huella[MODO_TIPO_2]);
        }
        info->huella[MODO_TIPO_1] = h1;
        info->huella[MODO_TIPO_2] = h2;
    }
}

/*
 * Compara dos subárboles de verdad (por si dos huellas chocaron). Como
 * están en preorden, basta comparar posición por posición: mismo símbolo,
 * misma producción y misma forma (tamaño) en cada paso del camino, y en
 * las hojas el mismo texto (si el modo lo pide).
 */
static int subarboles_iguales(const ListaNodos *lista, size_t a, size_t b, int modo)
{
    const InfoNodo *e = lista->elementos;
    if (e[a].tamano != e[b].tamano) return 0;

    for (size_t k = 0; k < e[a].tamano; ++k) {
        const InfoNodo *x = &e[a + k], *y = &e[b + k];
        if (x->simbolo != y->simbolo || x->produccion != y->produccion ||
            x->tamano != y->tamano) return 0;
        if (x->token == NULL) continue;
        if (modo == MODO_TIPO_2 && tipo2_ignora_texto(x->simbolo)) continue;
        const char *tx = x->token->texto ? x->token->texto : "";
        const char *ty = y->token->texto ? y->token->texto : "";
        if (strcmp(tx, ty) != 0) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Paso 3: candidatos y grupos de clones                               */
/* ------------------------------------------------------------------ */

/*
 * ¿Qué se repite? Devuelve el nombre para mostrar, o NULL si este nodo no
 * se reporta (una expresión suelta, un tipo, una lista de parámetros...).
 * Se reportan las funciones, los bloques y las sentencias; la sentencia se
 * nombra por su primer token (y, si empieza con un nombre, por el segundo:
 * "x * ..." declara, "x : ..." asigna, "x(...)" llama).
 */
static const char *descripcion(const InfoNodo *info)
{
    if (info->simbolo == NT_FUNCION) return "funcion";
    if (info->simbolo == NT_BLOQUE) return "bloque";
    if (info->simbolo == NT_CONSTANTE) return "constante";
    if (info->simbolo != NT_SENTENCIA || info->primero == NULL) return NULL;

    switch (info->primero->tipo) {
        case TOKEN_WHETHER: return "condicional whether";
        case TOKEN_WHALE: return "ciclo whale";
        case TOKEN_CYCLE: return "ciclo cycle";
        case TOKEN_SEEK: return "bloque seek";
        case TOKEN_GIVE: return "give";
        case TOKEN_ADD: return "add";
        case TOKEN_REMOVE: return "remove";
        case TOKEN_DECLARE_LIST: return "declaracion de lista";
        case TOKEN_IDENTIFIER:
            if (info->tokens < 2) return "sentencia";
            switch (info->primero[1].tipo) {
                case TOKEN_STAR: return "declaracion";
                case TOKEN_LPAREN: return "llamada";
                default: return "asignacion";
            }
        default:
            return "sentencia";
    }
}

/* Un nodo que podría ser un clon, en uno de los dos modos. */
typedef struct {
    size_t indice;
    int modo;
    const ListaNodos *lista;  /* Para poder ordenarlos con qsort. */
} Candidato;

/*
 * Orden para revisar los candidatos:
 *   1. Del más grande al más pequeño (en tokens; si empatan, en nodos):
 *      así el clon grande se reporta primero y sus partes ya no se repiten.
 *   2. Con el mismo tamaño, Tipo I antes que Tipo II.
 *   3. Con la misma huella, juntos (así se forman los grupos).
 *   4. Por posición, para que el resultado siempre salga en el mismo orden.
 */
static int comparar_candidatos(const void *pa, const void *pb)
{
    const Candidato *a = pa, *b = pb;
    const InfoNodo *ia = &a->lista->elementos[a->indice];
    const InfoNodo *ib = &b->lista->elementos[b->indice];

    if (ia->tokens != ib->tokens) return (ia->tokens > ib->tokens) ? -1 : 1;
    if (ia->tamano != ib->tamano) return (ia->tamano > ib->tamano) ? -1 : 1;
    if (a->modo != b->modo) return (a->modo < b->modo) ? -1 : 1;
    if (ia->huella[a->modo] != ib->huella[b->modo]) {
        return (ia->huella[a->modo] < ib->huella[b->modo]) ? -1 : 1;
    }
    if (a->indice != b->indice) return (a->indice < b->indice) ? -1 : 1;
    return 0;
}

/* Marca un nodo y todo su subárbol como parte de un clon ya reportado. */
static void cubrir(ListaNodos *lista, size_t indice)
{
    size_t fin = indice + lista->elementos[indice].tamano;
    for (size_t k = indice; k < fin; ++k) lista->elementos[k].cubierto = 1;
}

/* Agrega un clon a la lista de resultados con los nodos indicados. */
static int agregar_clon(ClonLista *resultado, const ListaNodos *lista,
                        const ClonFuente *fuentes, const size_t *miembros,
                        size_t cantidad, int modo)
{
    if (resultado->cantidad == resultado->capacidad) {
        size_t nueva = (resultado->capacidad == 0) ? 16 : resultado->capacidad * 2;
        Clon *mas = realloc(resultado->elementos, nueva * sizeof *mas);
        if (mas == NULL) return -1;
        resultado->elementos = mas;
        resultado->capacidad = nueva;
    }

    const InfoNodo *primero = &lista->elementos[miembros[0]];
    Clon clon = {0};
    clon.tipo = (modo == MODO_TIPO_1) ? CLON_TIPO_1 : CLON_TIPO_2;
    clon.que = descripcion(primero);
    clon.tokens = primero->tokens;
    clon.fragmentos = calloc(cantidad, sizeof *clon.fragmentos);
    if (clon.fragmentos == NULL) return -1;

    for (size_t i = 0; i < cantidad; ++i) {
        const InfoNodo *info = &lista->elementos[miembros[i]];
        const char *nombre = fuentes[info->fuente].nombre;
        ClonFragmento *f = &clon.fragmentos[i];

        f->archivo = malloc(strlen(nombre) + 1);
        if (f->archivo == NULL) {
            for (size_t j = 0; j < i; ++j) free(clon.fragmentos[j].archivo);
            free(clon.fragmentos);
            return -1;
        }
        strcpy(f->archivo, nombre);
        /* Del primer token que cubre hasta el final del último. */
        f->linea = info->primero->linea;
        f->columna = info->primero->columna;
        f->linea_fin = info->ultimo->linea_fin;
        f->columna_fin = info->ultimo->columna_fin;
        clon.cantidad++;
    }
    resultado->elementos[resultado->cantidad++] = clon;
    return 0;
}

/*
 * Revisa una "racha" de candidatos con el mismo tamaño, modo y huella.
 * Casi siempre son todos iguales, pero si hubo un choque de huellas puede
 * haber varios grupos mezclados: se separan comparando los árboles.
 */
static int revisar_racha(Candidato *racha, size_t largo, ListaNodos *lista,
                         const ClonFuente *fuentes, ClonLista *resultado)
{
    size_t *grupo = malloc(largo * sizeof *grupo);
    char *usado = calloc(largo, 1);
    if (grupo == NULL || usado == NULL) {
        free(grupo);
        free(usado);
        return -1;
    }

    int modo = racha[0].modo;
    int resultado_final = 0;
    for (size_t i = 0; i < largo && resultado_final == 0; ++i) {
        if (usado[i]) continue;

        /* Armar el grupo de los que son iguales a racha[i]. */
        size_t cantidad = 0;
        grupo[cantidad++] = racha[i].indice;
        usado[i] = 1;
        for (size_t j = i + 1; j < largo; ++j) {
            if (!usado[j] && subarboles_iguales(lista, racha[i].indice, racha[j].indice, modo)) {
                grupo[cantidad++] = racha[j].indice;
                usado[j] = 1;
            }
        }
        if (cantidad < 2) continue;  /* Solo aparece una vez: no es clon. */

        /* Si TODOS ya son parte de un clon más grande, no se repite. */
        int alguno_nuevo = 0;
        for (size_t k = 0; k < cantidad; ++k) {
            if (!lista->elementos[grupo[k]].cubierto) alguno_nuevo = 1;
        }
        if (!alguno_nuevo) continue;

        if (agregar_clon(resultado, lista, fuentes, grupo, cantidad, modo) != 0) {
            resultado_final = -1;
            break;
        }
        for (size_t k = 0; k < cantidad; ++k) cubrir(lista, grupo[k]);
    }

    free(grupo);
    free(usado);
    return resultado_final;
}

/* ------------------------------------------------------------------ */
/* Función principal                                                   */
/* ------------------------------------------------------------------ */

/* Anota un archivo que no se pudo revisar por tener errores de sintaxis. */
static int anotar_omitido(ClonLista *resultado, const char *nombre)
{
    char **mas = realloc(resultado->nombres_omitidos,
                         (resultado->omitidos + 1) * sizeof *mas);
    if (mas == NULL) return -1;
    resultado->nombres_omitidos = mas;

    char *copia = malloc(strlen(nombre) + 1);
    if (copia == NULL) return -1;
    strcpy(copia, nombre);
    resultado->nombres_omitidos[resultado->omitidos++] = copia;
    return 0;
}

/* Los tipos de los tokens, como los pide el analizador LL(1). */
static TokenType *tipos_de(const ListaTokens *tokens)
{
    TokenType *tipos = malloc((tokens->cantidad ? tokens->cantidad : 1) * sizeof *tipos);
    if (tipos != NULL) {
        for (size_t i = 0; i < tokens->cantidad; ++i) tipos[i] = tokens->elementos[i].tipo;
    }
    return tipos;
}

int clones_buscar(const ClonFuente *fuentes, size_t cantidad, size_t minimo_tokens,
                  ClonLista *resultado)
{
    int estado = 0;
    ListaNodos lista = {0};
    Candidato *candidatos = NULL;
    ListaTokens *tokens = calloc(cantidad > 0 ? cantidad : 1, sizeof *tokens);
    if (tokens == NULL) estado = -1;

    /* Paso 1: los tokens y el árbol de derivación de cada archivo. Un
     * archivo con errores de sintaxis no tiene árbol: se cuenta y se salta. */
    for (size_t f = 0; f < cantidad && estado == 0; ++f) {
        if (leer_tokens(&fuentes[f], &tokens[f]) != 0) {
            estado = -1;
            break;
        }
        TokenType *tipos = tipos_de(&tokens[f]);
        if (tipos == NULL) {
            estado = -1;
            break;
        }
        Ll1Derivacion arbol = {0};
        int r = ll1_derivar(tipos, tokens[f].cantidad, &arbol);
        free(tipos);
        if (r < 0) estado = -1;
        else if (r != 0) {
            if (anotar_omitido(resultado, fuentes[f].nombre) != 0) estado = -1;
        } else if (agregar_arbol(&lista, &arbol, &tokens[f], f) != 0) {
            estado = -1;
        }
        ll1_liberar_derivacion(&arbol);
    }

    /* Paso 2: las huellas de todos los nodos. */
    if (estado == 0) calcular_huellas(&lista);

    /* Paso 3: los candidatos (funciones, bloques y sentencias de tamaño
     * suficiente), una vez por cada modo. */
    size_t total = 0;
    if (estado == 0) {
        candidatos = malloc((lista.cantidad * 2 + 1) * sizeof *candidatos);
        if (candidatos == NULL) estado = -1;
    }
    for (size_t i = 0; i < lista.cantidad && estado == 0; ++i) {
        const InfoNodo *info = &lista.elementos[i];
        if (descripcion(info) == NULL || info->tokens < minimo_tokens) continue;
        for (int modo = MODO_TIPO_1; modo <= MODO_TIPO_2; ++modo) {
            Candidato c = {i, modo, &lista};
            candidatos[total++] = c;
        }
    }

    /* Paso 4: ordenar y revisar las rachas con la misma huella. */
    if (estado == 0 && total > 0) {
        qsort(candidatos, total, sizeof *candidatos, comparar_candidatos);

        size_t inicio = 0;
        while (inicio < total && estado == 0) {
            const InfoNodo *a = &lista.elementos[candidatos[inicio].indice];
            size_t fin = inicio + 1;
            while (fin < total) {
                const InfoNodo *b = &lista.elementos[candidatos[fin].indice];
                if (b->tokens != a->tokens || b->tamano != a->tamano ||
                    candidatos[fin].modo != candidatos[inicio].modo ||
                    b->huella[candidatos[fin].modo] != a->huella[candidatos[inicio].modo]) break;
                fin++;
            }
            if (fin - inicio >= 2 &&
                revisar_racha(candidatos + inicio, fin - inicio, &lista, fuentes, resultado) != 0) {
                estado = -1;
            }
            inicio = fin;
        }
    }

    free(candidatos);
    free(lista.elementos);
    for (size_t f = 0; tokens != NULL && f < cantidad; ++f) liberar_tokens(&tokens[f]);
    free(tokens);
    return estado;
}

void clones_liberar(ClonLista *lista)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        Clon *c = &lista->elementos[i];
        for (size_t j = 0; j < c->cantidad; ++j) free(c->fragmentos[j].archivo);
        free(c->fragmentos);
    }
    free(lista->elementos);
    for (size_t i = 0; i < lista->omitidos; ++i) free(lista->nombres_omitidos[i]);
    free(lista->nombres_omitidos);
    lista->nombres_omitidos = NULL;
    lista->elementos = NULL;
    lista->cantidad = 0;
    lista->capacidad = 0;
    lista->omitidos = 0;
}

/* ------------------------------------------------------------------ */
/* Salida: texto y JSON                                                */
/* ------------------------------------------------------------------ */

/* Nombre del tipo de clon para mostrar. */
static const char *nombre_tipo(ClonTipo tipo)
{
    return (tipo == CLON_TIPO_1) ? "tipo I (codigo identico)"
                                 : "tipo II (mismo camino en la gramatica, otros nombres o valores)";
}

int clones_escribir_texto(const ClonLista *lista, size_t archivos, FILE *salida)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        const Clon *c = &lista->elementos[i];
        fprintf(salida, "Clon %zu: %s, %s de %zu tokens, %zu copias\n",
                i + 1, nombre_tipo(c->tipo), c->que, c->tokens, c->cantidad);
        for (size_t j = 0; j < c->cantidad; ++j) {
            const ClonFragmento *f = &c->fragmentos[j];
            if (f->linea == f->linea_fin) {
                fprintf(salida, "  %s: linea %zu\n", f->archivo, f->linea);
            } else {
                fprintf(salida, "  %s: lineas %zu a %zu\n", f->archivo, f->linea, f->linea_fin);
            }
        }
        fputc('\n', salida);
    }

    /* Resumen al final. */
    fprintf(salida, "%zu clon%s en %zu archivo%s", lista->cantidad,
            lista->cantidad == 1 ? "" : "es", archivos, archivos == 1 ? "" : "s");
    fputc('\n', salida);
    for (size_t i = 0; i < lista->omitidos; ++i) {
        fprintf(salida, "No se reviso %s: tiene errores de sintaxis\n", lista->nombres_omitidos[i]);
    }
    return ferror(salida) ? -1 : 0;
}

/* Escribe un texto entre comillas, escapando lo que JSON no permite tal cual. */
static void escribir_texto_json(const char *texto, FILE *salida)
{
    fputc('"', salida);
    for (const unsigned char *p = (const unsigned char *)texto; *p != '\0'; ++p) {
        if (*p == '"') fputs("\\\"", salida);
        else if (*p == '\\') fputs("\\\\", salida);
        else if (*p < 0x20) fprintf(salida, "\\u%04x", *p);
        else fputc(*p, salida);
    }
    fputc('"', salida);
}

int clones_escribir_json(const ClonLista *lista, size_t archivos, FILE *salida)
{
    fprintf(salida, "{\"archivos\": %zu, \"omitidos\": [", archivos);
    for (size_t i = 0; i < lista->omitidos; ++i) {
        if (i > 0) fputs(", ", salida);
        escribir_texto_json(lista->nombres_omitidos[i], salida);
    }
    fputs("], \"clones\": [", salida);
    for (size_t i = 0; i < lista->cantidad; ++i) {
        const Clon *c = &lista->elementos[i];
        fprintf(salida, "%s\n  {\"tipo\": %d, \"que\": ", i > 0 ? "," : "", (int)c->tipo);
        escribir_texto_json(c->que, salida);
        fprintf(salida, ", \"tokens\": %zu, \"fragmentos\": [", c->tokens);
        for (size_t j = 0; j < c->cantidad; ++j) {
            const ClonFragmento *f = &c->fragmentos[j];
            fprintf(salida, "%s\n    {\"archivo\": ", j > 0 ? "," : "");
            escribir_texto_json(f->archivo, salida);
            fprintf(salida, ", \"linea\": %zu, \"columna\": %zu, \"linea_fin\": %zu, \"columna_fin\": %zu}",
                    f->linea, f->columna, f->linea_fin, f->columna_fin);
        }
        fputs("]}", salida);
    }
    fputs(lista->cantidad > 0 ? "\n]}\n" : "]}\n", salida);
    return ferror(salida) ? -1 : 0;
}
