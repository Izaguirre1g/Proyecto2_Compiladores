#include "semantic.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Etapa 2N-A: ruta de un modulo BAL relativa al archivo importador.
 * El resultado es propiedad del llamador (free).
 * No abre archivos ni registra simbolos.
 */
char *sem_ruta_modulo(const char *archivo_origen, const char *nombre_modulo)
{
    if (!archivo_origen || !*archivo_origen ||
        !nombre_modulo || !*nombre_modulo) {
        return NULL;
    }

    /* Los nombres de bring son identificadores, no rutas arbitrarias. */
    for (const unsigned char *p = (const unsigned char *)nombre_modulo;
         *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_')) {
            return NULL;
        }
    }

    const char *separador = strrchr(archivo_origen, '/');
    size_t prefijo = separador
        ? (size_t)(separador - archivo_origen + 1)
        : 0;
    size_t nombre = strlen(nombre_modulo);
    const size_t sufijo = sizeof(".bal"); /* incluye NUL */

    if (prefijo > SIZE_MAX - nombre ||
        prefijo + nombre > SIZE_MAX - sufijo) {
        return NULL;
    }

    char *ruta = malloc(prefijo + nombre + sufijo);
    if (!ruta) {
        return NULL;
    }

    if (prefijo) {
        memcpy(ruta, archivo_origen, prefijo);
    }
    memcpy(ruta + prefijo, nombre_modulo, nombre);
    memcpy(ruta + prefijo + nombre, ".bal", sufijo);
    return ruta;
}

//Contexto del análisis semántico
//Almacena la tabla de símbolos, el archivo actual y los errores
typedef struct {
    SimTabla *tabla;

    const Ast *arbol;

    const char *archivo;
    FILE *diagnosticos;

    int errores;
    int fatal;

    unsigned bloques;
    SemResolverModulo resolver;
    void *contexto_modulos;


    //Información de la función que estamos analizando actualmente

    SimTipo retorno_actual;

    int dentro_funcion;

} Sem;

//Obtiene un hijo del AST según su posición
// Los hijos están conectados mediante primer_hijo y siguiente
static const AstNodo *hijo(
    const AstNodo *n,
    unsigned indice
) {
    const AstNodo *p =
        n ? n->primer_hijo : NULL;

    while (p && indice--) {
        p = p->siguiente;
    }

    return p;
}

//Registra un error semántico, incluye archivo, línea y columna
 
static void error(
    Sem *s,
    const AstNodo *n,
    const char *formato,
    ...
) {
    ++s->errores;

    if (!s->diagnosticos) {
        return;
    }

    fprintf(
        s->diagnosticos,
        "%s:%zu:%zu: error semantico: ",
        s->archivo ? s->archivo : "<fuente>",
        n ? n->ubicacion.first_line : 0,
        n ? n->ubicacion.first_column : 0
    );

    va_list args;

    va_start(args, formato);

    vfprintf(
        s->diagnosticos,
        formato,
        args
    );

    va_end(args);

    fputc('\n', s->diagnosticos);
}

//Convierte los tipos del AST a los tipos de la tabla
 
static SimTipo tipo_simple(const AstNodo *n)
{
    if (!n ||
        n->tipo != AST_TIPO ||
        !n->texto) {
        return SIM_TIPO_ERROR;
    }

    if (!strcmp(n->texto, "declare_int")) {
        return SIM_TIPO_INT;
    }

    if (!strcmp(n->texto, "declare_boolean")) {
        return SIM_TIPO_BOOLEAN;
    }

    if (!strcmp(n->texto, "declare_text")) {
        return SIM_TIPO_TEXT;
    }

    if (!strcmp(n->texto, "declare_char")) {
        return SIM_TIPO_CHAR;
    }

    if (!strcmp(n->texto, "declare_infinite_void")) {
        return SIM_TIPO_VOID;
    }

    return SIM_TIPO_ERROR;
}

//Obtiene el tipo base de una declaración
 
static SimTipo tipo_declarado(const AstNodo *n)
{
    if (n &&
        (n->tipo == AST_TIPO_ARREGLO ||
         n->tipo == AST_TIPO_LISTA)) {

        return tipo_simple(hijo(n, 0));
    }

    return tipo_simple(n);
}


/*
 * Busca la declaración de una función
 * dentro del AST del programa.
 *
 * Devuelve el nodo AST_FUNCION correspondiente.
 *
 * No modifica el árbol.
 */
static const AstNodo *buscar_funcion(
    const Sem *s,
    const char *nombre
)
{
    if (!s || !s->arbol ||
        !s->arbol->raiz || !nombre) {
        return NULL;
    }

    const AstNodo *programa = s->arbol->raiz;

    for (
        const AstNodo *n = programa->primer_hijo;
        n;
        n = n->siguiente
    ) {
        if (n->tipo != AST_FUNCION) {
            continue;
        }

        const AstNodo *id = hijo(n, 0);

        if (id &&
            id->tipo == AST_IDENTIFICADOR &&
            id->texto &&
            strcmp(id->texto, nombre) == 0) {
            return n;
        }
    }

    return NULL;
}

//Cuenta los hijos directos de un nodo
//Se utiliza para contar argumentos y parámetros

static size_t contar_hijos(const AstNodo *n)
{
    size_t cantidad = 0;

    if (!n) {
        return 0;
    }

    for (
        const AstNodo *p = n->primer_hijo;
        p;
        p = p->siguiente
    ) {
        ++cantidad;
    }

    return cantidad;
}

//Registra un símbolo utilizando la tabla de símbolos
// También detecta declaraciones duplicadas.
 
static int registrar(
    Sem *s,
    const AstNodo *nombre,
    SimClase clase,
    SimTipo tipo,
    SimSimbolo **out
) {
    if (out) {
        *out = NULL;
    }

    if (!nombre ||
        nombre->tipo != AST_IDENTIFICADOR ||
        !nombre->texto) {

        error(
            s,
            nombre,
            "identificador invalido"
        );

        return 0;
    }

    SimSimbolo *creado = NULL;
    SimResultado r = sim_insertar(
        s->tabla,
        nombre->texto,
        clase,
        tipo,
        nombre->ubicacion.first_line,
        nombre->ubicacion.first_column,
        &creado
    );
    if (r == SIM_OK) {
        creado->declaracion = nombre;
        if (out) *out = creado;
    }

    if (r == SIM_DUPLICADO) {

        error(
            s,
            nombre,
            "'%s' ya fue declarado en este ambito",
            nombre->texto
        );

    } else if (r != SIM_OK) {

        error(
            s,
            nombre,
            "no se pudo registrar '%s'",
            nombre->texto
        );

        if (r == SIM_SIN_MEMORIA) {
            s->fatal = 1;
        }
    }

    return r == SIM_OK;
}

//Declaraciones adelantadas
//Permiten que las funciones se llamen entre sí dentro de este archivo

static void analizar_intentar(
    Sem *s,
    const AstNodo *n
);

static const AstNodo *buscar_funcion_en(const Ast *arbol,
                                           const char *nombre)
{
    if (!arbol || !arbol->raiz || !nombre) return NULL;
    for (const AstNodo *n = arbol->raiz->primer_hijo; n; n = n->siguiente) {
        const AstNodo *id = hijo(n, 0);
        if (n->tipo == AST_FUNCION && id && id->texto &&
            strcmp(id->texto, nombre) == 0) return n;
    }
    return NULL;
}

/* Resuelve una referencia alias.miembro, siempre en el ambito global
 * del modulo importado. Nunca expone variables locales. */
static const SimSimbolo *resolver_miembro(Sem *s, const AstNodo *n,
                                        const Ast **modulo_ast)
{
    const AstNodo *base = hijo(n, 0);
    const AstNodo *miembro = hijo(n, 1);
    if (modulo_ast) *modulo_ast = NULL;
    if (!base || base->tipo != AST_IDENTIFICADOR || !base->texto ||
        !miembro || miembro->tipo != AST_IDENTIFICADOR || !miembro->texto) {
        error(s, n, "acceso a miembro de modulo invalido");
        return NULL;
    }
    SimSimbolo *alias = sim_buscar(s->tabla, base->texto);
    if (!alias) {
        error(s, base, "modulo '%s' no declarado", base->texto);
        return NULL;
    }
    if (alias->clase != SIM_MODULO) {
        error(s, base, "'%s' no es un modulo", base->texto);
        return NULL;
    }
    const Ast *ast_remoto = NULL;
    const SimTabla *tabla_remota = NULL;
    if (!s->resolver || !s->resolver(s->contexto_modulos, s->archivo,
                                     base->texto, &ast_remoto, &tabla_remota) ||
        !ast_remoto || !tabla_remota || !tabla_remota->global) {
        error(s, base, "modulo '%s' no disponible para resolver miembros",
              base->texto);
        return NULL;
    }
    const SimSimbolo *simbolo = NULL;
    for (const SimSimbolo *p = tabla_remota->primero; p; p = p->siguiente_orden) {
        if (p->ambito == tabla_remota->global &&
            strcmp(p->nombre, miembro->texto) == 0) {
            simbolo = p;
            break;
        }
    }
    if (!simbolo) {
        error(s, miembro, "el modulo '%s' no exporta '%s'",
              base->texto, miembro->texto);
        return NULL;
    }
    if (modulo_ast) *modulo_ast = ast_remoto;
    return simbolo;
}

static SimTipo analizar_acceso_miembro(
    Sem *s,
    const AstNodo *n
);
 
static SimTipo expresion(
    Sem *s,
    const AstNodo *n
);

static void sentencia(
    Sem *s,
    const AstNodo *n
);

/*
 *
 * Busca una colección declarada.
 *
 * Si solo_lista es 1, exige SIM_LISTA.
 * Si es 0, acepta listas y arreglos.
 *
 * Esta función no evalúa índices.
 */
static SimSimbolo *buscar_coleccion(
    Sem *s,
    const AstNodo *n,
    int solo_lista
)
{
    if (!n ||
        n->tipo != AST_IDENTIFICADOR ||
        !n->texto) {

        error(
            s,
            n,
            "se esperaba el identificador "
            "de una coleccion"
        );

        return NULL;
    }

    SimSimbolo *simbolo = sim_buscar(
        s->tabla,
        n->texto
    );

    if (!simbolo) {

        error(
            s,
            n,
            "identificador '%s' no declarado",
            n->texto
        );

        return NULL;
    }

    if (solo_lista) {

        if (simbolo->clase != SIM_LISTA) {

            error(
                s,
                n,
                "'%s' no es una lista",
                n->texto
            );

            return NULL;
        }

    } else {

        if (simbolo->clase != SIM_LISTA &&
            simbolo->dimensiones == 0) {

            error(
                s,
                n,
                "'%s' no es una coleccion",
                n->texto
            );

            return NULL;
        }
    }

    return simbolo;
}

