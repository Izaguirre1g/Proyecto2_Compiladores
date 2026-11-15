/*
 * Eliminación de código muerto (DCE) con análisis de variables vivas.
 * La explicación completa está en include/dce.h.
 *
 * El archivo tiene cinco partes:
 *   1. Qué usa, qué define y qué efectos tiene cada instrucción de la IR.
 *   2. La lista de variables del programa (para numerarlas).
 *   3. El análisis de variables vivas: use, def, IN y OUT (diapositiva 25).
 *   4. Tipo 1: recorrer cada bloque hacia atrás quitando las asignaciones
 *      a variables que no están vivas (diapositiva 28).
 *   5. Tipo 2: código inalcanzable (constantes y grafo de flujo).
 */
#include "dce.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* 1. Usos, definiciones y efectos de cada instrucción                */
/* ================================================================== */

/*
 * ¿Es este operando una VARIABLE? Los operandos de la IR son cadenas, y
 * pueden ser:
 *   - variables del programa (x, total) o temporales ($t3): SÍ;
 *   - números (5, -2), textos ("hola"), caracteres ('a') y los valores
 *     declare_true / declare_false: NO, son constantes.
 */
static int es_variable(const char *s)
{
    if (s == NULL || s[0] == '\0') return 0;
    if (strcmp(s, "declare_true") == 0 || strcmp(s, "declare_false") == 0) return 0;
    return s[0] == '$' || s[0] == '_' || (s[0] >= 'a' && s[0] <= 'z') ||
           (s[0] >= 'A' && s[0] <= 'Z');
}

/* ¿Es "s" un número entero escrito tal cual (con signo opcional)?
 * Si lo es, deja su valor en "valor". */
static int es_numero(const char *s, long long *valor)
{
    if (s == NULL || s[0] == '\0') return 0;
    char *fin = NULL;
    long long v = strtoll(s, &fin, 10);
    if (fin == s || *fin != '\0') return 0;
    *valor = v;
    return 1;
}

/*
 * La variable que la instrucción ASIGNA (su "def"), o NULL si no asigna
 * ninguna. En la IR el destino está en "dst":
 *     x = a          (copia)          $t1 = a gauss b   (binaria)
 *     $t2 = ~ a      (unaria)         $t3 = ref v[i]    (referencia)
 *     $t4 = load $t3 (lectura)        $t5 = size xs     (tamaño)
 *     $t6 = call f, 2 (llamada)       param_decl n      (parámetro)
 * En store, add y remove el "dst" NO se asigna: es el lugar de memoria donde
 * se escribe, así que cuenta como un USO (ver usos()).
 */
static const char *definicion(const IrInstr *x)
{
    switch (x->op) {
        case IR_COPY: case IR_BIN: case IR_UNARY: case IR_INDEX_REF:
        case IR_LOAD: case IR_SIZE: case IR_CALL: case IR_PARAM_DECL:
            return x->dst;
        default:
            return NULL;
    }
}

/*
 * Las variables que la instrucción LEE (su "use"). Deja hasta 3 en "u" y
 * devuelve cuántas. Las constantes no cuentan.
 * Ojo: en "call f, 2" ni "f" (el nombre de la función) ni "2" (la cantidad
 * de argumentos) son variables; los argumentos ya se leyeron en los "arg".
 */
static int usos(const IrInstr *x, const char *u[3])
{
    const char *posibles[3] = {NULL, NULL, NULL};
    switch (x->op) {
        case IR_COPY: case IR_UNARY: case IR_LOAD: case IR_SIZE:
        case IR_JFALSE: case IR_JTRUE: case IR_ARG: case IR_RETURN:
            posibles[0] = x->a;
            break;
        case IR_BIN: case IR_INDEX_REF:
            posibles[0] = x->a;
            posibles[1] = x->b;
            break;
        case IR_STORE: case IR_LIST_ADD: case IR_LIST_REMOVE:
            posibles[0] = x->dst;  /* Dónde se escribe. */
            posibles[1] = x->a;    /* Qué se escribe. */
            break;
        case IR_ARRAY_DIM:
            posibles[0] = x->dst;  /* El arreglo. */
            posibles[1] = x->b;    /* El tamaño de la dimensión. */
            break;
        default:
            break;
    }
    int cuantos = 0;
    for (int i = 0; i < 3; ++i) {
        if (es_variable(posibles[i])) u[cuantos++] = posibles[i];
    }
    return cuantos;
}

/*
 * ¿Puede esta operación binaria FALLAR al ejecutarse? Entonces quitarla
 * cambiaría el comportamiento (el programa dejaría de dar el error), y la
 * regla de oro (diapositiva 12) no lo permite.
 *   - euclides y euler dividen: fallan si el divisor es 0. Solo es seguro
 *     si el divisor es un número escrito distinto de 0.
 *   - descartes es la potencia: el backend no acepta exponentes negativos.
 *     Solo es seguro si el exponente es un número escrito >= 0.
 */
static int puede_fallar(const IrInstr *x)
{
    if (x->op != IR_BIN || x->operador == NULL) return 0;
    long long valor = 0;
    if (strcmp(x->operador, "euclides") == 0 || strcmp(x->operador, "euler") == 0) {
        return !(es_numero(x->b, &valor) && valor != 0);
    }
    if (strcmp(x->operador, "descartes") == 0) {
        return !(es_numero(x->b, &valor) && valor >= 0);
    }
    return 0;
}

