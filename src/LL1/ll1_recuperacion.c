/*
 * Recuperación de errores a nivel de frase con la tabla LL(1).
 *
 * Bison se detiene en el primer error de sintaxis. Este módulo sigue
 * analizando para encontrar TODOS los errores del archivo, como hacen los
 * editores: cuando el analizador predictivo falla en un token, se elige UNA
 * corrección local (diapositiva 4, "nivel de frase: corrección local del
 * error"), se aplica a la lista de tokens y se vuelve a analizar.
 *
 * Cómo se elige la corrección (ll1_mejor_correccion):
 *   1. Si la tabla PREDICE qué falta (ll1_insercion_predicha), es eso.
 *   2. Si no, se prueban correcciones locales alrededor del error, cada una
 *      validada con el propio LL(1), y se queda la mejor.
 *   3. Si ninguna sirve, se descarta el token del error ("modo pánico").
 *
 * Además, si se conocen los lexemas, se detectan palabras clave mal escritas
 * ("whethr" en vez de "whether"): ver buscar_palabra_mal_escrita.
 */
#include "ll1.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Más allá de esta cantidad de tokens, avanzar más no hace a una corrección
 * "mejor": lo que falle después ya es otro error. Así dos correcciones que
 * arreglan este error quedan empatadas y se decide por su costo.
 */
#define VENTANA_AVANCE 12

/*
 * Lo mínimo que tiene que avanzar una corrección para aceptarla. Con 1
 * aparecerían correcciones que solo "tapan" el error un token y vuelven a
 * fallar enseguida (por ejemplo, poner 'gauss' en  x : 5  y * ...).
 */
#define AVANCE_MINIMO 2

/* ------------------------------------------------------------------ */
/* Aplicar una corrección a una lista de tokens                         */
/* ------------------------------------------------------------------ */

/*
 * Escribe en "destino" la lista "tokens" con la corrección aplicada en el
 * token k. Si "mapa" no es NULL, también arma "mapa_destino": para cada
 * token nuevo, su índice en la lista ORIGINAL (SIZE_MAX si fue insertado).
 * "destino" debe tener espacio para n + LL1_MAX_INSERCION tokens.
 *
 * Devuelve la cantidad de tokens de la lista nueva. En "primero_original"
 * queda el índice (en la lista nueva) del primer token que estaba en la
 * lista vieja y va después del cambio.
 */
static size_t aplicar(const TokenType *tokens, const size_t *mapa, size_t n, size_t k,
                      const Ll1Correccion *c, TokenType *destino, size_t *mapa_destino,
                      size_t *primero_original)
{
    size_t m = 0;
    /* Lo que se copia igual: todo lo de antes del cambio. */
    size_t hasta = (c->tipo == LL1_BORRAR_ANTERIOR || c->tipo == LL1_INSERTAR_ANTES_ANTERIOR)
                       ? k - 1 : k;
    for (size_t i = 0; i < hasta; ++i) {
        if (mapa_destino != NULL) mapa_destino[m] = mapa[i];
        destino[m++] = tokens[i];
    }

    size_t desde;
    if (c->tipo == LL1_INSERTAR_ANTES_ANTERIOR) {
        /* Los tokens nuevos van antes del k - 1, que sigue igual después. */
        for (int j = 0; j < c->cantidad; ++j) {
            if (mapa_destino != NULL) mapa_destino[m] = SIZE_MAX;
            destino[m++] = (TokenType)c->terminales[j];
        }
        if (mapa_destino != NULL) mapa_destino[m] = mapa[k - 1];
        destino[m++] = tokens[k - 1];
        desde = k;
    } else if (c->tipo == LL1_INSERTAR || c->tipo == LL1_CAMBIAR) {
        for (int j = 0; j < c->cantidad; ++j) {
            /* Un token insertado no está en el archivo (SIZE_MAX). Uno que
             * REEMPLAZA a otro sí: ocupa el mismo lugar, así que conserva
             * su posición original ("wheter" cambiado por "whether" sigue
             * siendo el token de la línea 7, y si ahí hay otro error, se
             * reporta ahí). */
            if (mapa_destino != NULL) {
                mapa_destino[m] = (c->tipo == LL1_CAMBIAR) ? mapa[k] : SIZE_MAX;
            }
            destino[m++] = (TokenType)c->terminales[j];
        }
        desde = (c->tipo == LL1_INSERTAR) ? k : k + 1;  /* Al cambiar, el del error se va. */
    } else if (c->tipo == LL1_BORRAR) {
        desde = k + 1;
    } else {  /* LL1_BORRAR_ANTERIOR: se saltó el k - 1, el del error sigue. */
        desde = k;
    }

    *primero_original = m;
    for (size_t i = desde; i < n; ++i) {
        if (mapa_destino != NULL) mapa_destino[m] = mapa[i];
        destino[m++] = tokens[i];
    }
    return m;
}