/*
 * Comprueba los elementos usados para
 * inicializar una lista.
 *
 * Ejemplo:
 *
 * declare_list numeros * declare_int
 *     : [1, 2, 3];
 */
static void validar_literal_lista(
    Sem *s,
    const AstNodo *literal,
    const SimSimbolo *simbolo
)
{
    if (!literal || !simbolo) {
        return;
    }

    if (literal->tipo != AST_LITERAL_COLECCION) {

        error(
            s,
            literal,
            "una lista debe inicializarse "
            "con una coleccion"
        );

        return;
    }

    for (
        const AstNodo *elemento =
            literal->primer_hijo;
        elemento;
        elemento = elemento->siguiente
    ) {

        /*
         * En esta etapa, las listas
         * contienen elementos escalares.
         */
        if (elemento->tipo == AST_LITERAL_COLECCION) {

            error(
                s,
                elemento,
                "no se admiten colecciones "
                "anidadas como elementos "
                "de esta lista"
            );

            continue;
        }

        SimTipo recibido = expresion(
            s,
            elemento
        );

        if (recibido != SIM_TIPO_ERROR &&
            simbolo->tipo != SIM_TIPO_ERROR &&
            recibido != simbolo->tipo) {

            error(
                s,
                elemento,
                "elemento de lista incompatible: "
                "se esperaba %s, se obtuvo %s",
                sim_nombre_tipo(simbolo->tipo),
                sim_nombre_tipo(recibido)
            );
        }
    }
}

/*
 * Validacion de inicializadores de arreglos y matrices
 *
 * Ejemplo:
 * declare_int[2][3] : [
 *     [1, 2, 3],
 *     [4, 5, 6]
 * ]
 * Comprueba:
 * 1. Cantidad de elementos por dimension
 * 2. Estructura de las colecciones anidadas
 * 3. Tipo de los elementos individuales
 *
 * No calcula direcciones de memoria
 */

static int validar_literal_arreglo(
    Sem *s,
    const AstNodo *literal,
    const SimSimbolo *simbolo,
    unsigned profundidad
)
{
    if (!literal || !simbolo) {
        return 0;
    }

    /*
     * Cada nivel de un arreglo debe
     * representarse con una coleccion.
     */
    if (literal->tipo != AST_LITERAL_COLECCION) {

        error(
            s,
            literal,
            "se esperaba una coleccion "
            "en la dimension %u",
            profundidad + 1
        );

        return 0;
    }

    if (profundidad >= simbolo->dimensiones) {

        error(
            s,
            literal,
            "el inicializador tiene "
            "mas dimensiones que el arreglo"
        );

        return 0;
    }

    /*
     * Contar los elementos del nivel actual.
     */
    size_t cantidad = 0;

    for (
        const AstNodo *elemento = literal->primer_hijo;
        elemento;
        elemento = elemento->siguiente
    ) {
        ++cantidad;
    }

    /*
     * Si conocemos la longitud declarada,
     * comprobar que coincida exactamente.
     *
     * Una longitud cero se reserva para
     * dimensiones cuyo tamaño no es
     * conocido de forma estática.
     */
    if (simbolo->longitudes &&
        simbolo->longitudes[profundidad] != 0 &&
        cantidad != simbolo->longitudes[profundidad]) {

        error(
            s,
            literal,
            "dimension %u: se esperaban "
            "%zu elementos, se encontraron %zu",
            profundidad + 1,
            simbolo->longitudes[profundidad],
            cantidad
        );

        return 0;
    }

    int valido = 1;

    /*
     * ¿Estamos en la ultima dimension?
     *
     * Si es así, esperamos valores escalares.
     * En caso contrario, esperamos
     * colecciones anidadas.
     */
    int ultima_dimension = (
        profundidad + 1 == simbolo->dimensiones
    );

    for (
        const AstNodo *elemento = literal->primer_hijo;
        elemento;
        elemento = elemento->siguiente
    ) {

        if (!ultima_dimension) {

            /*
             * Las dimensiones intermedias
             * deben contener otras colecciones.
             */
            if (elemento->tipo != AST_LITERAL_COLECCION) {

                error(
                    s,
                    elemento,
                    "dimension %u: se esperaba "
                    "una coleccion anidada",
                    profundidad + 1
                );

                valido = 0;
                continue;
            }

            if (!validar_literal_arreglo(
                    s,
                    elemento,
                    simbolo,
                    profundidad + 1
                )) {
                valido = 0;
            }

            continue;
        }

        /*
         * En la ultima dimension deben
         * aparecer valores individuales.
         */
        if (elemento->tipo == AST_LITERAL_COLECCION) {

            error(
                s,
                elemento,
                "el arreglo contiene "
                "una dimension adicional"
            );

            valido = 0;
            continue;
        }

        /*
         * Reutilizar el analizador
         * de expresiones para conocer
         * el tipo de cada elemento.
         */
        SimTipo tipo_elemento = expresion(
            s,
            elemento
        );

        if (tipo_elemento == SIM_TIPO_ERROR) {
            valido = 0;
            continue;
        }

        if (tipo_elemento != simbolo->tipo) {

            error(
                s,
                elemento,
                "elemento de arreglo incompatible: "
                "se esperaba %s, se obtuvo %s",
                sim_nombre_tipo(simbolo->tipo),
                sim_nombre_tipo(tipo_elemento)
            );

            valido = 0;
        }
    }

    return valido;
}

/*
 * Convierte un literal entero positivo o cero
 * a size_t sin desbordamiento.
 *
 * Retorna:
 * 1: conversión correcta.
 * 0: no es un entero válido.
 */
static int entero_natural(
    const AstNodo *n,
    size_t *valor
)
{
    if (!n ||
        n->tipo != AST_ENTERO ||
        !n->texto ||
        !n->texto[0] ||
        n->texto[0] == '-') {
        return 0;
    }

    errno = 0;

    char *final = NULL;

    unsigned long long numero = strtoull(
        n->texto,
        &final,
        10
    );

    if (errno == ERANGE ||
        !final ||
        *final != '\0' ||
        numero > SIZE_MAX) {
        return 0;
    }

    if (valor) {
        *valor = (size_t)numero;
    }

    return 1;
}


/*
 *
 * Valida las dimensiones estaticas y dinamicas
 * de arreglos y matrices.
 *
 * Ejemplos validos:
 *
 * declare_int[5]
 * declare_int[filas]
 * declare_int[filas gauss 1]
 * declare_int[2][columnas]
 *
 * Convencion:
 *
 * longitudes[i] > 0:
 *     dimension conocida estaticamente.
 *
 * longitudes[i] == 0:
 *     dimension dinamica.
 */
static void validar_dimensiones(
    Sem *s,
    const AstNodo *tipo_n,
    SimSimbolo *simbolo
)
{
    if (!tipo_n ||
        tipo_n->tipo != AST_TIPO_ARREGLO ||
        !simbolo) {
        return;
    }

    const AstNodo *dims = hijo(
        tipo_n,
        1
    );

    if (!dims ||
        dims->tipo != AST_DIMENSIONES) {

        error(
            s,
            tipo_n,
            "declaracion de arreglo sin dimensiones"
        );

        return;
    }

    /*
     * Contar las dimensiones.
     */
    unsigned cantidad = 0;

    for (
        const AstNodo *d = dims->primer_hijo;
        d;
        d = d->siguiente
    ) {

        if (cantidad == UINT_MAX) {

            error(
                s,
                d,
                "demasiadas dimensiones"
            );

            return;
        }

        ++cantidad;
    }

    if (cantidad == 0) {

        error(
            s,
            tipo_n,
            "el arreglo necesita al menos "
            "una dimension"
        );

        return;
    }

    /*
     * Reservar las longitudes.
     */
    size_t *longitudes = calloc(
        cantidad,
        sizeof(*longitudes)
    );

    if (!longitudes) {

        error(
            s,
            tipo_n,
            "memoria insuficiente para dimensiones"
        );

        s->fatal = 1;
        return;
    }

    unsigned posicion = 0;

    /*
     * Comprobar cada dimension.
     */
    for (
        const AstNodo *d = dims->primer_hijo;
        d;
        d = d->siguiente
    ) {

        const AstNodo *tamano = hijo(
            d,
            0
        );

        size_t longitud = 0;

        /*
         * Una dimension vacia no es valida
         * para una variable declarada.
         */
        if (!tamano) {

            error(
                s,
                d,
                "la dimension %u no tiene "
                "una expresion de tamano",
                posicion + 1
            );

            ++posicion;
            continue;
        }

        /*
         * CASO 1:
         * Entero literal no negativo.
         *
         * Ejemplo:
         * declare_int[5]
         */
        if (tamano->tipo == AST_ENTERO) {

            if (!entero_natural(tamano, &longitud)) {

                error(
                    s,
                    d,
                    "la dimension %u debe ser "
                    "un entero constante positivo",
                    posicion + 1
                );

            } else if (longitud == 0) {

                error(
                    s,
                    d,
                    "la dimension %u debe "
                    "ser mayor que cero",
                    posicion + 1
                );

            } else {

                longitudes[posicion] = longitud;
            }

        /*
         * CASO 2:
         * Negacion de un entero literal.
         *
         * Ejemplos:
         *
         * neumann 2
         * neumann 0
         *
         * Ambos son invalidos como
         * dimensiones de un arreglo.
         */
        } else if (
            tamano->tipo == AST_UNARIO &&
            tamano->texto &&
            strcmp(
                tamano->texto,
                "neumann"
            ) == 0 &&
            hijo(tamano, 0) &&
            hijo(tamano, 0)->tipo == AST_ENTERO
        ) {

            error(
                s,
                tamano,
                "la dimension %u debe "
                "ser mayor que cero",
                posicion + 1
            );

        /*
         * CASO 3:
         * Expresion dinamica.
         *
         * Ejemplos:
         *
         * declare_int[filas]
         * declare_int[filas gauss 1]
         */
        } else {

            SimTipo tipo = expresion(
                s,
                tamano
            );

            if (tipo != SIM_TIPO_INT &&
                tipo != SIM_TIPO_ERROR) {

                error(
                    s,
                    tamano,
                    "la dimension %u debe "
                    "ser de tipo declare_int",
                    posicion + 1
                );
            }

            /*
             * No conocemos todavia la longitud.
             *
             * El valor cero representa
             * una dimension dinamica.
             */
            longitudes[posicion] = 0;
        }

        ++posicion;
    }

    /*
     * Guardar el numero de dimensiones
     * y sus longitudes en el simbolo.
     *
     * Mantenemos el mismo orden de
     * argumentos que ya utilizas.
     */
    SimResultado r = sim_definir_dimensiones(
        simbolo,
        longitudes,
        cantidad
    );

    if (r != SIM_OK) {

        error(
            s,
            tipo_n,
            "no se pudieron guardar "
            "las dimensiones del arreglo"
        );

        if (r == SIM_SIN_MEMORIA) {
            s->fatal = 1;
        }
    }

    free(longitudes);
}