/*
 * ¿Se puede eliminar esta instrucción si su resultado no se usa?
 * Solo las que CALCULAN un valor sin hacer nada más: copiar, operar,
 * calcular una referencia, leer memoria o pedir un tamaño.
 * Todo lo demás se conserva siempre (ver include/dce.h): llamadas,
 * escrituras en memoria, declaraciones, saltos, etiquetas, retornos...
 */
static int se_puede_eliminar(const IrInstr *x)
{
    switch (x->op) {
        case IR_COPY: case IR_UNARY: case IR_INDEX_REF: case IR_LOAD: case IR_SIZE:
            return 1;
        case IR_BIN:
            return !puede_fallar(x);
        default:
            return 0;  /* IR_CALL entre ellas: la función puede tener efectos. */
    }
}

/* Escribe la instrucción en "texto" con el mismo formato que ir_imprimir.
 * Solo hace falta para las que se pueden eliminar. */
static void describir(const IrInstr *x, char *texto, size_t largo)
{
    /* Las del tipo 2 pueden ser cualquier instrucción (todo lo que hay en
     * un bloque inalcanzable), así que se cubren todas. */
    switch (x->op) {
        case IR_COPY:       snprintf(texto, largo, "%s = %s", x->dst, x->a); break;
        case IR_BIN:        snprintf(texto, largo, "%s = %s %s %s", x->dst, x->a, x->operador, x->b); break;
        case IR_UNARY:      snprintf(texto, largo, "%s = %s %s", x->dst, x->operador, x->a); break;
        case IR_INDEX_REF:  snprintf(texto, largo, "%s = ref %s[%s]", x->dst, x->a, x->b); break;
        case IR_LOAD:       snprintf(texto, largo, "%s = load %s", x->dst, x->a); break;
        case IR_SIZE:       snprintf(texto, largo, "%s = size %s", x->dst, x->a); break;
        case IR_JUMP:       snprintf(texto, largo, "goto %s", x->dst); break;
        case IR_JFALSE:     snprintf(texto, largo, "ifFalse %s goto %s", x->a, x->dst); break;
        case IR_JTRUE:      snprintf(texto, largo, "ifTrue %s goto %s", x->a, x->dst); break;
        case IR_ARG:        snprintf(texto, largo, "arg %s", x->a); break;
        case IR_CALL:       snprintf(texto, largo, "%s = call %s, %s", x->dst, x->a, x->b); break;
        case IR_RETURN:     snprintf(texto, largo, x->a ? "return %s" : "return", x->a); break;
        case IR_STORE:      snprintf(texto, largo, "store %s, %s", x->dst, x->a); break;
        case IR_ARRAY_DECL: snprintf(texto, largo, "array %s, dimensiones %s", x->dst, x->a); break;
        case IR_ARRAY_DIM:  snprintf(texto, largo, "dim %s[%s] = %s", x->dst, x->a, x->b); break;
        case IR_LIST_DECL:  snprintf(texto, largo, "list %s", x->dst); break;
        case IR_LIST_ADD:   snprintf(texto, largo, "add %s, %s", x->dst, x->a); break;
        case IR_LIST_REMOVE:snprintf(texto, largo, "remove %s, %s", x->dst, x->a); break;
        default:            snprintf(texto, largo, "(instruccion %d)", (int)x->op); break;
    }
}

/* ================================================================== */
/* 2. Lista de variables                                               */
/* ================================================================== */

/* Para usar conjuntos de 0/1, cada variable recibe un número: su posición
 * en la lista ordenada alfabéticamente. */