/*
 * Prueba una corrección: la aplica y vuelve a analizar con el LL(1).
 * Devuelve cuántos tokens ORIGINALES se lograron leer después del cambio
 * (VENTANA_AVANCE si el programa quedó válido), o (size_t)-1 sin memoria.
 */
static size_t probar(const TokenType *tokens, size_t n, size_t k, const Ll1Correccion *c,
                     TokenType *trabajo)
{
    size_t primero_original;
    size_t m = aplicar(tokens, NULL, n, k, c, trabajo, NULL, &primero_original);

    Ll1Resultado r;
    if (ll1_analizar(trabajo, m, &r) != 0) return (size_t)-1;
    if (r.estado == LL1_ACEPTADA) return VENTANA_AVANCE;
    if (r.posicion <= primero_original) return 0;  /* Falla igual o antes: no sirve. */

    size_t avance = r.posicion - primero_original;
    return avance > VENTANA_AVANCE ? VENTANA_AVANCE : avance;
}

/* ------------------------------------------------------------------ */
/* Elegir la mejor corrección                                          */
/* ------------------------------------------------------------------ */

/*
 * Cierres y separadores: lo que más se olvida al escribir.
 * En BAL el '*' NO es la multiplicación (esa es 'pitagoras'): es el
 * separador de las declaraciones "nombre * tipo" (ver NT_RESTO_IDENTIFICADOR
 * en ll1_gramatica.c). Por eso cuenta igual que ':' o '#'.
 */
static int es_cierre_o_separador(int terminal)
{
    switch (terminal) {
        case TOKEN_SEMICOLON: case TOKEN_RPAREN: case TOKEN_RBRACE: case TOKEN_RBRACKET:
        case TOKEN_COMMA: case TOKEN_LPAREN: case TOKEN_LBRACE: case TOKEN_ASSIGN:
        case TOKEN_HASH: case TOKEN_STAR:
            return 1;
        default:
            return 0;
    }
}

/*
 * Qué tan "cara" es una corrección para la persona, cuando dos arreglan el
 * error igual de bien. Más barata = mejor:
 *   0  agregar un cierre o separador olvidado (';' ')' '}' '*' ...)
 *   1  borrar un token que sobra
 *   2  cambiar un token por otro
 *   3  agregar una palabra u operador (también antes del token anterior)
 */
static int costo(const Ll1Correccion *c)
{
    switch (c->tipo) {
        case LL1_BORRAR:
        case LL1_BORRAR_ANTERIOR:
            return 1;
        case LL1_CAMBIAR:
            return 2;
        case LL1_INSERTAR_ANTES_ANTERIOR:
            return 3;  /* Como agregar una palabra: lo más caro. */
        default:
            return es_cierre_o_separador(c->terminales[0]) ? 0 : 3;
    }
}

/* Desempate final: el orden en que se prefieren los cierres. */
static int preferencia(const Ll1Correccion *c)
{
    static const int orden[] = {TOKEN_SEMICOLON, TOKEN_RPAREN, TOKEN_RBRACE, TOKEN_RBRACKET,
                                TOKEN_COMMA, TOKEN_LPAREN, TOKEN_LBRACE, TOKEN_ASSIGN,
                                TOKEN_STAR};
    if (c->cantidad == 0) return 0;
    for (size_t i = 0; i < sizeof orden / sizeof orden[0]; ++i) {
        if (orden[i] == c->terminales[0]) return (int)i;
    }
    return 100 + c->terminales[0];  /* Los demás, en el orden de la lista de tokens. */
}

/* ¿Es mejor la corrección a (que avanzó avance_a) que la b? */
static int es_mejor(const Ll1Correccion *a, size_t avance_a,
                    const Ll1Correccion *b, size_t avance_b)
{
    if (avance_a != avance_b) return avance_a > avance_b;           /* Arregla más. */
    if (costo(a) != costo(b)) return costo(a) < costo(b);           /* Es más sencilla. */
    return preferencia(a) < preferencia(b);
}