/*
 * Obtiene la variable base de un acceso.
 *
 * Ejemplo:
 *
 * matriz[1][0]
 *
 * devuelve el identificador "matriz".
 */
static const AstNodo *base_indice(
    const AstNodo *n
)
{
    while (n && n->tipo == AST_INDICE) {
        n = hijo(n, 0);
    }

    return n;
}

/*
 * Cuenta cuántos índices tiene
 * un acceso a un arreglo.
 *
 * matriz[1][0] -> 2
 * vector[3]    -> 1
 */
static unsigned contar_indices(
    const AstNodo *n
)
{
    unsigned cantidad = 0;

    while (n && n->tipo == AST_INDICE) {

        if (cantidad == UINT_MAX) {
            return UINT_MAX;
        }

        ++cantidad;

        n = hijo(n, 0);
    }

    return cantidad;
}

/*
 * Analiza accesos indexados a:
 *
 * - Arreglos
 * - Matrices
 * - Listas
 *
 * Ejemplos:
 *
 * numeros[0]
 * matriz[1][2]
 * lista[i]
 *
 * Reglas:
 *
 * - Un escalar no admite indices.
 * - Una lista admite exactamente un indice.
 * - Un arreglo exige sus dimensiones declaradas.
 * - Todos los indices deben ser declare_int.
 * - Los limites de arreglos estaticos se
 *   comprueban cuando el indice es constante.
 */
static SimTipo analizar_indice(
    Sem *s,
    const AstNodo *n
)
{
    const AstNodo *base = base_indice(n);

    if (!base ||
        base->tipo != AST_IDENTIFICADOR ||
        !base->texto) {

        error(
            s,
            n,
            "acceso indexado invalido"
        );

        return SIM_TIPO_ERROR;
    }

    SimSimbolo *simbolo = sim_buscar(
        s->tabla,
        base->texto
    );

    if (!simbolo) {

        error(
            s,
            base,
            "coleccion '%s' no declarada",
            base->texto
        );

        return SIM_TIPO_ERROR;
    }

    /*
     * Distinguir listas de arreglos.
     */
    int es_lista = (
        simbolo->clase == SIM_LISTA
    );

    /*
     * Una lista tiene un solo indice.
     * Los arreglos usan sus dimensiones.
     */
    unsigned esperados = es_lista
        ? 1u
        : simbolo->dimensiones;

    /*
     * Una variable escalar no admite indices.
     */
    if (esperados == 0) {

        error(
            s,
            n,
            "'%s' no es una coleccion indexable",
            base->texto
        );

        return SIM_TIPO_ERROR;
    }

    unsigned cantidad = contar_indices(n);

    /*
     * Exigir la cantidad correcta de indices.
     */
    if (cantidad != esperados) {

        error(
            s,
            n,
            "coleccion '%s': se esperaban %u "
            "indices, se recibieron %u",
            base->texto,
            esperados,
            cantidad
        );

        return SIM_TIPO_ERROR;
    }

    /*
     * Recorrer los indices.
     *
     * En accesos anidados, el nodo externo
     * contiene el ultimo indice.
     */
    const AstNodo *actual = n;

    unsigned posicion = cantidad;

    int valido = 1;

    while (actual &&
           actual->tipo == AST_INDICE) {

        --posicion;

        const AstNodo *indice = hijo(
            actual,
            1
        );

        SimTipo tipo = expresion(
            s,
            indice
        );

        /*
        * Detectar indices negativos constantes.
        *
        * Ejemplo:
        *
        * numeros[neumann 1]
        *
        * Esta comprobacion no depende
        * del tamano de la coleccion.
        */
        if (
            indice &&
            indice->tipo == AST_UNARIO &&
            indice->texto &&
            strcmp(
                indice->texto,
                "neumann"
            ) == 0 &&
            hijo(indice, 0) &&
            hijo(indice, 0)->tipo == AST_ENTERO
        ) {

            size_t magnitud = 0;

            if (
                entero_natural(
                    hijo(indice, 0),
                    &magnitud
                ) &&
                magnitud > 0
            ) {

                error(
                    s,
                    indice,
                    "el indice de una coleccion "
                    "no puede ser negativo"
                );

                valido = 0;
            }
        }

        /*
         * Validacion de tipo.
         */
        if (tipo != SIM_TIPO_INT &&
            tipo != SIM_TIPO_ERROR) {

            error(
                s,
                indice,
                "el indice de una coleccion "
                "debe ser declare_int"
            );

            valido = 0;
        }

        if (tipo == SIM_TIPO_ERROR) {
            valido = 0;
        }

        /*
         * Los indices de listas no se pueden
         * comprobar contra un tamano fijo
         * durante el analisis semantico.
         *
         * Para arreglos estaticos conservamos
         * la comprobacion de limites.
         */
        if (!es_lista &&
            tipo == SIM_TIPO_INT &&
            indice &&
            indice->tipo == AST_ENTERO) {

            size_t valor = 0;

            if (!entero_natural(
                    indice,
                    &valor
                )) {

                error(
                    s,
                    indice,
                    "indice entero invalido"
                );

                valido = 0;

            } else if (
                simbolo->longitudes &&
                simbolo->longitudes[posicion] != 0 &&
                valor >= simbolo->longitudes[posicion]
            ) {

                error(
                    s,
                    indice,
                    "indice %zu fuera de rango "
                    "en dimension %u de '%s' "
                    "(longitud %zu)",
                    valor,
                    posicion + 1,
                    base->texto,
                    simbolo->longitudes[posicion]
                );

                valido = 0;
            }
        }

        actual = hijo(
            actual,
            0
        );
    }

    /*
     * El resultado de lista[i] o
     * arreglo[i] es el tipo del elemento.
     */
    return valido
        ? simbolo->tipo
        : SIM_TIPO_ERROR;
}


//Analiza declaraciones de variables, constantes y listas
static void declarar(
    Sem *s,
    const AstNodo *n
) {
    const AstNodo *nombre = hijo(n, 0);
    const AstNodo *tipo_n = hijo(n, 1);
    const AstNodo *inicial = hijo(n, 2);

    SimTipo tipo = tipo_declarado(tipo_n);

    SimClase clase =
        n->tipo == AST_CONSTANTE ? SIM_CONSTANTE :
        n->tipo == AST_LISTA ? SIM_LISTA :
        SIM_VARIABLE;

    //Comprueba el tipo declarado
     
    if (tipo == SIM_TIPO_ERROR ||
        tipo == SIM_TIPO_VOID) {

        error(
            s,
            tipo_n,
            "tipo de variable invalido"
        );
    }

    //Registra la variable
     
    SimSimbolo *simbolo = NULL;

    int agregado = registrar(
        s,
        nombre,
        clase,
        tipo,
        &simbolo
    );

    //Si la variable es un arreglo registra sus dimensiones
    if (agregado &&
        simbolo &&
        tipo_n &&
        tipo_n->tipo == AST_TIPO_ARREGLO) {

        validar_dimensiones(
            s,
            tipo_n,
            simbolo
        );
    }

    //Comprueba el inicializador
     
    if (inicial) {

        /*
        * Una lista no puede inicializarse
        * directamente con un valor escalar.
        */
        if (clase == SIM_LISTA &&
            inicial->tipo != AST_LITERAL_COLECCION) {

            (void)expresion(s, inicial);

            error(
                s,
                inicial,
                "una lista debe inicializarse "
                "con un literal de coleccion"
            );

            return;
        }

        /*
        * Inicializador de arreglo o matriz.
        */
        if (inicial->tipo == AST_LITERAL_COLECCION &&
            tipo_n &&
            tipo_n->tipo == AST_TIPO_ARREGLO) {

            /*
            * Solo podemos validar la forma
            * después de registrar el símbolo
            * y guardar sus dimensiones.
            */
            if (agregado && simbolo) {

                (void)validar_literal_arreglo(
                    s,
                    inicial,
                    simbolo,
                    0
                );
            }

            return;
        }

        /*
        * Un literal de colección no es
        * compatible con una variable escalar
        */
        if (inicial->tipo == AST_LITERAL_COLECCION) {

            /*
            * Las listas sí admiten
            * literales de colección.
            */
            if (clase == SIM_LISTA) {

                if (agregado && simbolo) {

                    validar_literal_lista(
                        s,
                        inicial,
                        simbolo
                    );
                }

                return;
            }

            /*
            * Una variable escalar no admite
            * un literal de colección.
            */
            error(
                s,
                inicial,
                "no se puede inicializar "
                "una variable escalar con "
                "una coleccion"
            );

            return;
        }

        /*
        * Todavía no aceptamos un escalar
        * como inicializador de un arreglo
        * completo.
        */
        if (tipo_n &&
            tipo_n->tipo == AST_TIPO_ARREGLO) {

            /*
            * Evaluar la expresión para
            * conservar los diagnósticos de
            * identificadores no declarados.
            */
            (void)expresion(s, inicial);

            error(
                s,
                inicial,
                "un arreglo debe inicializarse "
                "con una coleccion compatible"
            );

            return;
        }

        /*
        * Inicializadores escalares:
        * conservar las comprobaciones
        * de tipos de las etapas anteriores.
        */
        SimTipo origen = expresion(
            s,
            inicial
        );

        if (agregado &&
            tipo != SIM_TIPO_ERROR &&
            origen != SIM_TIPO_ERROR &&
            tipo != origen) {

            error(
                s,
                inicial,
                "inicializador incompatible: "
                "se esperaba %s, se obtuvo %s",
                sim_nombre_tipo(tipo),
                sim_nombre_tipo(origen)
            );
        }
    }
}

/* Compatibilidad de argumentos compuestos
 *
 * Verifica que un argumento coincida con
 * un parametro declarado como:
 *
 * - AST_TIPO_ARREGLO
 * - AST_TIPO_LISTA
 *
 * Para arreglos comprueba:
 *
 * - Que el argumento sea un arreglo.
 * - Que coincida el tipo de elemento.
 * - Que coincida la cantidad de dimensiones.
 *
 * Para listas comprueba:
 *
 * - Que el argumento sea una lista.
 * - Que coincida el tipo de elemento.
 *
 * Las longitudes dinamicas no se comparan
 * en esta etapa.
 */