static int comparar_nombres(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Agrega "nombre" a la lista (sin ordenar todavía). 0 bien, 2 sin memoria. */
static int agregar_nombre(char ***lista, size_t *cantidad, size_t *capacidad, const char *nombre)
{
    if (*cantidad == *capacidad) {
        size_t nueva = *capacidad ? *capacidad * 2 : 32;
        char **mas = realloc(*lista, nueva * sizeof *mas);
        if (mas == NULL) return 2;
        *lista = mas;
        *capacidad = nueva;
    }
    (*lista)[(*cantidad)++] = (char *)nombre;  /* Por ahora apunta a la IR. */
    return 0;
}

/* Arma la lista de variables de "ir": todos los usos y definiciones, sin
 * repetir, en orden alfabético. Cada nombre es una copia propia. */
static int armar_variables(const IrPrograma *ir, DceVivas *v)
{
    char **lista = NULL;
    size_t cantidad = 0, capacidad = 0;

    for (size_t i = 0; i < ir->cantidad; ++i) {
        const IrInstr *x = &ir->codigo[i];
        const char *u[3];
        int n = usos(x, u);
        for (int k = 0; k < n; ++k) {
            if (agregar_nombre(&lista, &cantidad, &capacidad, u[k]) != 0) goto sin_memoria;
        }
        const char *d = definicion(x);
        if (es_variable(d) && agregar_nombre(&lista, &cantidad, &capacidad, d) != 0) goto sin_memoria;
    }

    /* Ordenar y quitar repetidos. */
    if (cantidad > 0) qsort(lista, cantidad, sizeof *lista, comparar_nombres);
    size_t unicos = 0;
    for (size_t i = 0; i < cantidad; ++i) {
        if (unicos > 0 && strcmp(lista[unicos - 1], lista[i]) == 0) continue;
        lista[unicos++] = lista[i];
    }

    /* Copias propias: la IR puede cambiar (el DCE le quita instrucciones). */
    v->variables = calloc(unicos ? unicos : 1, sizeof *v->variables);
    if (v->variables == NULL) goto sin_memoria;
    for (size_t i = 0; i < unicos; ++i) {
        size_t largo = strlen(lista[i]) + 1;
        v->variables[i] = malloc(largo);
        if (v->variables[i] == NULL) {
            v->cantidad_variables = i;
            goto sin_memoria;
        }
        memcpy(v->variables[i], lista[i], largo);
    }
    v->cantidad_variables = unicos;
    free(lista);
    return 0;

sin_memoria:
    free(lista);
    return 2;
}

/* El número de una variable, o SIZE_MAX si no está en la lista. */
static size_t numero_de(const DceVivas *v, const char *nombre)
{
    if (nombre == NULL || v->cantidad_variables == 0) return SIZE_MAX;
    char *const *encontrado = bsearch(&nombre, v->variables, v->cantidad_variables,
                                      sizeof *v->variables, comparar_nombres);
    return encontrado ? (size_t)(encontrado - v->variables) : SIZE_MAX;
}

/* ================================================================== */
/* 3. Variables vivas (diapositiva 25)                                 */
/* ================================================================== */

void dce_liberar_vivas(DceVivas *v)
{
    if (v == NULL) return;
    for (size_t i = 0; i < v->cantidad_variables; ++i) free(v->variables[i]);
    free(v->variables);
    free(v->usa);
    free(v->define);
    free(v->entrada);
    free(v->salida);
    memset(v, 0, sizeof *v);
}

/*
 * Las variables de main que son el RESULTADO del programa: las que se leen
 * en algún lugar de main (sin los temporales $tN), en el programa tal como
 * está en esta pasada.
 */
typedef struct {
    char **nombres;   /* Ordenados alfabéticamente (para buscar con bsearch). */
    size_t cantidad;
} ResultadoMain;

static void liberar_resultado_main(ResultadoMain *m)
{
    for (size_t i = 0; i < m->cantidad; ++i) free(m->nombres[i]);
    free(m->nombres);
    memset(m, 0, sizeof *m);
}

/* Junta las variables leídas en main. 0 bien, 2 sin memoria. */
static int resultado_de_main(const IrPrograma *ir, ResultadoMain *m)
{
    memset(m, 0, sizeof *m);
    char **lista = NULL;
    size_t cantidad = 0, capacidad = 0;
    int en_main = 0;
    for (size_t i = 0; i < ir->cantidad; ++i) {
        const IrInstr *x = &ir->codigo[i];
        if (x->op == IR_FUNC) en_main = x->dst && strcmp(x->dst, "main") == 0;
        if (!en_main) continue;
        const char *u[3];
        int n = usos(x, u);
        for (int k = 0; k < n; ++k) {
            if (u[k][0] == '$') continue;  /* Temporal: no es una variable del programa. */
            if (agregar_nombre(&lista, &cantidad, &capacidad, u[k]) != 0) {
                free(lista);
                return 2;
            }
        }
    }
    if (cantidad > 0) qsort(lista, cantidad, sizeof *lista, comparar_nombres);
    m->nombres = calloc(cantidad ? cantidad : 1, sizeof *m->nombres);
    if (m->nombres == NULL) {
        free(lista);
        return 2;
    }
    for (size_t i = 0; i < cantidad; ++i) {
        if (m->cantidad > 0 && strcmp(m->nombres[m->cantidad - 1], lista[i]) == 0) continue;
        size_t largo = strlen(lista[i]) + 1;
        char *copia = malloc(largo);
        if (copia == NULL) {
            free(lista);
            liberar_resultado_main(m);
            return 2;
        }
        memcpy(copia, lista[i], largo);
        m->nombres[m->cantidad++] = copia;
    }
    free(lista);
    return 0;
}

/* ¿Es "nombre" parte del resultado de main? */
static int es_resultado(const ResultadoMain *m, const char *nombre)
{
    return m->cantidad > 0 &&
           bsearch(&nombre, m->nombres, m->cantidad, sizeof *m->nombres, comparar_nombres) != NULL;
}

static int calcular_vivas(const IrPrograma *ir, DceVivas *v, const ResultadoMain *resultado);

int dce_calcular_vivas(const IrPrograma *ir, DceVivas *v)
{
    if (ir == NULL || v == NULL) return 2;
    ResultadoMain m;
    if (resultado_de_main(ir, &m) != 0) return 2;
    int estado = calcular_vivas(ir, v, &m);
    liberar_resultado_main(&m);
    return estado;
}

/* Variables vivas, con el resultado de main ya calculado (ver Paso 1b). */
static int calcular_vivas(const IrPrograma *ir, DceVivas *v, const ResultadoMain *resultado)
{
    if (ir == NULL || v == NULL) return 2;
    memset(v, 0, sizeof *v);
    if (armar_variables(ir, v) != 0) {
        dce_liberar_vivas(v);
        return 2;
    }

    size_t nb = ir->cantidad_bloques, nv = v->cantidad_variables;
    v->cantidad_bloques = nb;
    if (nb > 0 && nv > SIZE_MAX / nb) {
        dce_liberar_vivas(v);
        return 2;
    }
    size_t celdas = (nb > 0 && nv > 0) ? nb * nv : 1;
    v->usa = calloc(celdas, 1);
    v->define = calloc(celdas, 1);
    v->entrada = calloc(celdas, 1);
    v->salida = calloc(celdas, 1);
    if (!v->usa || !v->define || !v->entrada || !v->salida) {
        dce_liberar_vivas(v);
        return 2;
    }

    /*
     * Paso 1: use_B y def_B de cada bloque, recorriéndolo hacia ADELANTE.
     * Una variable está en use_B si el bloque la LEE antes de asignarla
     * (su valor viene de antes del bloque). Por eso, en cada instrucción,
     * primero se miran los usos y después la definición:
     *     x = x gauss 1   ->  x está en use (se lee el x de antes) y en def.
     */
    for (size_t b = 0; b < nb; ++b) {
        unsigned char *usa = v->usa + b * nv, *define = v->define + b * nv;
        for (size_t i = ir->bloques[b].inicio; i < ir->bloques[b].fin; ++i) {
            const char *u[3];
            int n = usos(&ir->codigo[i], u);
            for (int k = 0; k < n; ++k) {
                size_t j = numero_de(v, u[k]);
                if (j != SIZE_MAX && !define[j]) usa[j] = 1;
            }
            size_t d = numero_de(v, definicion(&ir->codigo[i]));
            if (d != SIZE_MAX) define[d] = 1;
        }
    }

    /*
     * Paso 1b: qué está vivo al TERMINAR cada función (OUT[SALIDA]).
     *
     * La regla de los libros es OUT[SALIDA] = ∅: al terminar, nadie mira las
     * variables. Eso vale para las funciones comunes: sus variables locales
     * desaparecen al hacer "give", y lo que devuelven ya cuenta como uso del
     * "return".
     *
     * Pero en este proyecto el RESULTADO de un programa es la memoria final
     * de las variables de main: BAL no tiene print, y el simulador (y las
     * pruebas del equipo, por ejemplo tests/test_resolucion_saltos.c con
     * factorial.bal) leen ahí el valor final, como "resultado = 120".
     *
     * ¿Qué variables de main son el resultado? Las que el programa USA: las
     * que se leen en algún lugar de main. Una variable que solo se escribe y
     * nunca se lee no aporta nada al cálculo: es código muerto.
     *     OUT[SALIDA de main] = { variables de main que se leen en main }
     * Así, en factorial.bal:
     *   - "resultado : 1" (rama n <= 1) NO es código muerto: resultado se
     *     lee (resultado pitagoras i), así que es parte del resultado;
     *   - "t * declare_int : 4" SÍ es código muerto: t nunca se lee.
     * En main también muere una asignación que se REEMPLAZA antes de leerse
     * (x : 1; x : 2;), aunque x sí se lea después.
     * Se recalcula en cada pasada (diapositiva 28: eliminar código muerto
     * puede generar nuevo código muerto): si la única lectura de r está en
     * código muerto (por ejemplo "suma : r gauss s;" con suma sin leer), r
     * tampoco se usa de verdad y también muere.
     *
     * Los temporales ($tN) no son variables del programa: nunca cuentan.
     */
    unsigned char *de_main = calloc(nv ? nv : 1, 1);     /* Variables de main que se leen. */
    unsigned char *final_main = calloc(nb ? nb : 1, 1);  /* Bloques donde termina main. */
    if (de_main == NULL || final_main == NULL) {
        free(de_main);
        free(final_main);
        dce_liberar_vivas(v);
        return 2;
    }
    int en_main = 0;  /* Los bloques de una función van seguidos, desde su "func". */
    for (size_t b = 0; b < nb; ++b) {
        const IrInstr *primera = &ir->codigo[ir->bloques[b].inicio];
        if (primera->op == IR_FUNC) en_main = primera->dst && strcmp(primera->dst, "main") == 0;
        if (!en_main) continue;
        if (ir->bloques[b].cantidad_sucesores == 0) final_main[b] = 1;
    }
    /* Las variables que se leen en main (calculadas con el programa original). */
    for (size_t j = 0; j < nv; ++j) de_main[j] = (unsigned char)es_resultado(resultado, v->variables[j]);

    /*
     * Paso 2: algoritmo iterativo hasta el punto fijo (diapositiva 27).
     * Se empieza con todos los IN y OUT vacíos y se repite:
     *     OUT[B] = unión de IN[S] para cada sucesor S
     *     IN[B]  = use_B ∪ (OUT[B] − def_B)
     * hasta que ninguna vuelta cambie nada. Un bloque sin sucesores (el
     * final de una función) tiene OUT = OUT[SALIDA] del paso 1b: vacío,
     * salvo al final de main.
     * Como el análisis va hacia atrás, los bloques se recorren del último
     * al primero: así la información llega en menos vueltas.
     */
    int cambio = 1;
    while (cambio) {
        cambio = 0;
        v->iteraciones++;
        for (size_t b = nb; b-- > 0;) {
            unsigned char *out = v->salida + b * nv, *in = v->entrada + b * nv;
            const unsigned char *usa = v->usa + b * nv, *define = v->define + b * nv;
            const IrBloque *bloque = &ir->bloques[b];
            for (size_t j = 0; j < nv; ++j) {
                /* Al final de main, sus variables siguen vivas (paso 1b). */
                unsigned char nuevo_out = final_main[b] & de_main[j];
                for (size_t s = 0; s < bloque->cantidad_sucesores; ++s) {
                    nuevo_out |= v->entrada[bloque->sucesores[s] * nv + j];
                }
                unsigned char nuevo_in = usa[j] | (nuevo_out & !define[j]);
                if (nuevo_out != out[j] || nuevo_in != in[j]) cambio = 1;
                out[j] = nuevo_out;
                in[j] = nuevo_in;
            }
        }
    }
    free(de_main);
    free(final_main);
    return 0;
}

int dce_esta_viva(const DceVivas *v, size_t bloque, const char *variable, int salida)
{
    if (v == NULL || bloque >= v->cantidad_bloques) return 0;
    size_t j = numero_de(v, variable);
    if (j == SIZE_MAX) return 0;
    const unsigned char *conjunto = salida ? v->salida : v->entrada;
    return conjunto[bloque * v->cantidad_variables + j];
}

/* Escribe un conjunto como "{ a, b }". */
static void escribir_conjunto(const DceVivas *v, const unsigned char *conjunto, FILE *f)
{
    const char *separador = "";
    fputs("{ ", f);
    for (size_t j = 0; j < v->cantidad_variables; ++j) {
        if (!conjunto[j]) continue;
        fprintf(f, "%s%s", separador, v->variables[j]);
        separador = ", ";
    }
    fputs(separador[0] ? " }" : "}", f);
}

int dce_imprimir_vivas(const DceVivas *v, FILE *f)
{
    if (v == NULL || f == NULL) return 2;
    fprintf(f, "Variables vivas (punto fijo en %zu iteraciones)\n", v->iteraciones);
    size_t nv = v->cantidad_variables;
    for (size_t b = 0; b < v->cantidad_bloques; ++b) {
        fprintf(f, "[B%zu]\n  use = ", b);
        escribir_conjunto(v, v->usa + b * nv, f);
        fputs("\n  def = ", f);
        escribir_conjunto(v, v->define + b * nv, f);
        fputs("\n  IN  = ", f);
        escribir_conjunto(v, v->entrada + b * nv, f);
        fputs("\n  OUT = ", f);
        escribir_conjunto(v, v->salida + b * nv, f);
        fputc('\n', f);
    }
    return ferror(f) ? 2 : 0;
}

/* ================================================================== */
/* 4. Eliminación de código muerto (diapositiva 28)                    */
/* ================================================================== */

/* Vuelve a calcular los bloques básicos y el grafo de flujo (después de
 * quitar instrucciones o de volver fijo un salto). 0 bien, 2 sin memoria. */
static int rehacer_bloques(IrPrograma *ir)
{
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) free(ir->bloques[b].sucesores);
    free(ir->bloques);
    ir->bloques = NULL;
    ir->cantidad_bloques = 0;
    return ir_bloques_basicos(ir);
}