/* Definidas más abajo, en "Palabras clave mal escritas". */
static int es_palabra_clave(int terminal);
static int parecida(const char *lexema, const char *palabra);

/*
 * ¿Tiene sentido cambiar el token "viejo" por el "nuevo"? Se evitan cambios
 * que ninguna persona haría, aunque arreglen la sintaxis (cambiar 'seize'
 * por 'main', o 'endgame' por 'give'):
 *   - un nombre o un número sí se puede cambiar por cualquier cosa
 *     (por ejemplo, un nombre escrito donde iba un tipo);
 *   - un símbolo solo por otro símbolo: ']' por ')';
 *   - una palabra clave solo por otra parecida: 'whale' por 'whole'.
 */
static int cambio_razonable(int viejo, int nuevo)
{
    if (!ll1_texto_fijo(viejo)) return 1;
    int viejo_palabra = es_palabra_clave(viejo), nuevo_palabra = es_palabra_clave(nuevo);
    if (!viejo_palabra && !nuevo_palabra) return 1;
    if (viejo_palabra && nuevo_palabra) return parecida(ll1_texto(viejo), ll1_texto(nuevo));
    return 0;
}

/* Una corrección de un solo terminal. */
static Ll1Correccion con_terminal(Ll1TipoCorreccion tipo, int terminal)
{
    Ll1Correccion c = {tipo, {terminal, 0}, 1, 0};
    return c;
}

/*
 * La búsqueda de la mejor corrección. "profundidad" dice cuántas veces más
 * se puede mirar el error SIGUIENTE (ver el paso 3): 1 desde afuera, 0 cuando
 * ya se está mirando ese segundo error (así no se repite sin fin).
 */