static int argumento_compuesto_compatible(
    Sem *s,
    const AstNodo *argumento,
    const AstNodo *tipo_parametro,
    const char *nombre_funcion,
    size_t posicion
)
{
    if (!argumento || !tipo_parametro) {
        return 0;
    }

    /*
     * En esta primera version,
     * un argumento compuesto debe ser
     * el identificador de una coleccion.
     *
     * Ejemplos:
     *
     * procesar(numeros)
     * procesar(matriz)
     */
    if (argumento->tipo != AST_IDENTIFICADOR ||
        !argumento->texto) {

        /*
         * Conservar el analisis de la
         * expresion para diagnosticar
         * referencias no declaradas.
         */
        (void)expresion(s, argumento);

        error(
            s,
            argumento,
            "argumento %zu de '%s': "
            "se esperaba una coleccion",
            posicion,
            nombre_funcion
        );

        return 0;
    }

    SimSimbolo *simbolo = sim_buscar(
        s->tabla,
        argumento->texto
    );

    if (!simbolo) {

        error(
            s,
            argumento,
            "identificador '%s' no declarado",
            argumento->texto
        );

        return 0;
    }

    /*
     * Obtener el tipo del elemento.
     */
    SimTipo esperado = tipo_declarado(
        tipo_parametro
    );

    /*
     * Comprobar parametros de listas.
     */
    if (tipo_parametro->tipo == AST_TIPO_LISTA) {

        if (simbolo->clase != SIM_LISTA) {

            error(
                s,
                argumento,
                "argumento %zu de '%s': "
                "se esperaba una lista",
                posicion,
                nombre_funcion
            );

            return 0;
        }

        if (esperado != SIM_TIPO_ERROR &&
            simbolo->tipo != SIM_TIPO_ERROR &&
            esperado != simbolo->tipo) {

            error(
                s,
                argumento,
                "argumento %zu de '%s': "
                "tipo de elemento incompatible "
                "(se esperaba %s, se obtuvo %s)",
                posicion,
                nombre_funcion,
                sim_nombre_tipo(esperado),
                sim_nombre_tipo(simbolo->tipo)
            );

            return 0;
        }

        return 1;
    }

    /*
     * Comprobar parametros de arreglos.
     */
    if (tipo_parametro->tipo == AST_TIPO_ARREGLO) {

        /*
         * Una lista no debe confundirse
         * con un arreglo.
         */
        if (simbolo->clase == SIM_LISTA ||
            simbolo->dimensiones == 0) {

            error(
                s,
                argumento,
                "argumento %zu de '%s': "
                "se esperaba un arreglo",
                posicion,
                nombre_funcion
            );

            return 0;
        }

        /*
         * Comparar el tipo de elemento.
         */
        if (esperado != SIM_TIPO_ERROR &&
            simbolo->tipo != SIM_TIPO_ERROR &&
            esperado != simbolo->tipo) {

            error(
                s,
                argumento,
                "argumento %zu de '%s': "
                "tipo de elemento incompatible "
                "(se esperaba %s, se obtuvo %s)",
                posicion,
                nombre_funcion,
                sim_nombre_tipo(esperado),
                sim_nombre_tipo(simbolo->tipo)
            );

            return 0;
        }

        /*
         * AST_TIPO_ARREGLO:
         *
         * hijo 0: tipo base
         * hijo 1: AST_DIMENSIONES
         */
        const AstNodo *dims = hijo(
            tipo_parametro,
            1
        );

        unsigned dimensiones_esperadas = 0;

        for (
            const AstNodo *d =
                dims ? dims->primer_hijo : NULL;
            d;
            d = d->siguiente
        ) {
            ++dimensiones_esperadas;
        }

        if (dimensiones_esperadas !=
            simbolo->dimensiones) {

            error(
                s,
                argumento,
                "argumento %zu de '%s': "
                "se esperaba un arreglo de "
                "%u dimensiones, se obtuvo uno de %u",
                posicion,
                nombre_funcion,
                dimensiones_esperadas,
                simbolo->dimensiones
            );

            return 0;
        }

        /*
        * Comprobar las longitudes estaticas
        * declaradas en los parametros.
        *
        * Una longitud cero representa una
        * dimension cuyo valor no se conoce
        * durante la compilacion.
        *
        * Ejemplo:
        *
        * parametro: declare_int[5]
        * argumento: declare_int[3]
        *
        * Debe producir un error.
        */
        unsigned indice_dimension = 0;

        for (
            const AstNodo *dimension =
                dims ? dims->primer_hijo : NULL;
            dimension;
            dimension = dimension->siguiente
        ) {

            const AstNodo *tamano = hijo(
                dimension,
                0
            );

            /*
            * Si el parametro tiene una
            * dimension abierta o dinamica,
            * no existe una longitud estatica
            * que podamos exigir aqui.
            */
            if (!tamano ||
                tamano->tipo != AST_ENTERO) {

                ++indice_dimension;
                continue;
            }

            size_t longitud_esperada = 0;

            if (!entero_natural(
                    tamano,
                    &longitud_esperada
                ) || longitud_esperada == 0) {

                /*
                * La declaracion del parametro
                * se valida en registrar_parametro().
                */
                ++indice_dimension;
                continue;
            }

            /*
            * Si el argumento tiene longitud
            * dinamica, no podemos comparar
            * su valor durante la compilacion.
            */
            if (!simbolo->longitudes ||
                simbolo->longitudes[indice_dimension] == 0) {

                ++indice_dimension;
                continue;
            }

            size_t longitud_recibida =
                simbolo->longitudes[indice_dimension];

            if (longitud_recibida !=
                longitud_esperada) {

                error(
                    s,
                    argumento,
                    "argumento %zu de '%s': "
                    "dimension %u incompatible "
                    "(se esperaba longitud %zu, "
                    "se obtuvo %zu)",
                    posicion,
                    nombre_funcion,
                    indice_dimension + 1,
                    longitud_esperada,
                    longitud_recibida
                );

                return 0;
            }

            ++indice_dimension;
        }

        return 1;
    }

    return 0;
}

//Determina el tipo de una expresión
//También comprueba identificadores, operadores y llamadas básicas
 