/* Guarda en el resultado una instrucción eliminada. 0 bien, 2 sin memoria. */
static int anotar(DceResultado *r, const IrInstr *x, DceTipo tipo)
{
    /* Las etiquetas no son código: se quitan sin anotarlas. */
    if (x->op == IR_LABEL) return 0;
    if (r->cantidad_eliminadas == r->capacidad) {
        size_t nueva = r->capacidad ? r->capacidad * 2 : 16;
        DceEliminada *mas = realloc(r->eliminadas, nueva * sizeof *mas);
        if (mas == NULL) return 2;
        r->eliminadas = mas;
        r->capacidad = nueva;
    }
    DceEliminada *e = &r->eliminadas[r->cantidad_eliminadas++];
    e->linea = x->linea;
    e->tipo = tipo;
    describir(x, e->texto, sizeof e->texto);
    if (tipo == DCE_INALCANZABLE) r->inalcanzables++;
    return 0;
}

/*
 * Una pasada: con las variables vivas ya calculadas, marca en "borrar" las
 * instrucciones muertas. Cada bloque se recorre hacia ATRÁS llevando el
 * conjunto de variables vivas en ese punto, empezando con OUT[B]
 * (como en el ejercicio 4 de la presentación):
 *
 *   - si la instrucción asigna x, x NO está viva y se puede eliminar,
 *     se marca y NO se tocan las vivas (sus usos ya no cuentan: así una
 *     cadena  a = ..., b = a, c = b  con c muerta cae entera en una pasada);
 *   - si no, se aplica la regla de la diapositiva 25 a esa instrucción:
 *     vivas = usos ∪ (vivas − {x}).
 *
 * Devuelve cuántas marcó, o SIZE_MAX si faltó memoria.
 */