static int mejor_correccion(const TokenType *tokens, size_t cantidad,
                            Ll1Correccion *correccion, int profundidad)
{
    memset(correccion, 0, sizeof *correccion);

    Ll1Resultado r;
    if (ll1_analizar(tokens, cantidad, &r) != 0) return -1;
    if (r.estado != LL1_ERROR) return 0;
    size_t k = r.posicion;

    /* Paso 1: lo que predice la tabla. */
    Ll1Insercion insercion;
    int predicha = ll1_insercion_predicha(tokens, cantidad, &insercion);
    if (predicha < 0) return -1;
    if (predicha == 1) {
        correccion->tipo = LL1_INSERTAR;
        correccion->cantidad = insercion.cantidad;
        for (int j = 0; j < insercion.cantidad; ++j) correccion->terminales[j] = insercion.terminales[j];
        correccion->predicha = 1;
        return 1;
    }

    /* Paso 2: la tabla no decide sola; probar correcciones locales. */
    TokenType *trabajo = malloc((cantidad + LL1_MAX_INSERCION) * sizeof *trabajo);
    if (trabajo == NULL) return -1;

    Ll1Correccion candidatas[3 * LL1_TERMINALES + 2];
    size_t cuantas = 0;
    for (int t = 0; t < LL1_TERMINALES; ++t) {
        /* Solo tokens que se escriben siempre igual: un nombre, un número o
         * un valor (declare_true / declare_false) habría que inventarlo. */
        if (!r.esperados.tiene[t] || !ll1_texto_fijo(t)) continue;
        if (t == TOKEN_DECLARE_TRUE || t == TOKEN_DECLARE_FALSE) continue;
        candidatas[cuantas++] = con_terminal(LL1_INSERTAR, t);
        if (tokens[k] != TOKEN_EOF && cambio_razonable(tokens[k], t)) {
            candidatas[cuantas++] = con_terminal(LL1_CAMBIAR, t);
        }
    }
    if (tokens[k] != TOKEN_EOF) candidatas[cuantas++] = (Ll1Correccion){LL1_BORRAR, {0, 0}, 0, 0};
    if (k > 0) candidatas[cuantas++] = (Ll1Correccion){LL1_BORRAR_ANTERIOR, {0, 0}, 0, 0};

    /* Agregar algo ANTES del token anterior (falta 'cycle' en  i let 0 ...).
     * Qué puede ir ahí lo dice la tabla: se analiza hasta antes de ese token
     * y se toman sus esperados. Solo palabras y símbolos de texto fijo. */
    if (k > 0) {
        Ll1Resultado antes;
        if (ll1_analizar(tokens, k - 1, &antes) != 0) {
            free(trabajo);
            return -1;
        }
        for (int t = 0; antes.estado == LL1_INCOMPLETA && t < LL1_TERMINALES; ++t) {
            if (!antes.esperados.tiene[t] || !ll1_texto_fijo(t)) continue;
            if (t == TOKEN_DECLARE_TRUE || t == TOKEN_DECLARE_FALSE) continue;
            candidatas[cuantas++] = con_terminal(LL1_INSERTAR_ANTES_ANTERIOR, t);
        }
    }

    int estado = 0;
    size_t mejor_avance = 0;
    size_t avances[2 * LL1_TERMINALES + 2];  /* Lo que avanzó cada candidata. */
    for (size_t i = 0; i < cuantas; ++i) {
        size_t avance = probar(tokens, cantidad, k, &candidatas[i], trabajo);
        if (avance == (size_t)-1) {
            free(trabajo);
            return -1;
        }
        avances[i] = avance;
        if (avance < AVANCE_MINIMO) continue;  /* No sirve. */
        if (estado == 0 || es_mejor(&candidatas[i], avance, correccion, mejor_avance)) {
            *correccion = candidatas[i];
            mejor_avance = avance;
            estado = 1;
        }
    }

    /*
     * Paso 3: ninguna corrección sirve SOLA. Puede pasar que haya DOS errores
     * seguidos, por ejemplo en una declaración sin '*' ni ':':
     *
     *     resultado  declare_int 1;        (debía ser  resultado * declare_int : 1;)
     *
     * Agregar '*' lee 'declare_int' pero vuelve a fallar en el '1' (avanza
     * solo 1 token). Aun así es la corrección correcta, porque ese segundo
     * error también tiene arreglo: agregar ':'. Entonces se acepta una
     * inserción que avanzó al menos un token si el error que sigue se puede
     * corregir bien. Entre varias, gana la que deja corregir mejor el segundo
     * error; si empatan, la más sencilla. El segundo error se reporta
     * después, en su lugar, con su propia corrección.
     */
    if (estado == 0 && profundidad > 0) {
        for (size_t i = 0; i < cuantas; ++i) {
            if (candidatas[i].tipo != LL1_INSERTAR || avances[i] == 0) continue;

            size_t primero_original;
            size_t m = aplicar(tokens, NULL, cantidad, k, &candidatas[i], trabajo, NULL,
                               &primero_original);
            Ll1Correccion segunda;
            int hay = mejor_correccion(trabajo, m, &segunda, profundidad - 1);
            if (hay < 0) {
                free(trabajo);
                return -1;
            }
            if (hay == 0) continue;  /* El segundo error tampoco tiene arreglo. */

            /* Cuánto avanza el programa con las dos correcciones. */
            Ll1Resultado r2;
            if (ll1_analizar(trabajo, m, &r2) != 0) {
                free(trabajo);
                return -1;
            }
            TokenType *trabajo2 = malloc((m + LL1_MAX_INSERCION) * sizeof *trabajo2);
            if (trabajo2 == NULL) {
                free(trabajo);
                return -1;
            }
            size_t avance = probar(trabajo, m, r2.posicion, &segunda, trabajo2);
            free(trabajo2);
            if (avance == (size_t)-1) {
                free(trabajo);
                return -1;
            }
            if (avance < AVANCE_MINIMO) continue;
            if (estado == 0 || es_mejor(&candidatas[i], avance, correccion, mejor_avance)) {
                *correccion = candidatas[i];
                mejor_avance = avance;
                estado = 1;
            }
        }
    }

    free(trabajo);
    return estado;
}

int ll1_mejor_correccion(const TokenType *tokens, size_t cantidad, Ll1Correccion *correccion)
{
    return mejor_correccion(tokens, cantidad, correccion, 1);
}

/* ------------------------------------------------------------------ */
/* Palabras clave mal escritas                                         */
/* ------------------------------------------------------------------ */

/*
 * Distancia de edición (Levenshtein) entre dos palabras: cuántas letras hay
 * que agregar, quitar o cambiar para pasar de una a otra.
 *   whethr -> whether: 1 (falta la 'e')     declar_int -> declare_int: 1
 * Se calcula con programación dinámica guardando solo la fila anterior.
 */