static SimTipo expresion(
    Sem *s,
    const AstNodo *n
) {
    if (!n) {
        return SIM_TIPO_ERROR;
    }

    switch (n->tipo) {

        //Literales
         
        case AST_ENTERO:
            return SIM_TIPO_INT;

        case AST_BOOLEANO:
            return SIM_TIPO_BOOLEAN;

        case AST_CADENA:
            return SIM_TIPO_TEXT;

        case AST_CARACTER:
            return SIM_TIPO_CHAR;

        //Uso de identificadores
         
        case AST_IDENTIFICADOR: {

            SimSimbolo *simbolo = sim_buscar(
                s->tabla,
                n->texto
            );

            if (!simbolo) {

                error(
                    s,
                    n,
                    "identificador '%s' no declarado",
                    n->texto
                );

                return SIM_TIPO_ERROR;
            }

            //Una función o módulo no debe de utilizarse como variable
             
            if (simbolo->clase == SIM_FUNCION ||
                simbolo->clase == SIM_MODULO) {

                error(
                    s,
                    n,
                    "'%s' no es una variable",
                    n->texto
                );

                return SIM_TIPO_ERROR;
            }

            /*
            * Un arreglo o lista completa no es
            * una expresion escalar.
            *
            * Las colecciones completas se permiten
            * solamente en contextos que las admiten
            * explicitamente, como los parametros
            * compuestos.
            */
            if (simbolo->clase == SIM_LISTA ||
                simbolo->dimensiones > 0) {

                error(
                    s,
                    n,
                    "la coleccion '%s' no puede "
                    "utilizarse como valor escalar",
                    n->texto
                );

                return SIM_TIPO_ERROR;
            }

            return simbolo->tipo;
        }

        //Operadores unarios
         
        case AST_UNARIO: {

            SimTipo t = expresion(
                s,
                hijo(n, 0)
            );

            if (t == SIM_TIPO_ERROR) {
                return t;
            }

            SimTipo esperado =
                n->texto &&
                !strcmp(n->texto, "~")
                ? SIM_TIPO_BOOLEAN
                : SIM_TIPO_INT;

            if (t != esperado) {

                error(
                    s,
                    n,
                    "operador unario requiere %s",
                    sim_nombre_tipo(esperado)
                );

                return SIM_TIPO_ERROR;
            }

            return esperado;
        }

        //Operadores binarios
         
        case AST_BINARIO: {

            SimTipo a = expresion(
                s,
                hijo(n, 0)
            );

            SimTipo b = expresion(
                s,
                hijo(n, 1)
            );

            if (a == SIM_TIPO_ERROR ||
                b == SIM_TIPO_ERROR) {
                return SIM_TIPO_ERROR;
            }

            const char *op =
                n->texto ? n->texto : "";

            //Operadores lógicos
             
            int logico =
                !strcmp(op, "&&") ||
                !strcmp(op, "||") ||
                !strcmp(op, "^");

            //Comparaciones de igualdad
             
            int igualdad =
                !strcmp(op, "==") ||
                !strcmp(op, "=/=");

            //Comparaciones de orden
             
            int orden =
                !strcmp(op, "<") ||
                !strcmp(op, ">") ||
                !strcmp(op, "<=") ||
                !strcmp(op, ">=");

            SimTipo esperado =
                logico
                ? SIM_TIPO_BOOLEAN
                : SIM_TIPO_INT;

            //Igualdad: ambos operandos deben tener el mismo tipo
             
            if (igualdad) {

                if (a == b) {
                    return SIM_TIPO_BOOLEAN;
                }

                error(
                    s,
                    n,
                    "comparacion entre tipos incompatibles"
                );

                return SIM_TIPO_ERROR;
            }

            //Validar operandos
            if (a != esperado ||
                b != esperado) {

                error(
                    s,
                    n,
                    "operador '%s' requiere operandos %s",
                    op,
                    sim_nombre_tipo(esperado)
                );

                return SIM_TIPO_ERROR;
            }

            //Las comparaciones y operaciones lógicas devuelven booleano.
             
            return logico || orden
                ? SIM_TIPO_BOOLEAN
                : SIM_TIPO_INT;
        }

        //Llamadas a funciones
        /* Verifica:
        * - Existencia de la función.
        * - Cantidad de argumentos.
        * - Compatibilidad de tipos.
        */
        case AST_LLAMADA: {

            const AstNodo *dest = hijo(n, 0);
            const AstNodo *args = hijo(n, 1);

            const SimSimbolo *funcion = NULL;
            const AstNodo *declaracion = NULL;
            const char *nombre_funcion = NULL;
            if (dest && dest->tipo == AST_IDENTIFICADOR && dest->texto) {
                funcion = sim_buscar(s->tabla, dest->texto);
                nombre_funcion = dest->texto;
                if (funcion && funcion->clase == SIM_FUNCION)
                    declaracion = buscar_funcion(s, nombre_funcion);
            } else if (dest && dest->tipo == AST_ACCESO_MIEMBRO) {
                const Ast *ast_remoto = NULL;
                funcion = resolver_miembro(s, dest, &ast_remoto);
                const AstNodo *id = hijo(dest, 1);
                nombre_funcion = id ? id->texto : NULL;
                if (funcion && funcion->clase == SIM_FUNCION)
                    declaracion = buscar_funcion_en(ast_remoto, nombre_funcion);
            } else {
                error(s, n, "destino de llamada invalido");
            }
            if (!funcion || funcion->clase != SIM_FUNCION || !declaracion) {
                if (funcion && funcion->clase != SIM_FUNCION)
                    error(s, dest, "'%s' no es una funcion",
                          nombre_funcion ? nombre_funcion : "<desconocido>");
                else if (dest && dest->tipo == AST_IDENTIFICADOR)
                    error(s, dest, "funcion '%s' no declarada o sin firma",
                          nombre_funcion);
                else if (funcion)
                    error(s, dest, "no se encontro la firma de '%s'",
                          nombre_funcion ? nombre_funcion : "<desconocido>");
                for (const AstNodo *a = args ? args->primer_hijo : NULL;
                     a; a = a->siguiente) (void)expresion(s, a);
                return SIM_TIPO_ERROR;
            }

            /*
            * AST_FUNCION:
            *
            * hijo 0: nombre
            * hijo 1: retorno
            * hijo 2: parámetros
            * hijo 3: cuerpo
            */
            const AstNodo *params = hijo(
                declaracion,
                2
            );

            /*
            * Verificar cantidad de argumentos.
            */
            size_t esperados = contar_hijos(params);
            size_t recibidos = contar_hijos(args);

            if (esperados != recibidos) {

                error(
                    s,
                    n,
                    "funcion '%s' esperaba %zu "
                    "argumentos, pero recibio %zu",
                    nombre_funcion,
                    esperados,
                    recibidos
                );
            }

            /*
            * Comparar cada argumento con su
            * parámetro correspondiente.
            */
            const AstNodo *parametro =
                params ? params->primer_hijo : NULL;

            const AstNodo *argumento =
                args ? args->primer_hijo : NULL;

            size_t posicion = 1;

            while (parametro && argumento) {

                /*
                * AST_PARAMETRO:
                *
                * hijo 0: identificador
                * hijo 1: tipo
                */
                /*
                * Obtener el tipo del parametro.
                */
                const AstNodo *tipo_parametro = hijo(
                    parametro,
                    1
                );

                SimTipo esperado = tipo_declarado(
                    tipo_parametro
                );

                /*
                * Distinguir tipos compuestos
                * de tipos escalares.
                */
                int compuesto =
                    tipo_parametro &&
                    (
                        tipo_parametro->tipo == AST_TIPO_ARREGLO ||
                        tipo_parametro->tipo == AST_TIPO_LISTA
                    );

                if (compuesto) {

                    /*
                    * Validar arreglos, matrices y listas.
                    */
                    (void)argumento_compuesto_compatible(
                        s,
                        argumento,
                        tipo_parametro,
                        nombre_funcion,
                        posicion
                    );

                } else {

                    /*
                    * Mantener la validacion anterior
                    * de argumentos escalares.
                    */
                    SimTipo recibido = expresion(
                        s,
                        argumento
                    );

                    /*
                    * Un arreglo o lista completa
                    * no puede utilizarse como si
                    * fuera un argumento escalar.
                    */
                    int argumento_es_compuesto = 0;

                    if (argumento &&
                        argumento->tipo == AST_IDENTIFICADOR &&
                        argumento->texto) {

                        SimSimbolo *simbolo = sim_buscar(
                            s->tabla,
                            argumento->texto
                        );

                        if (simbolo &&
                            (
                                simbolo->clase == SIM_LISTA ||
                                simbolo->dimensiones > 0
                            )) {

                            argumento_es_compuesto = 1;

                            error(
                                s,
                                argumento,
                                "argumento %zu de '%s': "
                                "no se puede pasar una "
                                "coleccion completa a un "
                                "parametro escalar",
                                posicion,
                                nombre_funcion
                            );
                        }
                    }

                    if (!argumento_es_compuesto &&
                        esperado != SIM_TIPO_ERROR &&
                        recibido != SIM_TIPO_ERROR &&
                        esperado != recibido) {

                        error(
                            s,
                            argumento,
                            "argumento %zu de '%s': "
                            "se esperaba %s, se obtuvo %s",
                            posicion,
                            nombre_funcion,
                            sim_nombre_tipo(esperado),
                            sim_nombre_tipo(recibido)
                        );
                    }
                }

                parametro = parametro->siguiente;
                argumento = argumento->siguiente;

                ++posicion;
            }

            /*
            * Analizar los argumentos sobrantes,
            * incluso cuando la cantidad es incorrecta.
            */
            while (argumento) {
                (void)expresion(s, argumento);
                argumento = argumento->siguiente;
            }

            /*
            * El tipo de una llamada es el tipo
            * de retorno de su función.
            */
            return funcion->tipo;
        }

        case AST_ACCESO_MIEMBRO:
            return analizar_acceso_miembro(
                s,
                n
            );
        //Acceso a arreglos

    
        case AST_INDICE:

            return analizar_indice(
                s,
                n
            );

        //Tamaño de una colección
        //El resultado es entero
         
        /*
        * size(lista)
        * size(arreglo)
        *
        * Devuelve declare_int solo cuando
        * el argumento es una coleccion valida.
        */
        case AST_TAMANO: {

            const AstNodo *argumento = hijo(n, 0);

            SimSimbolo *coleccion = buscar_coleccion(
                s,
                argumento,
                0
            );

            if (!coleccion) {
                return SIM_TIPO_ERROR;
            }

            return SIM_TIPO_INT;
        }



        //Nodos todavía no implementados
         
        default:

            error(
                s,
                n,
                "expresion de tipo AST %s pendiente",
                ast_nombre_tipo(n->tipo)
            );

            return SIM_TIPO_ERROR;
    }
}

//Analiza un bloque de instrucciones.
//Puede crear un ámbito nuevo para las variables declaradas dentro del bloque
 
static void bloque(
    Sem *s,
    const AstNodo *n,
    int ambito_nuevo
) {
    if (!n) {
        return;
    }

    int entro = 0;

    if (ambito_nuevo) {

        char nombre[32];

        snprintf(
            nombre,
            sizeof(nombre),
            "bloque_%u",
            ++s->bloques
        );

        SimResultado r = sim_entrar_ambito(
            s->tabla,
            nombre
        );

        if (r != SIM_OK) {

            error(
                s,
                n,
                "no se pudo crear el ambito"
            );

            if (r == SIM_SIN_MEMORIA) {
                s->fatal = 1;
            }

            return;
        }

        entro = 1;
    }

    s->tabla->actual->bloque = n;

    //Recorre las sentencias en orden
     
    for (
        const AstNodo *p = n->primer_hijo;
        p;
        p = p->siguiente
    ) {
        sentencia(s, p);
    }

    //Vuelve al ambito padre
     
    if (entro) {
        (void)sim_salir_ambito(s->tabla);
    }
}

/* ETAPA 2J
 * Analisis semantico de seek / seize
 *
 * AST_INTENTAR:
 *
 * hijo 0: AST_BLOQUE protegido
 * hijo 1: AST_CAPTURAS
 *
 * AST_CAPTURA:
 *
 * hijo 0: tipo de excepcion
 * hijo 1: variable capturada
 * hijo 2: bloque de captura
 */
static void analizar_intentar(
    Sem *s,
    const AstNodo *n
)
{
    if (!s || !n) {
        return;
    }

    const AstNodo *protegido = hijo(n, 0);
    const AstNodo *capturas = hijo(n, 1);

    /*
     * Cada bloque seek tiene su
     * propio ambito.
     */
    if (!protegido ||
        protegido->tipo != AST_BLOQUE) {

        error(
            s,
            n,
            "seek requiere un bloque valido"
        );

    } else {

        bloque(
            s,
            protegido,
            1
        );
    }

    /*
     * La gramatica exige al menos
     * una captura seize.
     */
    if (!capturas ||
        capturas->tipo != AST_CAPTURAS ||
        !capturas->primer_hijo) {

        error(
            s,
            n,
            "seek requiere al menos un seize"
        );

        return;
    }

    for (
        const AstNodo *captura =
            capturas->primer_hijo;
        captura;
        captura = captura->siguiente
    ) {

        if (captura->tipo != AST_CAPTURA) {

            error(
                s,
                captura,
                "nodo de captura invalido"
            );

            continue;
        }

        const AstNodo *tipo_excepcion = hijo(
            captura,
            0
        );

        const AstNodo *variable = hijo(
            captura,
            1
        );

        const AstNodo *contenido = hijo(
            captura,
            2
        );

        /*
         * Comprobar estructura del AST.
         */
        if (!tipo_excepcion ||
            tipo_excepcion->tipo != AST_IDENTIFICADOR ||
            !tipo_excepcion->texto ||
            !variable ||
            variable->tipo != AST_IDENTIFICADOR ||
            !variable->texto ||
            !contenido ||
            contenido->tipo != AST_BLOQUE) {

            error(
                s,
                captura,
                "estructura seize invalida"
            );

            continue;
        }

        /*
         * Comprobar capturas duplicadas.
         *
         * seize(Error e) {...}
         * seize(Error otro) {...}
         *
         * Es un duplicado del mismo tipo.
         */
        for (
            const AstNodo *anterior =
                capturas->primer_hijo;
            anterior != captura;
            anterior = anterior->siguiente
        ) {

            if (anterior->tipo != AST_CAPTURA) {
                continue;
            }

            const AstNodo *tipo_anterior = hijo(
                anterior,
                0
            );

            if (tipo_anterior &&
                tipo_anterior->texto &&
                strcmp(
                    tipo_anterior->texto,
                    tipo_excepcion->texto
                ) == 0) {

                error(
                    s,
                    tipo_excepcion,
                    "tipo de excepcion '%s' "
                    "capturado mas de una vez",
                    tipo_excepcion->texto
                );

                break;
            }
        }

        /*
         * Crear un ambito independiente
         * para cada captura.
         */
        SimResultado resultado = sim_entrar_ambito(
            s->tabla,
            "seize"
        );

        if (resultado != SIM_OK) {

            error(
                s,
                captura,
                "no se pudo crear el "
                "ambito de seize"
            );

            if (resultado == SIM_SIN_MEMORIA) {
                s->fatal = 1;
            }

            continue;
        }

        /*
         * Registrar identificador capturado.
         *
         * Todavia no existe un tipo dedicado
         * para excepciones en SimTipo.
         *
         * SIM_TIPO_ERROR se utiliza como
         * marcador provisional de tipo
         * desconocido, sin reportar error
         * por el mero registro.
         */
        SimSimbolo *simbolo = NULL;

        (void)registrar(
            s,
            variable,
            SIM_VARIABLE,
            SIM_TIPO_ERROR,
            &simbolo
        );

        /*
         * El ambito seize ya esta creado.
         *
         * Por eso bloque(..., 0) evita
         * crear un segundo ambito.
         */
        bloque(
            s,
            contenido,
            0
        );

        /*
         * Salir del ambito de captura.
         */
        (void)sim_salir_ambito(
            s->tabla
        );
    }
}