static size_t marcar_muertas(const IrPrograma *ir, const DceVivas *v, unsigned char *borrar,
                             DceResultado *r)
{
    size_t nv = v->cantidad_variables, marcadas = 0;
    unsigned char *vivas = malloc(nv ? nv : 1);
    if (vivas == NULL) return SIZE_MAX;

    /* Se cuentan de nuevo en cada pasada; queda lo de la última. */
    r->llamadas_conservadas = 0;
    r->riesgosas_conservadas = 0;

    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {
        if (nv) memcpy(vivas, v->salida + b * nv, nv);
        for (size_t i = ir->bloques[b].fin; i-- > ir->bloques[b].inicio;) {
            const IrInstr *x = &ir->codigo[i];
            size_t d = numero_de(v, definicion(x));
            int muerta = d != SIZE_MAX && !vivas[d];

            if (muerta && se_puede_eliminar(x)) {
                borrar[i] = 1;
                marcadas++;
                continue;
            }
            /* Muerta pero con efectos o con riesgo de fallar: se queda. */
            if (muerta && x->op == IR_CALL) r->llamadas_conservadas++;
            if (muerta && puede_fallar(x)) r->riesgosas_conservadas++;

            if (d != SIZE_MAX) vivas[d] = 0;
            const char *u[3];
            int n = usos(x, u);
            for (int k = 0; k < n; ++k) {
                size_t j = numero_de(v, u[k]);
                if (j != SIZE_MAX) vivas[j] = 1;
            }
        }
    }
    free(vivas);
    return marcadas;
}