static size_t distancia(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (lb > 63) return (size_t)-1;  /* Ninguna palabra clave es tan larga. */
    size_t fila[64];
    for (size_t j = 0; j <= lb; ++j) fila[j] = j;
    for (size_t i = 1; i <= la; ++i) {
        size_t diagonal = fila[0];  /* fila_anterior[j - 1] */
        fila[0] = i;
        for (size_t j = 1; j <= lb; ++j) {
            size_t arriba = fila[j];
            size_t cambiar = diagonal + (a[i - 1] != b[j - 1]);
            size_t quitar = arriba + 1, agregar = fila[j - 1] + 1;
            size_t menor = cambiar < quitar ? cambiar : quitar;
            fila[j] = menor < agregar ? menor : agregar;
            diagonal = arriba;
        }
    }
    return fila[lb];
}

/* ¿Es "lexema" una palabra clave mal escrita? A lo sumo 2 letras de
 * diferencia, y nunca más de un tercio de la palabra (así "x" no se
 * confunde con "aka"). */
static int parecida(const char *lexema, const char *palabra)
{
    size_t largo = strlen(palabra);
    if (largo < 4 || strlen(lexema) < 3) return 0;
    size_t d = distancia(lexema, palabra);
    return d >= 1 && d <= 2 && d * 3 <= largo;
}

/* Las palabras clave del lenguaje: los terminales que se escriben con letras. */
static int es_palabra_clave(int terminal)
{
    const char *texto = ll1_texto(terminal);
    return ll1_texto_fijo(terminal) && texto[0] >= 'a' && texto[0] <= 'z';
}

/* Declarada más abajo: el análisis con recuperación. */
static int recuperar(const TokenType *tokens, const char *const *lexemas, size_t cantidad,
                     Ll1Errores *errores);

/* Cuántos errores tiene una lista de tokens (con recuperación, sin buscar
 * palabras mal escritas). Devuelve (size_t)-1 sin memoria. */
static size_t contar_errores(const TokenType *tokens, size_t cantidad)
{
    Ll1Errores errores;
    if (recuperar(tokens, NULL, cantidad, &errores) != 0) return (size_t)-1;
    size_t total = errores.cantidad;
    ll1_liberar_errores(&errores);
    return total;
}

/*
 * Busca, hacia atrás desde el error k y dentro de la misma sentencia (sin
 * pasar un ';', '{' o '}'), un nombre parecido a una palabra clave tal que,
 * al cambiarlo por la palabra, el archivo quede con MENOS errores.
 *
 *   whethr (n <= 1)        el parser lo lee como una llamada a whethr(...)
 *       resultado : 1;     y falla aquí, pidiendo un ';'
 *
 * Cambiando "whethr" por "whether" el resto del archivo deja de fallar en
 * cascada, así que esa es la corrección. Si el nombre de verdad era un
 * nombre (una variable que se llama "whale1"), cambiarlo no reduce los
 * errores y no se propone.
 *
 * Devuelve 1 si encontró una (queda en *posicion y *palabra), 0 si no, -1
 * sin memoria. "mapa" dice qué token original es cada uno (SIZE_MAX si fue
 * insertado); los lexemas son de la lista original.
 */
static int buscar_palabra_mal_escrita(const TokenType *tokens, const size_t *mapa, size_t n,
                                      size_t k, const char *const *lexemas,
                                      size_t *posicion, int *palabra)
{
    /* Primero, ¿hay algún candidato? Casi nunca: así no se gasta tiempo. */
    int hay_candidato = 0;
    size_t limite = (k > 10) ? k - 10 : 0;  /* Como mucho 10 tokens hacia atrás. */
    for (size_t j = k + 1; j-- > limite;) {
        if (j != k && (tokens[j] == TOKEN_SEMICOLON || tokens[j] == TOKEN_LBRACE ||
                       tokens[j] == TOKEN_RBRACE)) {
            break;  /* Empieza otra sentencia. */
        }
        if (tokens[j] == TOKEN_IDENTIFIER && mapa[j] != SIZE_MAX && lexemas[mapa[j]] != NULL) {
            hay_candidato = 1;
        }
    }
    if (!hay_candidato) return 0;

    size_t errores_ahora = contar_errores(tokens, n);
    if (errores_ahora == (size_t)-1) return -1;

    TokenType *prueba = malloc(n * sizeof *prueba);
    if (prueba == NULL) return -1;
    memcpy(prueba, tokens, n * sizeof *prueba);

    int encontrada = 0;
    size_t menos_errores = errores_ahora;
    for (size_t j = k + 1; j-- > limite;) {
        if (j != k && (tokens[j] == TOKEN_SEMICOLON || tokens[j] == TOKEN_LBRACE ||
                       tokens[j] == TOKEN_RBRACE)) {
            break;
        }
        if (tokens[j] != TOKEN_IDENTIFIER || mapa[j] == SIZE_MAX) continue;
        const char *lexema = lexemas[mapa[j]];
        if (lexema == NULL) continue;

        for (int t = 0; t < LL1_TERMINALES; ++t) {
            if (!es_palabra_clave(t) || !parecida(lexema, ll1_texto(t))) continue;
            prueba[j] = (TokenType)t;
            size_t errores = contar_errores(prueba, n);
            prueba[j] = tokens[j];
            if (errores == (size_t)-1) {
                free(prueba);
                return -1;
            }
            if (errores < menos_errores) {  /* Reduce los errores: es mejor. */
                menos_errores = errores;
                *posicion = j;
                *palabra = t;
                encontrada = 1;
            }
        }
    }
    free(prueba);
    return encontrada;
}