/*
 * Analiza un ciclo cycle.
 *
 * AST_RECORRIDO:
 *
 * hijo 0: variable de control
 * hijo 1: inicio
 * hijo 2: limite
 * hijo 3: paso
 * hijo 4: cuerpo
 */
static void analizar_recorrido(
    Sem *s,
    const AstNodo *n
)
{
    const AstNodo *variable = hijo(n, 0);
    const AstNodo *inicio = hijo(n, 1);
    const AstNodo *limite = hijo(n, 2);
    const AstNodo *paso = hijo(n, 3);
    const AstNodo *cuerpo = hijo(n, 4);

    /*
     * Validar expresiones en el ámbito
     * exterior al ciclo.
     */
    SimTipo tipo_inicio = expresion(
        s,
        inicio
    );

    SimTipo tipo_limite = expresion(
        s,
        limite
    );

    SimTipo tipo_paso = expresion(
        s,
        paso
    );

    if (tipo_inicio != SIM_TIPO_INT &&
        tipo_inicio != SIM_TIPO_ERROR) {

        error(
            s,
            inicio,
            "el valor inicial de cycle "
            "debe ser declare_int"
        );
    }

    if (tipo_limite != SIM_TIPO_INT &&
        tipo_limite != SIM_TIPO_ERROR) {

        error(
            s,
            limite,
            "el limite de cycle "
            "debe ser declare_int"
        );
    }

    if (tipo_paso != SIM_TIPO_INT &&
        tipo_paso != SIM_TIPO_ERROR) {

        error(
            s,
            paso,
            "el paso de cycle "
            "debe ser declare_int"
        );
    }

    /*
     * Evitar un ciclo con paso constante cero.
     */
    size_t paso_constante = 0;

    if (paso &&
        paso->tipo == AST_ENTERO &&
        entero_natural(paso, &paso_constante) &&
        paso_constante == 0) {

        error(
            s,
            paso,
            "el paso de cycle no puede ser cero"
        );
    }

    /*
     * Crear un ámbito propio para el ciclo.
     */
    char nombre_ambito[32];

    snprintf(
        nombre_ambito,
        sizeof(nombre_ambito),
        "cycle_%u",
        ++s->bloques
    );

    SimResultado r = sim_entrar_ambito(
        s->tabla,
        nombre_ambito
    );

    if (r != SIM_OK) {

        error(
            s,
            n,
            "no se pudo crear el ambito de cycle"
        );

        if (r == SIM_SIN_MEMORIA) {
            s->fatal = 1;
        }

        return;
    }

    /*
     * Registrar la variable de control.
     *
     * Por ejemplo:
     *
     * cycle i let 0 until 10
     *
     * registra i como declare_int.
     */
    (void)registrar(
        s,
        variable,
        SIM_VARIABLE,
        SIM_TIPO_INT,
        NULL
    );

    /*
     * El cuerpo comparte el ámbito
     * de la variable de control.
     */
    bloque(
        s,
        cuerpo,
        0
    );

    /*
     * Salir del ámbito del ciclo.
     */
    (void)sim_salir_ambito(
        s->tabla
    );
}


//Analiza una sentencia del programa
 
static void sentencia(
    Sem *s,
    const AstNodo *n
) {
    if (!n) {
        return;
    }

    switch (n->tipo) {

        //Bloques anidados
         
        case AST_BLOQUE:

            bloque(s, n, 1);
            break;

        //Grupos de declaraciones
         
        case AST_DECLARACIONES:

            for (
                const AstNodo *p = n->primer_hijo;
                p;
                p = p->siguiente
            ) {
                sentencia(s, p);
            }

            break;

        //Variables, constantes y listas
         
        case AST_DECLARACION:
        case AST_CONSTANTE:
        case AST_LISTA:

            declarar(s, n);
            break;

        //Asignaciones
         
        case AST_ASIGNACION: {

            const AstNodo *dest = hijo(n, 0);
            const AstNodo *valor = hijo(n, 1);

            SimTipo d = expresion(s, dest);
            SimTipo v = expresion(s, valor);

            //Comprueba que el destino sea asignable
            if (dest &&
                dest->tipo != AST_IDENTIFICADOR &&
                dest->tipo != AST_INDICE) {

                error(
                    s,
                    dest,
                    "destino de asignacion invalido"
                );
            }

            //No permite una asignación escalar a un arreglo completo
            if (dest &&
                dest->tipo == AST_IDENTIFICADOR &&
                dest->texto) {

                SimSimbolo *simbolo = sim_buscar(
                    s->tabla,
                    dest->texto
                );

                if (simbolo &&
                    (simbolo->dimensiones > 0 ||
                    simbolo->clase == SIM_LISTA)) {

                    error(
                        s,
                        dest,
                        "no se puede asignar un valor "
                        "directamente a la coleccion "
                        "completa '%s'",
                        dest->texto
                    );

                    d = SIM_TIPO_ERROR;
                }
            }

            //Prohibe modificación de constantes incluso mediante un acceso por índice
            const AstNodo *base_destino = dest;

            if (dest && dest->tipo == AST_INDICE) {
                base_destino = base_indice(dest);
            }

            if (base_destino &&
                base_destino->tipo == AST_IDENTIFICADOR &&
                base_destino->texto) {

                SimSimbolo *sym = sim_buscar(
                    s->tabla,
                    base_destino->texto
                );

                if (sym && sym->clase == SIM_CONSTANTE) {

                    error(
                        s,
                        dest,
                        "no se puede modificar "
                        "la constante '%s'",
                        base_destino->texto
                    );
                }
            }

            //Verifica tipos
             
            if (d != SIM_TIPO_ERROR &&
                v != SIM_TIPO_ERROR &&
                d != v) {

                error(
                    s,
                    n,
                    "asignacion incompatible (%s y %s)",
                    sim_nombre_tipo(d),
                    sim_nombre_tipo(v)
                );
            }

            break;
        }

        //Condicional whether / alif / also
         
        case AST_SI: {

            SimTipo t = expresion(
                s,
                hijo(n, 0)
            );

            if (t != SIM_TIPO_BOOLEAN &&
                t != SIM_TIPO_ERROR) {

                error(
                    s,
                    hijo(n, 0),
                    "condicion de whether/alif "
                    "debe ser booleana"
                );
            }

            //Bloque principal
             
            bloque(
                s,
                hijo(n, 1),
                1
            );

            //Alternativa opcional
             
            const AstNodo *alt = hijo(n, 2);

            if (alt) {

                if (alt->tipo == AST_SI) {
                    sentencia(s, alt);
                } else {
                    bloque(s, alt, 1);
                }
            }

            break;
        }

        //Ciclo whale
         
        case AST_MIENTRAS: {

            SimTipo t = expresion(
                s,
                hijo(n, 0)
            );

            if (t != SIM_TIPO_BOOLEAN &&
                t != SIM_TIPO_ERROR) {

                error(
                    s,
                    hijo(n, 0),
                    "condicion de whale "
                    "debe ser booleana"
                );
            }

            bloque(
                s,
                hijo(n, 1),
                1
            );

            break;
        }

        /*
        * Ciclo cycle
        */
        case AST_RECORRIDO:

            analizar_recorrido(
                s,
                n
            );

            break;

        case AST_AGREGAR: {

            const AstNodo *destino = hijo(n, 0);
            const AstNodo *valor = hijo(n, 1);

            SimSimbolo *lista = buscar_coleccion(
                s,
                destino,
                1
            );

            SimTipo recibido = expresion(
                s,
                valor
            );

            if (lista &&
                recibido != SIM_TIPO_ERROR &&
                lista->tipo != SIM_TIPO_ERROR &&
                recibido != lista->tipo) {

                error(
                    s,
                    valor,
                    "add: la lista '%s' requiere "
                    "elementos de tipo %s, "
                    "pero se obtuvo %s",
                    lista->nombre,
                    sim_nombre_tipo(lista->tipo),
                    sim_nombre_tipo(recibido)
                );
            }

            break;
        }

        case AST_ELIMINAR: {

            const AstNodo *destino = hijo(n, 0);
            const AstNodo *indice = hijo(n, 1);

            (void)buscar_coleccion(
                s,
                destino,
                1
            );

            SimTipo tipo_indice = expresion(
                s,
                indice
            );

            if (tipo_indice != SIM_TIPO_INT &&
                tipo_indice != SIM_TIPO_ERROR) {

                error(
                    s,
                    indice,
                    "remove: el indice debe "
                    "ser de tipo declare_int"
                );
            }

            break;
        }

        case AST_INTENTAR: {

            analizar_intentar(
                s,
                n
            );

            break;
        }

        //Retorno
         
        /* Validaremos el tipo esperado
         * en la siguiente ampliación*/
        case AST_RETORNO: {

            const AstNodo *valor = hijo(n, 0);

            /*
            * Comprobar que se está
            * dentro de una función
            */
            if (!s->dentro_funcion) {

                error(
                    s,
                    n,
                    "give fuera de una funcion"
                );

                break;
            }

            //Función void: No puede retornar un valor
            if (s->retorno_actual == SIM_TIPO_VOID) {

                if (valor) {

                    /*
                    * Analiza la expresión
                    * para detectar otros errores
                    */
                    (void)expresion(s, valor);

                    error(
                        s,
                        n,
                        "una funcion void no puede "
                        "retornar un valor"
                    );
                }

                break;
            }

            /*
            * Una función no void debe
            * retornar una expresión.
            */
            if (!valor) {

                error(
                    s,
                    n,
                    "la funcion debe retornar "
                    "un valor de tipo %s",
                    sim_nombre_tipo(s->retorno_actual)
                );

                break;
            }

            /*
            * Determina el tipo del valor
            * que se está retornando
            */
            SimTipo recibido = expresion(
                s,
                valor
            );

            /*
            * Verifica compatibilidad de tipos
            */
            if (s->retorno_actual != SIM_TIPO_ERROR &&
                recibido != SIM_TIPO_ERROR &&
                recibido != s->retorno_actual) {

                error(
                    s,
                    valor,
                    "retorno incompatible: "
                    "se esperaba %s, se obtuvo %s",
                    sim_nombre_tipo(s->retorno_actual),
                    sim_nombre_tipo(recibido)
                );
            }

            break;
        }
        //Llamadas como sentencias
         
        case AST_LLAMADA:

            (void)expresion(s, n);
            break;

        //Construcciones pendientes
         
        default:

            error(
                s,
                n,
                "sentencia AST %s pendiente "
                "de analisis",
                ast_nombre_tipo(n->tipo)
            );

            break;
    }
}