/* Quita de la IR las instrucciones marcadas y vuelve a armar los bloques. */
static int quitar_marcadas(IrPrograma *ir, const unsigned char *borrar, DceResultado *r,
                           DceTipo tipo)
{
    size_t quedan = 0;
    for (size_t i = 0; i < ir->cantidad; ++i) {
        IrInstr *x = &ir->codigo[i];
        if (borrar[i]) {
            if (anotar(r, x, tipo) != 0) return 2;
            free(x->dst);
            free(x->a);
            free(x->b);
            free(x->operador);
            continue;
        }
        ir->codigo[quedan++] = *x;
    }
    ir->cantidad = quedan;

    /* Los índices de las instrucciones cambiaron: los bloques se calculan
     * de nuevo con la función de la IR. */
    return rehacer_bloques(ir);
}

/* ================================================================== */
/* 5. Tipo 2: código inalcanzable                                      */
/* ================================================================== */

/*
 * Plegado de constantes (diapositiva 20), solo lo necesario para saber si
 * la condición de un salto se conoce al compilar.
 *
 * Solo cuentan las condiciones ESCRITAS con constantes:
 *     whether (declare_false)        la condición ya es una constante
 *     whale (1 > 2)                  $t3 = 1 > 2: operandos constantes
 *     whether (~declare_true)        $t4 = ~ declare_true
 * Los temporales ($tN) que calculan la condición tienen una sola definición
 * y sus operandos son constantes o temporales ya conocidos, así que su valor
 * se calcula al compilar.
 *
 * NO se propagan valores de VARIABLES: en
 *     n * declare_int : 5;   whether (n <= 1) { ... }
 * n es un dato del programa (con otro n la rama sí se ejecuta), así que ese
 * whether no es código inalcanzable aunque n valga 5 en este código.
 */
typedef struct {
    const char *nombre;
    int definiciones;
    int conocido;
    long long valor;
} Constante;

/* La entrada de "nombre" en la tabla, o NULL. */
static Constante *buscar_constante(Constante *tabla, size_t cantidad, const char *nombre)
{
    for (size_t i = 0; i < cantidad; ++i) {
        if (strcmp(tabla[i].nombre, nombre) == 0) return &tabla[i];
    }
    return NULL;
}

/* El valor de un operando si se conoce al compilar: un número, declare_true
 * (1), declare_false (0) o una variable constante. 1 si se conoce. */
static int valor_de(const char *operando, Constante *tabla, size_t cantidad, long long *valor)
{
    if (operando == NULL) return 0;
    if (es_numero(operando, valor)) return 1;
    if (strcmp(operando, "declare_true") == 0) { *valor = 1; return 1; }
    if (strcmp(operando, "declare_false") == 0) { *valor = 0; return 1; }
    Constante *c = buscar_constante(tabla, cantidad, operando);
    if (c != NULL && c->definiciones == 1 && c->conocido) {
        *valor = c->valor;
        return 1;
    }
    return 0;
}

/* Calcula "a operador b" al compilar, como lo haría la máquina (enteros de
 * 32 bits). 1 si se pudo; 0 si el operador no se calcula aquí o la
 * operación fallaría (división entre 0, exponente negativo). */
static int plegar(const char *op, long long a, long long b, long long *r)
{
    if (op == NULL) return 0;
    if (!strcmp(op, "gauss")) *r = (int32_t)(a + b);
    else if (!strcmp(op, "neumann")) *r = (int32_t)(a - b);
    else if (!strcmp(op, "pitagoras")) *r = (int32_t)(a * b);
    else if (!strcmp(op, "euclides")) { if (b == 0) return 0; *r = (int32_t)(a / b); }
    else if (!strcmp(op, "euler")) { if (b == 0) return 0; *r = (int32_t)(a % b); }
    else if (!strcmp(op, "descartes")) {
        if (b < 0 || b > 62) return 0;
        long long p = 1;
        for (long long k = 0; k < b; ++k) p = (int32_t)(p * a);
        *r = p;
    }
    else if (!strcmp(op, "<")) *r = a < b;
    else if (!strcmp(op, ">")) *r = a > b;
    else if (!strcmp(op, "<=")) *r = a <= b;
    else if (!strcmp(op, ">=")) *r = a >= b;
    else if (!strcmp(op, "==")) *r = a == b;
    else if (!strcmp(op, "=/=")) *r = a != b;
    else if (!strcmp(op, "&&")) *r = (a != 0) && (b != 0);
    else if (!strcmp(op, "||")) *r = (a != 0) || (b != 0);
    else if (!strcmp(op, "^")) *r = (a != 0) != (b != 0);
    else return 0;
    return 1;
}

/*
 * Resuelve los saltos condicionales cuya condición es constante, función
 * por función. Si un "ifFalse c goto L":
 *   - SIEMPRE salta (c es falso): se vuelve "goto L", un salto fijo. La rama
 *     que venía después queda sin camino: inalcanzable.
 *   - NUNCA salta (c es verdadero): se elimina; L puede quedar inalcanzable.
 * (Igual con "ifTrue", al revés.) Un salto vuelto fijo se marca con
 * operador "fijo", para reconocerlo después.
 * Devuelve cuántos saltos resolvió, o SIZE_MAX sin memoria.
 */