/* ------------------------------------------------------------------ */
/* Analizar todo el archivo                                            */
/* ------------------------------------------------------------------ */

/* Tokens de sincronización del modo pánico: donde termina o empieza una sentencia. */
static int sincroniza(TokenType tipo)
{
    return tipo == TOKEN_SEMICOLON || tipo == TOKEN_LBRACE || tipo == TOKEN_RBRACE ||
           tipo == TOKEN_EOF;
}

/*
 * Modo pánico (diapositiva 4): cuando no hay ninguna corrección local, se
 * descarta la sentencia rota completa y el análisis sigue en la siguiente.
 *
 * Se borra desde el inicio de la sentencia del error (justo después del
 * ';', '{' o '}' anterior) hasta el siguiente token de sincronización; si
 * ese es un ';', también se borra, porque cerraba la sentencia descartada.
 *
 *   i let 0 until 10 step 1 { ... }      (falta "cycle" al inicio)
 *   ^^^^^^^^^^^^^^^^^^^^^^^ se descarta: queda "{ ... }", un bloque válido
 *
 * Así se reporta UN error, y no uno por cada token descartado.
 * Escribe la lista nueva en "destino" y devuelve su largo. En "reanudar"
 * queda el índice (en la lista nueva) donde sigue el análisis.
 */
static size_t descartar_sentencia(const TokenType *tokens, const size_t *mapa, size_t n,
                                  size_t k, TokenType *destino, size_t *mapa_destino,
                                  size_t *reanudar)
{
    size_t inicio = k;
    while (inicio > 0 && !sincroniza(tokens[inicio - 1])) inicio--;
    size_t fin = k;  /* Primer token que se conserva. */
    while (fin < n && !sincroniza(tokens[fin])) fin++;
    if (fin < n && tokens[fin] == TOKEN_SEMICOLON) fin++;
    if (fin == inicio && tokens[k] != TOKEN_EOF) fin = k + 1;  /* Avanzar al menos un token. */
    *reanudar = inicio;

    size_t m = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i >= inicio && i < fin) continue;
        mapa_destino[m] = mapa[i];
        destino[m++] = tokens[i];
    }
    return m;
}