/*
 * Analisis de rutas de retorno.
 *
 * Determina si una sentencia garantiza
 * la ejecucion de un give.
 *
 * No modifica el AST ni la tabla
 * de simbolos.
 */

static int garantiza_retorno(
    const AstNodo *n
);

/*
 * Un bloque garantiza retorno cuando
 * alguna sentencia de su secuencia
 * garantiza retornar.
 *
 * Ejemplo:
 *
 * {
 *     n * declare_int : 10;
 *     give n;
 * }
 *
 * La segunda sentencia garantiza
 * el retorno del bloque.
 */
static int bloque_garantiza_retorno(
    const AstNodo *bloque_n
)
{
    if (!bloque_n ||
        bloque_n->tipo != AST_BLOQUE) {
        return 0;
    }

    for (
        const AstNodo *actual =
            bloque_n->primer_hijo;
        actual;
        actual = actual->siguiente
    ) {
        if (garantiza_retorno(actual)) {
            return 1;
        }
    }

    return 0;
}

/*
 * Verificar una estructura condicional.
 *
 * AST_SI:
 *
 * hijo 0: condicion
 * hijo 1: bloque verdadero
 * hijo 2: rama alternativa (opcional)
 *
 * La alternativa puede ser:
 *
 * - AST_BLOQUE -> also
 * - AST_SI     -> alif
 *
 * Para garantizar retorno, tanto
 * la rama verdadera como la alternativa
 * deben garantizarlo.
 */
static int si_garantiza_retorno(
    const AstNodo *n
)
{
    if (!n || n->tipo != AST_SI) {
        return 0;
    }

    const AstNodo *verdadero =
        hijo(n, 1);

    const AstNodo *alternativa =
        hijo(n, 2);

    /*
     * Sin alternativa, la condicion
     * puede ser falsa y continuar
     * despues del whether.
     */
    if (!alternativa) {
        return 0;
    }

    /*
     * La rama verdadera debe retornar.
     */
    if (!garantiza_retorno(verdadero)) {
        return 0;
    }

    /*
     * La alternativa tambien debe
     * garantizar un retorno.
     */
    return garantiza_retorno(alternativa);
}

/*
 * Comprueba si una sentencia garantiza
 * que la funcion termine mediante give.
 */
static int garantiza_retorno(
    const AstNodo *n
)
{
    if (!n) {
        return 0;
    }

    switch (n->tipo) {

        /*
         * Un give retorna directamente.
         */
        case AST_RETORNO:
            return 1;

        /*
         * Un bloque se analiza en orden.
         */
        case AST_BLOQUE:
            return bloque_garantiza_retorno(n);

        /*
         * whether / alif / also.
         */
        case AST_SI:
            return si_garantiza_retorno(n);

        /*
         * No consideramos que whale o cycle
         * garanticen retornar porque sus
         * cuerpos pueden no ejecutarse.
         *
         * Las demas instrucciones tampoco
         * garantizan un retorno.
         */
        default:
            return 0;
    }
}

/* Registro de parametros escalares y compuestos
 *
 * Soporta:
 *
 * a * declare_int
 * a * declare_int[]
 * a * declare_int[][]
 * a * declare_list declare_int
 *
 * Para arreglos:
 * - Registra la cantidad de dimensiones.
 * - Guarda longitud 0 cuando no se conoce.
 *
 * Para listas:
 * - Registra SIM_LISTA como clase del simbolo.
 *
 * Los parametros no se tratan como
 * declaraciones de variables locales.
 */
static void registrar_parametro(
    Sem *s,
    const AstNodo *parametro
)
{
    if (!s || !parametro) {
        return;
    }

    const AstNodo *id = hijo(
        parametro,
        0
    );

    const AstNodo *tipo_n = hijo(
        parametro,
        1
    );

    SimTipo tipo = tipo_declarado(
        tipo_n
    );

    /*
     * Los parametros escalares y arreglos
     * usan SIM_PARAMETRO.
     *
     * Las listas usan SIM_LISTA para que
     * add, remove, size e indices funcionen.
     *
     * Esta decision es provisional hasta
     * separar formalmente clase y forma.
     */
    SimClase clase = (
        tipo_n &&
        tipo_n->tipo == AST_TIPO_LISTA
    )
        ? SIM_LISTA
        : SIM_PARAMETRO;

    if (tipo == SIM_TIPO_ERROR ||
        tipo == SIM_TIPO_VOID) {

        error(
            s,
            tipo_n,
            "tipo de parametro invalido"
        );

        return;
    }

    SimSimbolo *simbolo = NULL;

    int agregado = registrar(
        s,
        id,
        clase,
        tipo,
        &simbolo
    );

    if (!agregado || !simbolo) {
        return;
    }
    /*
    * Conservar el origen del simbolo.
    *
    * Un parametro de lista mantiene SIM_LISTA
    * para las operaciones sobre colecciones,
    * pero es_parametro permite distinguirlo
    * de una lista local.
    */
    simbolo->es_parametro = 1;

    /*
     * Parametro escalar o lista:
     * no requiere dimensiones.
     */
    if (!tipo_n ||
        tipo_n->tipo != AST_TIPO_ARREGLO) {
        return;
    }

    /*
     * Parametro de arreglo o matriz.
     */
    const AstNodo *dimensiones = hijo(
        tipo_n,
        1
    );

    if (!dimensiones ||
        dimensiones->tipo != AST_DIMENSIONES) {

        error(
            s,
            tipo_n,
            "parametro de arreglo sin "
            "informacion de dimensiones"
        );

        return;
    }

    unsigned cantidad = 0;

    for (
        const AstNodo *d =
            dimensiones->primer_hijo;
        d;
        d = d->siguiente
    ) {

        if (cantidad == UINT_MAX) {

            error(
                s,
                d,
                "demasiadas dimensiones "
                "en parametro"
            );

            return;
        }

        ++cantidad;
    }

    if (cantidad == 0) {

        error(
            s,
            tipo_n,
            "el parametro de arreglo "
            "necesita dimensiones"
        );

        return;
    }

    /*
     * Cero representa longitud
     * no especificada.
     *
     * Ejemplo:
     * declare_int[][]
     */
    size_t *longitudes = calloc(
        cantidad,
        sizeof(*longitudes)
    );

    if (!longitudes) {

        error(
            s,
            tipo_n,
            "memoria insuficiente para "
            "dimensiones de parametro"
        );

        s->fatal = 1;
        return;
    }

    /*
    * Validar las dimensiones declaradas
    * en parametros de arreglos y matrices.
    *
    * Una dimension vacia [] es valida
    * cuando se declara un parametro.
    *
    * Las dimensiones con expresiones deben
    * tener tipo declare_int.
    */
    unsigned posicion = 0;

    for (
        const AstNodo *dimension =
            dimensiones->primer_hijo;
        dimension;
        dimension = dimension->siguiente
    ) {

        const AstNodo *tamano = hijo(
            dimension,
            0
        );

        /*
        * Parametro con dimension abierta:
        *
        * datos * declare_int[]
        *
        * No existe una longitud estatica.
        */
        if (!tamano) {
            longitudes[posicion] = 0;
            ++posicion;
            continue;
        }

        /*
        * Dimension estatica:
        *
        * datos * declare_int[5]
        */
        if (tamano->tipo == AST_ENTERO) {

            size_t valor = 0;

            if (!entero_natural(
                    tamano,
                    &valor
                ) || valor == 0) {

                error(
                    s,
                    tamano,
                    "la dimension %u del parametro "
                    "debe ser mayor que cero",
                    posicion + 1
                );

            } else {

                longitudes[posicion] = valor;
            }

            ++posicion;
            continue;
        }

        /*
        * Evitar una dimension negativa
        * expresada como neumann N.
        */
        if (
            tamano->tipo == AST_UNARIO &&
            tamano->texto &&
            strcmp(
                tamano->texto,
                "neumann"
            ) == 0 &&
            hijo(tamano, 0) &&
            hijo(tamano, 0)->tipo == AST_ENTERO
        ) {

            error(
                s,
                tamano,
                "la dimension %u del parametro "
                "debe ser mayor que cero",
                posicion + 1
            );

            ++posicion;
            continue;
        }

        /*
        * Dimension expresada mediante
        * un identificador u operacion.
        */
        SimTipo tipo_dimension = expresion(
            s,
            tamano
        );

        if (
            tipo_dimension != SIM_TIPO_INT &&
            tipo_dimension != SIM_TIPO_ERROR
        ) {

            error(
                s,
                tamano,
                "la dimension %u del parametro "
                "debe ser declare_int",
                posicion + 1
            );
        }

        /*
        * El valor se conoce en tiempo
        * de ejecucion, no en compilacion.
        */
        longitudes[posicion] = 0;

        ++posicion;
    }

    /*
     * En esta etapa conservamos la
     * cantidad de dimensiones.
     *
     * El parametro puede contener
     * expresiones de tamano que requieren
     * reglas adicionales de alcance.
     *
     * No las evaluamos como si fueran
     * dimensiones locales ordinarias.
     */
    SimResultado resultado =
        sim_definir_dimensiones(
            simbolo,
            longitudes,
            cantidad
        );

    if (resultado != SIM_OK) {

        error(
            s,
            tipo_n,
            "no se pudieron registrar "
            "las dimensiones del parametro"
        );

        if (resultado == SIM_SIN_MEMORIA) {
            s->fatal = 1;
        }
    }

    free(longitudes);
}