static size_t resolver_condiciones(IrPrograma *ir, DceResultado *r)
{
    size_t resueltos = 0;
    unsigned char *borrar = calloc(ir->cantidad ? ir->cantidad : 1, 1);
    if (borrar == NULL) return SIZE_MAX;

    for (size_t inicio = 0; inicio < ir->cantidad; ++inicio) {
        if (ir->codigo[inicio].op != IR_FUNC) continue;
        size_t fin = inicio + 1;
        while (fin < ir->cantidad && ir->codigo[fin].op != IR_END_FUNC) fin++;

        /* La tabla de esta función: cuántas veces se define cada nombre. */
        size_t cantidad = 0;
        Constante *tabla = calloc(fin - inicio + 1, sizeof *tabla);
        if (tabla == NULL) {
            free(borrar);
            return SIZE_MAX;
        }
        for (size_t i = inicio; i < fin; ++i) {
            const char *d = definicion(&ir->codigo[i]);
            /* Solo temporales: las variables no se propagan (ver arriba). */
            if (d == NULL || d[0] != '$') continue;
            Constante *c = buscar_constante(tabla, cantidad, d);
            if (c == NULL) {
                c = &tabla[cantidad++];
                c->nombre = d;
            }
            c->definiciones++;
        }

        /* Propagar y plegar hasta que nada cambie (punto fijo). */
        int cambio = 1;
        while (cambio) {
            cambio = 0;
            for (size_t i = inicio; i < fin; ++i) {
                const IrInstr *x = &ir->codigo[i];
                const char *d = definicion(x);
                Constante *c = d ? buscar_constante(tabla, cantidad, d) : NULL;
                if (c == NULL || c->definiciones != 1 || c->conocido) continue;
                long long a = 0, b = 0, v = 0;
                int sabe = 0;
                if (x->op == IR_COPY) {
                    sabe = valor_de(x->a, tabla, cantidad, &v);
                } else if (x->op == IR_BIN) {
                    sabe = valor_de(x->a, tabla, cantidad, &a) && valor_de(x->b, tabla, cantidad, &b) &&
                           plegar(x->operador, a, b, &v);
                } else if (x->op == IR_UNARY && valor_de(x->a, tabla, cantidad, &a) && x->operador) {
                    if (!strcmp(x->operador, "~")) { v = (a == 0); sabe = 1; }
                    else if (!strcmp(x->operador, "neumann")) { v = (int32_t)(-a); sabe = 1; }
                    else if (!strcmp(x->operador, "bool")) { v = (a != 0); sabe = 1; }
                }
                if (sabe) {
                    c->conocido = 1;
                    c->valor = v;
                    cambio = 1;
                }
            }
        }

        /* Los saltos condicionales con condición constante. */
        for (size_t i = inicio; i < fin; ++i) {
            IrInstr *x = &ir->codigo[i];
            long long v = 0;
            if ((x->op != IR_JFALSE && x->op != IR_JTRUE) || !valor_de(x->a, tabla, cantidad, &v)) continue;
            int salta = (x->op == IR_JFALSE) ? (v == 0) : (v != 0);
            resueltos++;
            if (salta) {
                /* Siempre salta: "goto L" fijo. La condición ya no se usa. */
                char *fijo = malloc(5);
                if (fijo == NULL) {
                    free(tabla);
                    free(borrar);
                    return SIZE_MAX;
                }
                strcpy(fijo, "fijo");
                free(x->a);
                free(x->operador);
                x->a = NULL;
                x->operador = fijo;
                x->op = IR_JUMP;
            } else {
                borrar[i] = 1;  /* Nunca salta: el salto sobra. */
            }
        }
        free(tabla);
        inicio = fin;
    }

    r->condiciones_constantes += resueltos;
    int estado = 0;
    int hay_borrar = 0;
    for (size_t i = 0; i < ir->cantidad; ++i) hay_borrar |= borrar[i];
    if (hay_borrar) estado = quitar_marcadas(ir, borrar, r, DCE_INALCANZABLE);
    else if (resueltos > 0) estado = rehacer_bloques(ir);
    free(borrar);
    return estado == 0 ? resueltos : SIZE_MAX;
}

/*
 * Quita los bloques INALCANZABLES: se recorre el grafo de flujo desde el
 * bloque donde empieza cada función (diapositiva 15) siguiendo los
 * sucesores; los bloques que no se visitan nunca se ejecutan. De ellos se
 * quita todo, menos el inicio y el fin de la función ("func" y "endfunc").
 * También se quita un salto fijo (de resolver_condiciones) que va a la
 * etiqueta que sigue justo después: ya no salta nada.
 * Devuelve cuántas instrucciones quitó, o SIZE_MAX sin memoria.
 */