static int guardar_error(Ll1Errores *errores, const Ll1Error *error)
{
    if (errores->cantidad == errores->capacidad) {
        size_t nueva = (errores->capacidad == 0) ? 8 : errores->capacidad * 2;
        Ll1Error *mas = realloc(errores->elementos, nueva * sizeof *mas);
        if (mas == NULL) return -1;
        errores->elementos = mas;
        errores->capacidad = nueva;
    }
    errores->elementos[errores->cantidad++] = *error;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Operandos que faltan                                                */
/* ------------------------------------------------------------------ */

/* Operadores que necesitan un operando a cada lado (ver las producciones
 * resto_or, resto_xor, resto_and, operador_relacional, resto_suma,
 * resto_producto y resto_potencia en ll1_gramatica.c). */
static int es_operador_binario(int t)
{
    switch (t) {
        case TOKEN_OR: case TOKEN_XOR: case TOKEN_AND:
        case TOKEN_EQUAL: case TOKEN_NOT_EQUAL: case TOKEN_LESS: case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL: case TOKEN_GREATER_EQUAL:
        case TOKEN_GAUSS: case TOKEN_NEUMANN:
        case TOKEN_PITAGORAS: case TOKEN_EUCLIDES: case TOKEN_EULER: case TOKEN_DESCARTES:
            return 1;
        default:
            return 0;
    }
}

/*
 * ¿El error en el token k es una operación a la que le falta un operando?
 * Se reconoce porque la tabla esperaba un OPERANDO en ese punto: entre los
 * esperados está un número (al inicio de una sentencia, por ejemplo, no se
 * espera un número, así que ahí no se confunde).
 *   - Falta el de la DERECHA: el token anterior es un operador (binario o
 *     unario, '~'):   i : i gauss ;     whether (x < ) {
 *   - Falta el de la IZQUIERDA: el token del error es un operador binario
 *     que llegó cuando se esperaba un operando:   x : gauss 1;
 *     ('neumann' no cuenta: también es el menos unario, "x : neumann 1").
 * Devuelve LL1_FALTA_DERECHO, LL1_FALTA_IZQUIERDO o LL1_SIN_FALTA, y en
 * "operador" el índice del operador.
 */
static int falta_operando(const TokenType *tokens, size_t k, const Ll1Conjunto *esperados,
                          size_t *operador)
{
    if (!esperados->tiene[TOKEN_INTEGER]) return LL1_SIN_FALTA;  /* No se esperaba un operando. */
    if (k > 0 && (es_operador_binario(tokens[k - 1]) || tokens[k - 1] == TOKEN_NOT)) {
        *operador = k - 1;
        return LL1_FALTA_DERECHO;
    }
    if (es_operador_binario(tokens[k]) && tokens[k] != TOKEN_NEUMANN) {
        *operador = k;
        return LL1_FALTA_IZQUIERDO;
    }
    return LL1_SIN_FALTA;
}

static int recuperar(const TokenType *tokens, const char *const *lexemas, size_t cantidad,
                     Ll1Errores *errores)
{
    memset(errores, 0, sizeof *errores);

    /*
     * Se trabaja sobre una copia de los tokens que se va corrigiendo.
     * "mapa" dice, para cada token de la copia, cuál era en la lista
     * original (SIZE_MAX si lo insertó una corrección). Así cada error se
     * reporta en su posición real del archivo. Cada corrección agrega como
     * mucho LL1_MAX_INSERCION tokens, por eso se reserva ese espacio extra
     * por cada error posible.
     */
    size_t capacidad = cantidad + LL1_MAX_INSERCION * (LL1_MAX_ERRORES + 1);
    TokenType *actual = malloc(capacidad * sizeof *actual);
    TokenType *siguiente = malloc(capacidad * sizeof *siguiente);
    size_t *mapa = malloc(capacidad * sizeof *mapa);
    size_t *mapa_siguiente = malloc(capacidad * sizeof *mapa_siguiente);
    int estado = 0;
    if (actual == NULL || siguiente == NULL || mapa == NULL || mapa_siguiente == NULL) {
        estado = -1;
        goto fin;
    }
    for (size_t i = 0; i < cantidad; ++i) {
        actual[i] = tokens[i];
        mapa[i] = i;
    }
    size_t n = cantidad;

    /*
     * Después de un descarte por pánico, el análisis sigue en "reanudar".
     * Si el siguiente error ocurre JUSTO ahí, es la misma falla en cascada
     * (por ejemplo, el error estaba en la cabecera de una función y el parser
     * todavía espera el resto de la cabecera): se sigue descartando sin
     * reportarlo, como hacen Yacc y Bison para no llenar la lista de errores
     * repetidos.
     */
    size_t reanudar = SIZE_MAX;
    int hubo_panico = 0;

    while (errores->cantidad < LL1_MAX_ERRORES) {
        Ll1Resultado r;
        if (ll1_analizar(actual, n, &r) != 0) {
            estado = -1;
            break;
        }
        if (r.estado != LL1_ERROR) {       /* Ya no hay más errores. */
            /* Si hubo que descartar sentencias, las correcciones no explican
             * todo el archivo: se descartó código sin saber qué faltaba. */
            errores->completo = (r.estado == LL1_ACEPTADA) && !hubo_panico;
            break;
        }
        size_t k = r.posicion;
        if (mapa[k] == SIZE_MAX) break;    /* No debería pasar: lo insertado sí se lee. */

        if (k == reanudar) {  /* Error en cascada después de un pánico: sin reportarlo. */
            if (actual[k] == TOKEN_EOF) break;
            n = descartar_sentencia(actual, mapa, n, k, siguiente, mapa_siguiente, &reanudar);
            goto cambiar_listas;
        }

        Ll1Error error;
        memset(&error, 0, sizeof error);
        Ll1Correccion c;

        /* ¿Es una palabra clave mal escrita? Entonces el error está en ese nombre. */
        size_t j = 0;
        int palabra = 0;
        int mal_escrita = (lexemas != NULL)
                              ? buscar_palabra_mal_escrita(actual, mapa, n, k, lexemas, &j, &palabra)
                              : 0;
        if (mal_escrita < 0) {
            estado = -1;
            break;
        }
        if (mal_escrita) {
            k = j;
            c = con_terminal(LL1_CAMBIAR, palabra);
            error.palabra_mal_escrita = 1;
            error.tiene_correccion = 1;
            error.esperados = r.esperados;  /* Los del lugar donde falló el parser. */
        } else {
            error.esperados = r.esperados;

            /* ¿Le falta un operando a una operación? Para seguir analizando
             * se supone un operando (un nombre) en el lugar del error, pero
             * solo si así el análisis avanza de verdad. */
            size_t operador = 0;
            int lado = falta_operando(actual, k, &r.esperados, &operador);
            Ll1Correccion supuesto = con_terminal(LL1_INSERTAR, TOKEN_IDENTIFIER);
            if (lado != LL1_SIN_FALTA) {
                TokenType *prueba = malloc((n + LL1_MAX_INSERCION) * sizeof *prueba);
                if (prueba == NULL) {
                    estado = -1;
                    break;
                }
                size_t avance = probar(actual, n, k, &supuesto, prueba);
                free(prueba);
                if (avance == (size_t)-1) {
                    estado = -1;
                    break;
                }
                if (avance < AVANCE_MINIMO) lado = LL1_SIN_FALTA;  /* No era eso. */
            }

            if (lado != LL1_SIN_FALTA) {
                /* El error se marca en el operador, y NO tiene corrección
                 * (no se inventa el valor). */
                error.falta_operando = lado;
                error.operador = actual[operador];
                error.posicion = mapa[operador];
                error.anterior = (operador > 0) ? mapa[operador - 1] : SIZE_MAX;
                error.encontrado = actual[operador];
                if (guardar_error(errores, &error) != 0) {
                    estado = -1;
                    break;
                }
                size_t primero_original;
                n = aplicar(actual, mapa, n, k, &supuesto, siguiente, mapa_siguiente,
                            &primero_original);
                reanudar = SIZE_MAX;
                goto cambiar_listas;
            }

            int hay = ll1_mejor_correccion(actual, n, &c);
            if (hay < 0) {
                estado = -1;
                break;
            }
            error.tiene_correccion = hay;
        }
        error.posicion = mapa[k];
        error.anterior = (k > 0) ? mapa[k - 1] : SIZE_MAX;
        error.encontrado = actual[k];
        if (error.tiene_correccion) error.correccion = c;
        if (guardar_error(errores, &error) != 0) {
            estado = -1;
            break;
        }

        /* Seguir con la corrección aplicada. Sin corrección: modo pánico
         * (se descarta la sentencia); si el error es el final del archivo,
         * ya no hay nada más que analizar. */
        if (error.tiene_correccion) {
            size_t primero_original;
            n = aplicar(actual, mapa, n, k, &c, siguiente, mapa_siguiente, &primero_original);
            reanudar = SIZE_MAX;
        } else {
            if (actual[k] == TOKEN_EOF) break;
            n = descartar_sentencia(actual, mapa, n, k, siguiente, mapa_siguiente, &reanudar);
            hubo_panico = 1;
        }

    cambiar_listas:;

        /* La lista nueva pasa a ser la actual. */
        TokenType *t = actual;
        actual = siguiente;
        siguiente = t;
        size_t *mp = mapa;
        mapa = mapa_siguiente;
        mapa_siguiente = mp;
    }

fin:
    free(actual);
    free(siguiente);
    free(mapa);
    free(mapa_siguiente);
    if (estado != 0) ll1_liberar_errores(errores);
    return estado;
}

int ll1_analizar_con_recuperacion(const TokenType *tokens, const char *const *lexemas,
                                  size_t cantidad, Ll1Errores *errores)
{
    return recuperar(tokens, lexemas, cantidad, errores);
}

void ll1_liberar_errores(Ll1Errores *errores)
{
    free(errores->elementos);
    memset(errores, 0, sizeof *errores);
}