/* Registro semantico de importaciones
 *
 * Sintaxis:
 *
 * bring modulo;
 * bring modulo aka alias;
 *
 * AST_IMPORTACION:
 *
 * hijo 0: identificador del modulo
 * hijo 1: identificador del alias (opcional)
 *
 * El nombre visible se registra con SIM_MODULO.
 *
 * Esta etapa comprueba nombres y duplicados.
 * La carga real del modulo queda pendiente.
 */
static void registrar_importacion(
    Sem *s,
    const AstNodo *n
)
{
    if (!s || !n) {
        return;
    }

    const AstNodo *modulo = hijo(n, 0);
    const AstNodo *alias = hijo(n, 1);

    if (!modulo ||
        modulo->tipo != AST_IDENTIFICADOR ||
        !modulo->texto) {

        error(
            s,
            n,
            "importacion sin nombre de modulo"
        );

        return;
    }

    /*
     * Sin alias:
     *
     * bring matematicas;
     *
     * Nombre visible: matematicas
     *
     * Con alias:
     *
     * bring matematicas aka math;
     *
     * Nombre visible: math
     */
    const AstNodo *visible = modulo;

    if (alias) {

        if (alias->tipo != AST_IDENTIFICADOR ||
            !alias->texto) {

            error(
                s,
                alias,
                "alias de importacion invalido"
            );

            return;
        }

        visible = alias;
    }

    /*
     * Registrar en el ambito global.
     *
     * Los modulos no son valores escalares;
     * SIM_TIPO_ERROR indica que su tipo
     * no esta resuelto en esta etapa.
     */
    SimSimbolo *simbolo = NULL;

    (void)registrar(
        s,
        visible,
        SIM_MODULO,
        SIM_TIPO_ERROR,
        &simbolo
    );
}

/* Accesos a miembros de modulos
 *
 * Valida que el nombre base corresponda
 * a un modulo importado.
 *
 * No intenta inventar los miembros de un
 * modulo cuya interfaz aun no esta cargada.
 */
static SimTipo analizar_acceso_miembro(Sem *s, const AstNodo *n)
{
    const SimSimbolo *simbolo = resolver_miembro(s, n, NULL);
    if (!simbolo) return SIM_TIPO_ERROR;
    if (simbolo->clase == SIM_FUNCION || simbolo->clase == SIM_MODULO) {
        error(s, n, "un miembro funcion o modulo no es un valor escalar");
        return SIM_TIPO_ERROR;
    }
    if (simbolo->clase != SIM_CONSTANTE || simbolo->dimensiones != 0) {
        error(s, n, "solo se admite el acceso escalar a constantes importadas");
        return SIM_TIPO_ERROR;
    }
    return simbolo->tipo;
}


//Función principal del análisis semántico.
//Recibe el AST construido por y registra los símbolos del programa
 
int analizar_semantica_con_modulos(
    const Ast *arbol,
    SimTabla *tabla,
    const char *archivo,
    FILE *diagnosticos,
    SemModo modo,
    SemResolverModulo resolver,
    void *contexto
) {
    //Verifica argumentos y estado inicial
     
    if (!arbol ||
        !arbol->raiz ||
        arbol->raiz->tipo != AST_PROGRAMA ||
        !tabla ||
        !tabla->global ||
        tabla->actual != tabla->global) {

        return 2;
    }

    Sem s = {0};

    s.tabla = tabla;
    s.resolver = resolver;
    s.contexto_modulos = contexto;
    s.archivo = archivo;
    s.diagnosticos = diagnosticos;

    // Conservar el AST para consultar las firmas de las funciones
    s.arbol = arbol;
    s.retorno_actual = SIM_TIPO_ERROR;
    s.dentro_funcion = 0;

    //Contador de funciones main
    size_t cantidad_main = 0;

    if (modo != SEM_PROGRAMA_PRINCIPAL &&
        modo != SEM_MODULO_IMPORTADO) {
        return 2;
    }

    //PRIMERA PASADA:
    //Registra funciones globales antes de analizar sus cuerpos
    //Así se admiten llamadas a funciones declaradas posteriormente
    for (
        const AstNodo *n =
            arbol->raiz->primer_hijo;
        n;
        n = n->siguiente
    ) {
        if (n->tipo == AST_FUNCION) {

            const AstNodo *nombre = hijo(n, 0);

            /*
            * Verificar las declaraciones de main.
            */
            if (nombre &&
                nombre->texto &&
                strcmp(nombre->texto, "main") == 0) {

                ++cantidad_main;

                /*
                * Los modulos importados no pueden
                * declarar el punto de entrada main.
                */
                if (modo == SEM_MODULO_IMPORTADO) {

                    error(
                        &s,
                        nombre,
                        "un modulo importado no puede "
                        "declarar la funcion main"
                    );
                }

                /*
                * main debe retornar void.
                */
                SimTipo retorno = tipo_declarado(
                    hijo(n, 1)
                );

                if (retorno != SIM_TIPO_VOID) {

                    error(
                        &s,
                        nombre,
                        "main debe retornar "
                        "declare_infinite_void"
                    );
                }

                /*
                * main no puede recibir parametros.
                */
                const AstNodo *params_main = hijo(
                    n,
                    2
                );

                if (contar_hijos(params_main) != 0) {

                    error(
                        &s,
                        nombre,
                        "main no debe recibir parametros"
                    );
                }
            }

            /*
            * Registrar la funcion en el
            * ambito global.
            */
            SimSimbolo *simbolo = NULL;

            if (
                registrar(
                    &s,
                    nombre,
                    SIM_FUNCION,
                    tipo_declarado(hijo(n, 1)),
                    &simbolo
                ) &&
                simbolo
            ) {

                const AstNodo *params = hijo(
                    n,
                    2
                );

                /*
                * Guardar la cantidad de parametros.
                */
                for (
                    const AstNodo *p =
                        params ? params->primer_hijo : NULL;
                    p;
                    p = p->siguiente
                ) {
                    ++simbolo->cantidad_parametros;
                }
            }

        } else if (n->tipo == AST_IMPORTACION) {

            /*
            * ETAPA 2J
            *
            * Registrar importaciones y alias.
            *
            * Ejemplos:
            *
            * bring matematicas;
            * bring matematicas aka math;
            */
            registrar_importacion(
                &s,
                n
            );

        } else if (n->tipo == AST_CONSTANTE) {

            /*
            * Registrar constantes globales.
            */
            declarar(
                &s,
                n
            );
        }
    }

    /*
    * Solamente el programa principal
    * necesita exactamente una funcion main.
    *
    * Los modulos no necesitan main.
    * Si la declaran, el error ya se
    * informa durante la primera pasada.
    */
    if (modo == SEM_PROGRAMA_PRINCIPAL) {

        if (cantidad_main == 0) {

            error(
                &s,
                arbol->raiz,
                "no se encontro la funcion main"
            );

        } else if (cantidad_main > 1) {

            error(
                &s,
                arbol->raiz,
                "el programa tiene mas de una "
                "funcion main"
            );
        }
    }

    //SEGUNDA PASADA: Analizar parámetros y cuerpos de cada función
     
    for (
        const AstNodo *n =
            arbol->raiz->primer_hijo;
        n;
        n = n->siguiente
    ) {
        if (n->tipo != AST_FUNCION) {
            continue;
        }

        const AstNodo *nombre = hijo(n, 0);

        //Crea ambito para la función
         
        SimResultado r = sim_entrar_ambito(
            s.tabla,
            nombre && nombre->texto
                ? nombre->texto
                : "funcion"
        );

        if (r != SIM_OK) {

            error(
                &s,
                n,
                "no se pudo crear ambito de funcion"
            );

            if (r == SIM_SIN_MEMORIA) {
                s.fatal = 1;
            }

            continue;
        }

        //Registra parámetros
         
        /* Registrar parametros de la funcion.
        * Admite:
        * - Escalares
        * - Arreglos
        * - Matrices
        * - Listas
        */
        const AstNodo *params = hijo(n, 2);

        for (
            const AstNodo *p =
                params ? params->primer_hijo : NULL;
            p;
            p = p->siguiente
        ) {
            registrar_parametro(
                &s,
                p
            );
        }

        /*
        * Obtener la declaracion de retorno
        * y el cuerpo de la funcion actual.
        */
        const AstNodo *tipo_retorno = hijo(
            n,
            1
        );

        const AstNodo *cuerpo = hijo(
            n,
            3
        );

        s.retorno_actual = tipo_declarado(
            tipo_retorno
        );

        s.dentro_funcion = 1;

        /*
        * Primero ejecutamos las validaciones
        * semanticas que ya implementamos:
        *
        * - Variables
        * - Ambitos
        * - Tipos
        * - Llamadas
        * - Arreglos
        * - Retornos individuales
        */
        bloque(
            &s,
            cuerpo,
            0
        );

        /*
        * Despues comprobamos que una funcion
        * con retorno no void garantice give.
        *
        * No exigimos un valor a las funciones
        * declare_infinite_void.
        */
        if (s.retorno_actual != SIM_TIPO_VOID &&
            s.retorno_actual != SIM_TIPO_ERROR) {

            if (!garantiza_retorno(cuerpo)) {

                const AstNodo *nombre_funcion =
                    hijo(n, 0);

                error(
                    &s,
                    nombre_funcion,
                    "la funcion '%s' debe "
                    "retornar un valor de tipo %s "
                    "en todos los caminos",
                    nombre_funcion &&
                    nombre_funcion->texto
                        ? nombre_funcion->texto
                        : "(sin nombre)",
                    sim_nombre_tipo(
                        s.retorno_actual
                    )
                );
            }
        }

        /*
        * Limpiar la informacion de la
        * funcion actual.
        */
        s.dentro_funcion = 0;

        s.retorno_actual = SIM_TIPO_ERROR;

        //Regresa al ambito global
         
        (void)sim_salir_ambito(s.tabla);
    }

    //Devuelve resultado final
     
    if (s.fatal) {
        return 2;
    }

    return s.errores ? 1 : 0;
}

int analizar_semantica_modo(const Ast *arbol, SimTabla *tabla,
                            const char *archivo, FILE *diagnosticos,
                            SemModo modo)
{
    return analizar_semantica_con_modulos(arbol, tabla, archivo,
                                         diagnosticos, modo, NULL, NULL);
}

/*
 * API original.
 *
 * Conserva el comportamiento anterior:
 * analizar_semantica() siempre analiza
 * un programa principal.
 */
int analizar_semantica(
    const Ast *arbol,
    SimTabla *tabla,
    const char *archivo,
    FILE *diagnosticos
)
{
    return analizar_semantica_modo(
        arbol,
        tabla,
        archivo,
        diagnosticos,
        SEM_PROGRAMA_PRINCIPAL
    );
}