static size_t quitar_inalcanzables(IrPrograma *ir, DceResultado *r)
{
    size_t nb = ir->cantidad_bloques;
    unsigned char *visto = calloc(nb ? nb : 1, 1);
    size_t *pendientes = malloc((nb ? nb : 1) * sizeof *pendientes);
    unsigned char *borrar = calloc(ir->cantidad ? ir->cantidad : 1, 1);
    if (visto == NULL || pendientes == NULL || borrar == NULL) {
        free(visto);
        free(pendientes);
        free(borrar);
        return SIZE_MAX;
    }

    /* Recorrido desde el inicio de cada función. */
    size_t tope = 0;
    for (size_t b = 0; b < nb; ++b) {
        if (ir->codigo[ir->bloques[b].inicio].op == IR_FUNC) {
            visto[b] = 1;
            pendientes[tope++] = b;
        }
    }
    while (tope > 0) {
        const IrBloque *bloque = &ir->bloques[pendientes[--tope]];
        for (size_t s = 0; s < bloque->cantidad_sucesores; ++s) {
            size_t sig = bloque->sucesores[s];
            if (!visto[sig]) {
                visto[sig] = 1;
                pendientes[tope++] = sig;
            }
        }
    }

    size_t quitadas = 0;
    for (size_t b = 0; b < nb; ++b) {
        if (visto[b]) continue;
        for (size_t i = ir->bloques[b].inicio; i < ir->bloques[b].fin; ++i) {
            IrOp op = ir->codigo[i].op;
            if (op == IR_FUNC || op == IR_END_FUNC) continue;
            borrar[i] = 1;
            quitadas++;
        }
    }

    /* Un salto fijo a la etiqueta que viene justo después (saltando solo
     * etiquetas) no hace nada: la ejecución llega ahí de todas formas. */
    for (size_t i = 0; i < ir->cantidad; ++i) {
        const IrInstr *x = &ir->codigo[i];
        if (borrar[i] || x->op != IR_JUMP || x->operador == NULL || strcmp(x->operador, "fijo") != 0) continue;
        for (size_t k = i + 1; k < ir->cantidad && ir->codigo[k].op == IR_LABEL; ++k) {
            if (!borrar[k] && ir->codigo[k].dst && x->dst && !strcmp(ir->codigo[k].dst, x->dst)) {
                borrar[i] = 1;
                quitadas++;
                break;
            }
        }
    }

    int estado = quitadas > 0 ? quitar_marcadas(ir, borrar, r, DCE_INALCANZABLE) : 0;
    free(visto);
    free(pendientes);
    free(borrar);
    return estado == 0 ? quitadas : SIZE_MAX;
}

int dce_eliminar(IrPrograma *ir, DceResultado *r)
{
    if (ir == NULL || r == NULL) return 2;
    memset(r, 0, sizeof *r);
    r->instrucciones_antes = ir->cantidad;

    /*
     * Procedimiento de la diapositiva 28:
     *   1. calcular las variables vivas de todo el grafo de flujo;
     *   2. eliminar las asignaciones a variables que no están vivas;
     *   3. repetir: lo eliminado ya no usa sus operandos, y otras
     *      asignaciones pueden haber quedado muertas.
     */
    for (;;) {
        /* Tipo 2: condiciones constantes y bloques inalcanzables. */
        size_t resueltos = resolver_condiciones(ir, r);
        if (resueltos == SIZE_MAX) return 2;
        size_t inalcanzables = quitar_inalcanzables(ir, r);
        if (inalcanzables == SIZE_MAX) return 2;

        /* Tipo 1: variables vivas. */
        DceVivas vivas;
        if (dce_calcular_vivas(ir, &vivas) != 0) return 2;
        r->pasadas++;

        unsigned char *borrar = calloc(ir->cantidad ? ir->cantidad : 1, 1);
        if (borrar == NULL) {
            dce_liberar_vivas(&vivas);
            return 2;
        }
        size_t marcadas = marcar_muertas(ir, &vivas, borrar, r);
        dce_liberar_vivas(&vivas);

        int estado = 0;
        if (marcadas == SIZE_MAX) estado = 2;
        else if (marcadas > 0) estado = quitar_marcadas(ir, borrar, r, DCE_NO_USADA);
        free(borrar);
        if (estado != 0) return estado;
        /* Punto fijo: ninguno de los dos tipos encontró nada. */
        if (marcadas == 0 && resueltos == 0 && inalcanzables == 0) break;
    }

    r->instrucciones_despues = ir->cantidad;
    return 0;
}

int dce_imprimir_resultado(const DceResultado *r, FILE *f)
{
    if (r == NULL || f == NULL) return 2;
    fprintf(f, "Codigo muerto eliminado: %zu de %zu instrucciones (quedan %zu), en %zu pasada%s\n",
            r->cantidad_eliminadas, r->instrucciones_antes, r->instrucciones_despues, r->pasadas,
            r->pasadas == 1 ? "" : "s");
    for (size_t i = 0; i < r->cantidad_eliminadas; ++i) {
        fprintf(f, "  - linea %zu: %s%s\n", r->eliminadas[i].linea, r->eliminadas[i].texto,
                r->eliminadas[i].tipo == DCE_INALCANZABLE ? "   (inalcanzable)" : "");
    }
    if (r->inalcanzables > 0 || r->condiciones_constantes > 0) {
        fprintf(f, "Tipo 2: %zu instrucciones inalcanzables; %zu condiciones constantes\n",
                r->inalcanzables, r->condiciones_constantes);
    }
    if (r->llamadas_conservadas > 0) {
        fprintf(f, "Llamadas conservadas aunque su resultado no se usa (pueden tener efectos): %zu\n",
                r->llamadas_conservadas);
    }
    if (r->riesgosas_conservadas > 0) {
        fprintf(f, "Operaciones conservadas porque pueden fallar (division o potencia): %zu\n",
                r->riesgosas_conservadas);
    }
    return ferror(f) ? 2 : 0;
}

void dce_liberar_resultado(DceResultado *r)
{
    if (r == NULL) return;
    free(r->eliminadas);
    memset(r, 0, sizeof *r);
}
