/* =====================================================================
 * loop_unrolling.c  --  Optimizacion de LOOP UNROLLING (Proyecto II)
 * =====================================================================
 *
 * Como Funciona
 * ---------------------------
 * Recibe la IR de tres direcciones que genera src/Representacion_Intermedia/ir.c (un arreglo de
 * instrucciones IrInstr) y "desenrolla" los ciclos: copia el cuerpo del
 * ciclo varias veces para ejecutar menos comparaciones y saltos. Ademas
 * deja bloques basicos mas largos, que luego aprovechan el reordenamiento
 * y la calendarizacion VLIW (mas instrucciones para llenar los slots).
 *
 * Hay dos tipos de desenrollado:
 *   - TOTAL:   si se sabe cuantas vueltas da el ciclo (N) y son pocas,
 *              se copia el cuerpo N veces y el ciclo desaparece.
 *   - PARCIAL: se copia el cuerpo k veces dentro de un ciclo nuevo
 *              (factor k) y las vueltas que sobran se ejecutan aparte.
 *
 * COMO ESTA ORGANIZADO (de arriba hacia abajo)
 * --------------------------------------------
 *   1. Utilidades de cadenas y listas      (duplicar, comparar, listas)
 *   2. Construccion de la IR nueva         (Codigo, poner, reemplazar)
 *   3. Reconocimiento del ciclo canonico   (reconocer, verificar)
 *   4. Calculo de vueltas                  (valor_entrada, vueltas)
 *   5. Plan y aplicacion                   (planear, emitir_copia, aplicar)
 *   6. Interfaz publica                    (lu_opciones_defecto, lu_desenrollar)
 *
 * La explicacion completa de reglas y ejemplos esta en README.md.
 * ===================================================================== */

#include "loop_unrolling.h"   /* Tipos publicos: LuOpciones, LuResumen, IrPrograma */
#include <errno.h>            /* errno: para detectar desbordes en strtoll */
#include <limits.h>           /* UINT_MAX y LLONG_MAX: limites de contadores */
#include <stdint.h>           /* SIZE_MAX: limite para no desbordar realloc */
#include <stdlib.h>           /* malloc, realloc, free, strtoll */
#include <string.h>           /* strlen, strcmp, memcpy */

/* ===================================================================== */
/* 1. UTILIDADES DE CADENAS Y LISTAS                                      */
/* ===================================================================== */

/* Devuelve una copia nueva de la cadena s (o NULL si s es NULL).
 * Se usa porque cada instruccion de la IR es duena de sus cadenas. */
static char *duplicar(const char *s) {
    if (!s) return NULL;                  /* Nada que copiar */
    size_t n = strlen(s);                 /* Largo sin el '\0' final */
    char *p = malloc(n + 1);              /* Reserva espacio para el '\0' */
    if (p) memcpy(p, s, n + 1);           /* Copia el texto y el '\0' */
    return p;                             /* NULL si no hubo memoria */
}

/* Compara dos cadenas; si alguna es NULL se consideran distintas. */
static int igual(const char *a, const char *b) { return a && b && !strcmp(a, b); }

/* Los nombres que empiezan con '$' los inventa la IR (no son del usuario):
 * $tN son temporales y $LN son etiquetas. */
static int es_interno(const char *s) { return s && s[0] == '$'; }

/* Verdadero si el nombre es un temporal ($t1, $t2, ...). */
static int es_temporal(const char *s) { return s && s[0] == '$' && s[1] == 't'; }

/* Convierte s a entero en *v. Solo acepta un numero completo ("12" si,
 * "12a" o "n" no). Devuelve 1 si lo logro y 0 si no es un entero. */
static int entero(const char *s, long long *v) {
    if (!s || !*s) return 0;              /* Cadena nula o vacia: no es numero */
    char *fin = NULL;                     /* strtoll indica donde dejo de leer */
    errno = 0;                            /* Se limpia para detectar desborde */
    long long x = strtoll(s, &fin, 10);   /* Lee el numero en base 10 */
    if (errno || !fin || *fin) return 0;  /* Desborde o sobro texto: no es entero */
    *v = x;                               /* Guarda el valor leido */
    return 1;                             /* Exito */
}

/* Lista dinamica de cadenas. Cada cadena guardada es propiedad de la lista
 * (la lista la libera). v = arreglo, n = cantidad usada, cap = capacidad. */
typedef struct { char **v; size_t n, cap; } Lista;

/* Agrega a la lista una cadena YA reservada (la lista se queda con ella).
 * Si falla, libera s para no perder memoria. Devuelve 1 si la agrego. */
static int lista_agregar_propia(Lista *l, char *s) {
    if (!s) return 0;                                 /* La cadena no se pudo crear */
    if (l->n == l->cap) {                             /* Arreglo lleno: hay que crecer */
        size_t cap = l->cap ? l->cap * 2 : 16;        /* Duplica (empieza en 16) */
        char **p = realloc(l->v, cap * sizeof *p);    /* Pide el arreglo mas grande */
        if (!p) { free(s); return 0; }                /* Sin memoria: libera y falla */
        l->v = p; l->cap = cap;                       /* Usa el arreglo nuevo */
    }
    l->v[l->n++] = s;                                 /* Guarda la cadena al final */
    return 1;
}

/* Agrega una COPIA de s a la lista. */
static int lista_agregar(Lista *l, const char *s) { return lista_agregar_propia(l, duplicar(s)); }

/* Busca s en la lista. Devuelve su posicion, o -1 si no esta. */
static long lista_buscar(const Lista *l, const char *s) {
    for (size_t i = 0; i < l->n; ++i) if (igual(l->v[i], s)) return (long)i; /* Encontrada */
    return -1;                                                               /* No esta */
}

/* Libera todas las cadenas, el arreglo, y deja la lista vacia. */
static void lista_liberar(Lista *l) {
    for (size_t i = 0; i < l->n; ++i) free(l->v[i]); /* Cada cadena */
    free(l->v);                                      /* El arreglo */
    *l = (Lista){0};                                 /* Todo en cero */
}

/* ===================================================================== */
/* 2. CONSTRUCCION DE LA IR NUEVA                                         */
/* ===================================================================== */
/* La transformacion no modifica la IR "en el lugar": arma un arreglo
 * nuevo de instrucciones (Codigo) y al final lo pone en lugar del viejo. */

/* Arreglo de instrucciones en construccion. error queda en 1 si falto
 * memoria en algun momento (asi no hay que revisar cada llamada). */
typedef struct { IrInstr *v; size_t n, cap; int error; } Codigo;

/* Agrega al final de c una instruccion nueva con copias de sus cadenas.
 * op = operacion, dst/a/b = operandos, operador = "gauss", "<", etc. */
static void poner(Codigo *c, IrOp op, const char *dst, const char *a,
                  const char *b, const char *operador, size_t linea) {
    if (c->error) return;                                     /* Ya fallo antes: no sigue */
    if (c->n == c->cap) {                                     /* Arreglo lleno */
        size_t cap = c->cap ? c->cap * 2 : 64;                /* Nueva capacidad */
        if (cap > SIZE_MAX / sizeof *c->v) { c->error = 1; return; } /* Evita desborde */
        IrInstr *p = realloc(c->v, cap * sizeof *p);          /* Agranda el arreglo */
        if (!p) { c->error = 1; return; }                     /* Sin memoria */
        c->v = p; c->cap = cap;                               /* Usa el arreglo nuevo */
    }
    /* Arma la instruccion con copias propias de cada cadena. */
    IrInstr x = {op, duplicar(dst), duplicar(a), duplicar(b), duplicar(operador), linea};
    /* Si alguna copia fallo (habia texto pero salio NULL), se deshace todo. */
    if ((dst && !x.dst) || (a && !x.a) || (b && !x.b) || (operador && !x.operador)) {
        free(x.dst); free(x.a); free(x.b); free(x.operador); /* Libera lo que si se copio */
        c->error = 1; return;                                /* Marca el error */
    }
    c->v[c->n++] = x;                                         /* Guarda la instruccion */
}

/* Copia sin cambios las instrucciones [desde, hasta) de la IR original. */
static void copiar_rango(Codigo *c, const IrPrograma *ir, size_t desde, size_t hasta) {
    for (size_t k = desde; k < hasta; ++k) {                  /* Recorre el rango */
        const IrInstr *x = &ir->codigo[k];                    /* Instruccion original */
        poner(c, x->op, x->dst, x->a, x->b, x->operador, x->linea); /* Copia identica */
    }
}

/* Libera n instrucciones (sus cadenas) y el arreglo que las contiene. */
static void liberar_instrucciones(IrInstr *v, size_t n) {
    for (size_t k = 0; k < n; ++k) { free(v[k].dst); free(v[k].a); free(v[k].b); free(v[k].operador); }
    free(v);
}

/* Sustituye el codigo de ir por el codigo nuevo c. Los bloques basicos
 * viejos ya no sirven (las posiciones cambiaron), por eso se borran; se
 * recalculan una sola vez al final con ir_bloques_basicos(). */
static void reemplazar(IrPrograma *ir, Codigo *c) {
    liberar_instrucciones(ir->codigo, ir->cantidad);                         /* Codigo viejo */
    for (size_t k = 0; k < ir->cantidad_bloques; ++k) free(ir->bloques[k].sucesores); /* Aristas viejas */
    free(ir->bloques);                                                       /* Bloques viejos */
    ir->bloques = NULL; ir->cantidad_bloques = 0;                            /* Sin bloques por ahora */
    ir->codigo = c->v; ir->cantidad = c->n; ir->capacidad = c->cap;          /* Codigo nuevo */
    *c = (Codigo){0};                                                        /* c ya no es dueno */
}

/* Crea un nombre que no exista todavia: "$tN" (tipo 't') o "$LN" (tipo 'L').
 * Usa los contadores del propio IrPrograma, asi nunca choca con los de src/Representacion_Intermedia/ir.c. */
static char *nombre_nuevo(IrPrograma *ir, char tipo) {
    unsigned *contador = tipo == 't' ? &ir->temporales : &ir->etiquetas; /* Cual contador */
    if (*contador == UINT_MAX) return NULL;                              /* Se acabaron los numeros */
    char texto[32];                                                      /* Buffer temporal */
    snprintf(texto, sizeof texto, "$%c%u", tipo, ++*contador);           /* Ej: "$t17" */
    return duplicar(texto);                                              /* Copia en memoria dinamica */
}

/* ===================================================================== */
/* 3. RECONOCIMIENTO DEL CICLO CANONICO                                   */
/* ===================================================================== */
/* Un ciclo "canonico" es el que genera `cycle` en src/Representacion_Intermedia/ir.c:
 *
 *     $Lx:                       <- cabecera
 *     $tc = i < limite           (o <=)
 *     ifFalse $tc goto $Ly
 *     ...cuerpo...
 *     $tp = i gauss paso         <- incremento
 *     i = $tp
 *     goto $Lx                   <- regreso
 *     $Ly:                       <- salida
 */

/* Posiciones y datos de un ciclo encontrado. Los punteros const char*
 * apuntan a cadenas de la IR actual (dejan de valer si se reemplaza). */
typedef struct {
    size_t cabecera;               /* Posicion de "$Lx:" */
    size_t cuerpo_ini, cuerpo_fin; /* Cuerpo = [cuerpo_ini, cuerpo_fin), sin el incremento */
    size_t incremento;             /* Posicion de "$tp = i gauss paso" (le sigue "i = $tp") */
    size_t regreso;                /* Posicion de "goto $Lx" */
    size_t salida;                 /* Posicion de "$Ly:" */
    const char *iter;              /* Nombre del iterador (ej. "i") */
    const char *limite;            /* Limite: variable o numero (ej. "n" o "5") */
    const char *relacion;          /* "<" o "<=" */
    const char *paso_txt;          /* Paso como texto (ej. "1") */
    const char *etq_ini, *etq_fin; /* Nombres de $Lx y $Ly */
    long long paso;                /* Paso como numero */
    size_t linea;                  /* Linea del fuente (para los mensajes) */
} Ciclo;

/* Devuelve el nombre de la variable que la instruccion x ESCRIBE,
 * o NULL si no escribe ninguna (saltos, etiquetas, store, arg, ...). */
static const char *escrito(const IrInstr *x) {
    switch (x->op) {
    case IR_COPY: case IR_BIN: case IR_UNARY: case IR_CALL: case IR_LOAD: /* dst = ... */
    case IR_SIZE: case IR_INDEX_REF: case IR_ARRAY_DECL: case IR_LIST_DECL:
    case IR_PARAM_DECL:
        return x->dst;            /* En todas estas, dst es lo que se escribe */
    default:
        return NULL;              /* El resto no escribe una variable */
    }
}

/* Verdadero si la operacion es un salto (goto, ifFalse, ifTrue). */
static int es_salto(IrOp op) { return op == IR_JUMP || op == IR_JFALSE || op == IR_JTRUE; }

/* Revisa si la etiqueta en la posicion h es la cabecera de un ciclo.
 * Devuelve:  0 -> no es ciclo (nadie salta hacia atras a ella)
 *            1 -> ciclo canonico; sus datos quedan en *c
 *           -1 -> es un ciclo pero con otra forma; *motivo dice por que */
static int reconocer(const IrPrograma *ir, size_t h, Ciclo *c, const char **motivo) {
    const IrInstr *v = ir->codigo;           /* Atajo al arreglo de instrucciones */
    size_t n = ir->cantidad;                 /* Total de instrucciones */
    if (v[h].op != IR_LABEL) return 0;       /* Un ciclo empieza con una etiqueta */
    const char *etq = v[h].dst;              /* Nombre de la etiqueta ($Lx) */
    size_t g = n;                            /* Posicion del "goto $Lx" (n = no encontrado) */
    /* Busca hacia adelante, sin salir de la funcion, un goto a esta etiqueta. */
    for (size_t k = h + 1; k < n && v[k].op != IR_END_FUNC && v[k].op != IR_FUNC; ++k)
        if (v[k].op == IR_JUMP && igual(v[k].dst, etq)) { g = k; break; }
    if (g == n) return 0;                    /* Sin salto hacia atras: no es ciclo */

    *c = (Ciclo){0};                         /* Limpia el resultado */
    c->cabecera = h; c->regreso = g; c->etq_ini = etq;          /* Datos ya conocidos */
    c->linea = h + 1 < n ? v[h + 1].linea : v[h].linea;         /* Linea para mensajes */
    const IrInstr *cond = &v[h + 1], *jf = &v[h + 2];           /* Condicion y su ifFalse */
    long long tmp;                                              /* Auxiliar para entero() */
    /* La condicion debe ser "$tc = i < limite" o "<=", donde i es una
     * variable del usuario (no numero ni temporal) y limite no es temporal. */
    if (h + 2 >= g || cond->op != IR_BIN ||
        !(igual(cond->operador, "<") || igual(cond->operador, "<=")) ||
        !cond->a || es_interno(cond->a) || entero(cond->a, &tmp) ||
        !cond->b || es_interno(cond->b)) {
        *motivo = "la condicion no tiene la forma 'i < limite' o 'i <= limite'";
        return -1;
    }
    /* Justo despues debe venir "ifFalse $tc goto $Ly". */
    if (jf->op != IR_JFALSE || !igual(jf->a, cond->dst)) {
        *motivo = "la condicion no va seguida de 'ifFalse ... goto salida'";
        return -1;
    }
    /* Despues del "goto $Lx" debe estar la etiqueta de salida "$Ly:". */
    if (g + 1 >= n || v[g + 1].op != IR_LABEL || !igual(v[g + 1].dst, jf->dst)) {
        *motivo = "la etiqueta de salida no esta justo despues del salto de regreso";
        return -1;
    }
    c->iter = cond->a; c->limite = cond->b; c->relacion = cond->operador; /* Guarda la condicion */
    c->etq_fin = jf->dst; c->salida = g + 1;                              /* Guarda la salida */
    /* Las dos instrucciones antes del goto deben ser el incremento:
     * "$tp = i gauss paso" (paso entero > 0) y luego "i = $tp". */
    const IrInstr *inc = g >= 2 ? &v[g - 2] : NULL, *asig = &v[g - 1];
    if (g < h + 5 || inc->op != IR_BIN || !igual(inc->operador, "gauss") ||
        !igual(inc->a, c->iter) || !entero(inc->b, &c->paso) || c->paso <= 0 ||
        !es_temporal(inc->dst) || asig->op != IR_COPY || !igual(asig->dst, c->iter) ||
        !igual(asig->a, inc->dst)) {
        *motivo = "el iterador no avanza con un paso constante positivo al final del cuerpo";
        return -1;
    }
    c->paso_txt = inc->b;          /* Paso en texto, para copiarlo en el incremento nuevo */
    c->incremento = g - 2;         /* Donde empieza el incremento */
    c->cuerpo_ini = h + 3;         /* El cuerpo empieza despues del ifFalse */
    c->cuerpo_fin = g - 2;         /* y termina antes del incremento */
    return 1;                      /* Es un ciclo canonico */
}

/* Revisa las PRECONDICIONES: que copiar el cuerpo no cambie el resultado.
 * Devuelve 1 si se puede desenrollar, 0 si no (*motivo dice por que),
 * y 2 si el cuerpo tiene otro ciclo dentro (se intenta despues). */
static int verificar(const IrPrograma *ir, const Ciclo *c, const LuOpciones *op, const char **motivo) {
    const IrInstr *v = ir->codigo;                   /* Atajo al arreglo */
    size_t tam = c->cuerpo_fin - c->cuerpo_ini;      /* Instrucciones del cuerpo */
    for (size_t k = c->cuerpo_ini; k < c->cuerpo_fin; ++k) { /* Revisa cada instruccion */
        const IrInstr *x = &v[k];
        /* Un return sale del ciclo a mitad de camino: no se puede copiar. */
        if (x->op == IR_RETURN) { *motivo = "el cuerpo contiene un return"; return 0; }
        /* Inicio/fin de funcion dentro del cuerpo seria IR invalida. */
        if (x->op == IR_FUNC || x->op == IR_END_FUNC || x->op == IR_PARAM_DECL) {
            *motivo = "el cuerpo contiene limites de funcion"; return 0;
        }
        const char *w = escrito(x);                  /* Que variable escribe */
        /* Si el cuerpo cambia i o el limite, ya no se sabe cuantas vueltas da. */
        if (igual(w, c->iter)) { *motivo = "el cuerpo modifica el iterador"; return 0; }
        if (igual(w, c->limite)) { *motivo = "el cuerpo modifica el limite"; return 0; }
        if (es_salto(x->op)) {                       /* Revisa a donde salta */
            size_t destino = c->cuerpo_fin;          /* cuerpo_fin = "no esta en el cuerpo" */
            for (size_t q = c->cuerpo_ini; q < c->cuerpo_fin; ++q) /* Busca la etiqueta destino */
                if (v[q].op == IR_LABEL && igual(v[q].dst, x->dst)) { destino = q; break; }
            /* Saltar fuera del cuerpo (como un break) rompe las copias. */
            if (destino == c->cuerpo_fin) { *motivo = "el cuerpo salta fuera del ciclo"; return 0; }
            /* Saltar hacia atras dentro del cuerpo = hay un ciclo interno. */
            if (destino < k) { *motivo = "contiene un ciclo interno que no se pudo desenrollar"; return 2; }
        }
    }
    /* Un cuerpo muy grande haria crecer demasiado el codigo. */
    if (tam > op->max_cuerpo) { *motivo = "el cuerpo supera el tamano maximo"; return 0; }
    return 1;                                        /* Cumple todas las precondiciones */
}

/* ===================================================================== */
/* 4. CALCULO DE VUELTAS                                                  */
/* ===================================================================== */

/* Intenta saber el valor de `nombre` al entrar al ciclo (posicion h).
 * Si es un numero, es ese numero. Si es una variable, busca hacia atras
 * su ultima asignacion dentro del mismo bloque; si fue "nombre = 5", vale 5.
 * Esto es valido porque verificar() ya garantizo que el cuerpo no cambia
 * ni el iterador ni el limite. Devuelve 1 si lo supo y 0 si no. */
static int valor_entrada(const IrPrograma *ir, size_t h, const char *nombre, long long *v) {
    if (entero(nombre, v)) return 1;                      /* Ya es un numero */
    for (size_t k = h; k-- > 0;) {                        /* Recorre hacia atras desde h-1 */
        const IrInstr *x = &ir->codigo[k];
        /* Si se llega a una etiqueta o salto, el valor podria venir de
         * varios caminos: no se puede asegurar, se rinde. */
        if (x->op == IR_LABEL || x->op == IR_FUNC || x->op == IR_RETURN ||
            x->op == IR_END_FUNC || es_salto(x->op)) return 0;
        /* Ultima escritura de la variable: sirve solo si es "nombre = numero". */
        if (igual(escrito(x), nombre)) return x->op == IR_COPY && entero(x->a, v);
    }
    return 0;                                             /* Nunca se asigno */
}

/* Cuantas vueltas da: for (i = ini; i < lim (o <=); i += paso).
 * Se usa aritmetica sin signo para no desbordar con numeros grandes. */
static unsigned long long vueltas(long long ini, long long lim, long long paso, int menor_igual) {
    if (menor_igual ? ini > lim : ini >= lim) return 0;   /* La condicion falla de entrada */
    unsigned long long d = (unsigned long long)lim - (unsigned long long)ini; /* Distancia */
    unsigned long long p = (unsigned long long)paso;      /* Paso como sin signo */
    /* Con <= se incluye el limite (+1). Con < se redondea hacia arriba. */
    return menor_igual ? d / p + 1 : d / p + (d % p != 0);
}

/* Verdadero si algun salto FUERA de [desde, hasta] apunta a la etiqueta.
 * Si es asi, la etiqueta no se puede borrar al quitar el ciclo. */
static int referenciada(const IrPrograma *ir, const char *etq, size_t desde, size_t hasta) {
    for (size_t k = 0; k < ir->cantidad; ++k)                                   /* Toda la IR */
        if ((k < desde || k > hasta) && es_salto(ir->codigo[k].op) && igual(ir->codigo[k].dst, etq)) return 1;
    return 0;
}

/* ===================================================================== */
/* 5. PLAN Y APLICACION                                                   */
/* ===================================================================== */

/* Tres formas de transformar un ciclo:
 *   PLAN_TOTAL          N conocido y pequeno: N copias, sin ciclo.
 *   PLAN_PARCIAL_CONST  N conocido pero grande: ciclo con k copias y las
 *                       N mod k vueltas restantes como copias sueltas.
 *   PLAN_PARCIAL_VAR    N desconocido: ciclo con k copias y luego el
 *                       ciclo original ("residuo") para lo que falte. */
typedef enum { PLAN_TOTAL, PLAN_PARCIAL_CONST, PLAN_PARCIAL_VAR } TipoPlan;
typedef struct {
    TipoPlan tipo;             /* Cual de las tres formas */
    unsigned factor;           /* k (solo en los parciales) */
    unsigned long long n;      /* N, si se conoce */
} Plan;

/* HEURISTICA: decide que hacer con el ciclo. Devuelve 1 y llena *p,
 * o 0 si no conviene desenrollar (*motivo explica por que). */
static int planear(const IrPrograma *ir, const Ciclo *c, const LuOpciones *op, Plan *p, const char **motivo) {
    size_t por_vuelta = c->cuerpo_fin - c->cuerpo_ini + 2; /* Cuerpo + 2 instrucciones del incremento */
    long long ini, lim;                                    /* Valores de inicio y limite */
    /* N se conoce solo si se sabe el valor inicial de i Y el del limite. */
    int constante = valor_entrada(ir, c->cabecera, c->iter, &ini) &&
                    valor_entrada(ir, c->cabecera, c->limite, &lim);
    *p = (Plan){0};                                        /* Limpia el plan */
    if (constante) {
        p->n = vueltas(ini, lim, c->paso, igual(c->relacion, "<=")); /* Calcula N */
        /* Pocas vueltas y codigo resultante pequeno: desenrollado total. */
        if (p->n <= op->max_vueltas_total && p->n * por_vuelta <= op->max_instr_total) {
            p->tipo = PLAN_TOTAL; return 1;
        }
    }
    unsigned k = op->factor;                               /* Factor pedido */
    /* Si k copias del cuerpo serian demasiado codigo, se baja k. */
    while (k >= 2 && (size_t)k * por_vuelta > op->max_instr_parcial) --k;
    if (k < 2) { *motivo = "el cuerpo es demasiado grande para repetirlo"; return 0; }
    /* (k-1)*paso se escribe en la cabecera nueva: no debe desbordar. */
    if (c->paso > LLONG_MAX / (long long)(k - 1)) { *motivo = "el paso es demasiado grande"; return 0; }
    /* N conocido pero menor que k: el ciclo principal nunca entraria. */
    if (constante && p->n < k) {
        *motivo = "tiene menos vueltas que el factor y no cabe en un desenrollado total"; return 0;
    }
    p->factor = k;                                         /* Factor final */
    p->tipo = constante ? PLAN_PARCIAL_CONST : PLAN_PARCIAL_VAR; /* Cual parcial */
    return 1;
}

/* Escribe en out UNA copia del cuerpo seguida del incremento de i.
 * Cada copia usa temporales y etiquetas NUEVOS: si dos copias definieran
 * la misma etiqueta ($L5:) la IR seria invalida. */
static int emitir_copia(Codigo *out, IrPrograma *ir, const Ciclo *c) {
    const IrInstr *v = ir->codigo;          /* IR original */
    Lista viejo = {0}, nuevo = {0};         /* Tabla de renombre: viejo[i] -> nuevo[i] */
    int ok = 1;                             /* Se vuelve 0 si falta memoria */
    /* Paso 1: buscar que nombres define el cuerpo (etiquetas y temporales)
     * y darle a cada uno un nombre nuevo. */
    for (size_t k = c->cuerpo_ini; k < c->cuerpo_fin && ok; ++k) {
        const char *nombre = NULL; char tipo = 0;                    /* Nombre a renombrar */
        if (v[k].op == IR_LABEL) { nombre = v[k].dst; tipo = 'L'; }  /* Etiqueta: $LN nuevo */
        else if (es_temporal(escrito(&v[k]))) { nombre = v[k].dst; tipo = 't'; } /* Temporal: $tN nuevo */
        if (!nombre || lista_buscar(&viejo, nombre) >= 0) continue;  /* Nada o ya renombrado */
        ok = lista_agregar(&viejo, nombre) && lista_agregar_propia(&nuevo, nombre_nuevo(ir, tipo));
    }
    /* Paso 2: copiar cada instruccion cambiando los nombres segun la tabla.
     * Las variables del usuario (suma, i, ...) no estan en la tabla y quedan igual. */
    for (size_t k = c->cuerpo_ini; k < c->cuerpo_fin && ok; ++k) {
        const IrInstr *x = &v[k];
        long i;                                                                 /* Posicion en la tabla */
        const char *d = (i = lista_buscar(&viejo, x->dst)) >= 0 ? nuevo.v[i] : x->dst; /* Destino */
        const char *a = (i = lista_buscar(&viejo, x->a)) >= 0 ? nuevo.v[i] : x->a;     /* Operando a */
        const char *b = (i = lista_buscar(&viejo, x->b)) >= 0 ? nuevo.v[i] : x->b;     /* Operando b */
        poner(out, x->op, d, a, b, x->operador, x->linea);                      /* Instruccion copiada */
    }
    /* Paso 3: el incremento "$tN = i gauss paso ; i = $tN" con un temporal nuevo. */
    char *t = ok ? nombre_nuevo(ir, 't') : NULL;
    if (!t) ok = 0;
    size_t linea = v[c->incremento].linea;                        /* Misma linea que el original */
    poner(out, IR_BIN, t, c->iter, c->paso_txt, "gauss", linea);  /* $tN = i gauss paso */
    poner(out, IR_COPY, c->iter, t, NULL, NULL, linea);           /* i = $tN */
    free(t);                                                      /* poner() hizo su propia copia */
    lista_liberar(&viejo); lista_liberar(&nuevo);                 /* La tabla ya no se usa */
    return ok && !out->error;                                     /* 1 si todo salio bien */
}

/* Escribe la cabecera del ciclo principal de un desenrollado parcial:
 *     $Lm:
 *     $ta = i gauss (k-1)*paso
 *     $tc = $ta < limite
 *     ifFalse $tc goto destino
 * Solo entra si quedan AL MENOS k vueltas (la ultima, i+(k-1)*paso, cumple). */
static int emitir_cabecera(Codigo *out, IrPrograma *ir, const Ciclo *c, unsigned k,
                           const char *etq_m, const char *destino) {
    char avance[32];                                                     /* (k-1)*paso como texto */
    snprintf(avance, sizeof avance, "%lld", c->paso * (long long)(k - 1));
    char *ta = nombre_nuevo(ir, 't'), *tc = nombre_nuevo(ir, 't');      /* Dos temporales nuevos */
    int ok = ta && tc;
    poner(out, IR_LABEL, etq_m, NULL, NULL, NULL, c->linea);             /* $Lm: */
    poner(out, IR_BIN, ta, c->iter, avance, "gauss", c->linea);          /* $ta = i gauss (k-1)*paso */
    poner(out, IR_BIN, tc, ta, c->limite, c->relacion, c->linea);        /* $tc = $ta < limite */
    poner(out, IR_JFALSE, destino, tc, NULL, NULL, c->linea);            /* ifFalse $tc goto destino */
    free(ta); free(tc);                                                  /* poner() ya los copio */
    return ok && !out->error;
}

/* Copia s en el arreglo fijo d (de tamano n), recortando si no cabe. */
static void copiar_texto(char *d, size_t n, const char *s) {
    snprintf(d, n, "%s", s ? s : "");     /* snprintf siempre deja el '\0' */
}

/* Guarda en r->detalle lo que se decidio con un ciclo (para el reporte).
 * c puede estar incompleto si el ciclo no era canonico; p puede ser NULL.
 * Si falta memoria simplemente no se anota (el detalle es opcional). */
static void anotar(LuResumen *r, const Ciclo *c, const char *etq, size_t linea,
                   LuDecision d, const Plan *p, const char *motivo) {
    if (!r) return;                                               /* Nadie pidio detalle */
    LuDetalle *nuevo = realloc(r->detalle, (r->cantidad_detalle + 1) * sizeof *nuevo);
    if (!nuevo) return;                                           /* Sin memoria: se omite */
    r->detalle = nuevo;
    LuDetalle *x = &nuevo[r->cantidad_detalle++];                 /* Elemento nuevo al final */
    *x = (LuDetalle){0};                                          /* Todo en cero */
    copiar_texto(x->etiqueta, sizeof x->etiqueta, etq);           /* $Lx */
    copiar_texto(x->iter, sizeof x->iter, c->iter);               /* i (o vacio) */
    copiar_texto(x->limite, sizeof x->limite, c->limite);         /* limite (o vacio) */
    copiar_texto(x->relacion, sizeof x->relacion, c->relacion);   /* < o <= */
    x->paso = c->paso;
    x->linea = linea;
    x->decision = d;
    x->motivo = motivo;
    if (p) {                                                      /* Solo si hubo plan */
        x->factor = p->factor;
        x->vueltas = p->n;
        x->vueltas_conocidas = p->tipo != PLAN_PARCIAL_VAR;       /* Total y parcial const saben N */
    }
}

/* Arma la IR nueva segun el plan y reemplaza la vieja.
 * `hechos` guarda etiquetas de ciclos ya procesados para no repetirlos.
 * Devuelve 0 si funciono y 2 si falto memoria (la IR queda como estaba). */
static int aplicar(IrPrograma *ir, const Ciclo *c, const Plan *p, Lista *hechos, FILE *log, LuResumen *r) {
    Codigo out = {0};                                                    /* IR nueva */
    int ok = 1;
    /* Si alguien fuera del ciclo salta a $Lx o $Ly, esas etiquetas se conservan. */
    int ref_ini = referenciada(ir, c->etq_ini, c->cabecera, c->salida);
    int ref_fin = referenciada(ir, c->etq_fin, c->cabecera, c->salida);
    char *etq_m = NULL, *etq_f = NULL;                                   /* Etiquetas nuevas */

    copiar_rango(&out, ir, 0, c->cabecera);         /* Todo lo anterior al ciclo, igual */
    if (p->tipo == PLAN_TOTAL) {
        /* TOTAL: N copias seguidas; desaparecen condicion, ifFalse y goto. */
        if (ref_ini) poner(&out, IR_LABEL, c->etq_ini, NULL, NULL, NULL, c->linea);
        for (unsigned long long r = 0; r < p->n && ok; ++r) ok = emitir_copia(&out, ir, c);
        if (ref_fin) poner(&out, IR_LABEL, c->etq_fin, NULL, NULL, NULL, c->linea);
        copiar_rango(&out, ir, c->salida + 1, ir->cantidad); /* Lo que sigue al ciclo */
    } else if (p->tipo == PLAN_PARCIAL_VAR) {
        /* PARCIAL con N desconocido:
         *   $Lm: [cabecera] k copias goto $Lm
         *   ...ciclo original intacto (residuo)... */
        etq_m = nombre_nuevo(ir, 'L');
        ok = etq_m && emitir_cabecera(&out, ir, c, p->factor, etq_m, c->etq_ini); /* Si no hay k vueltas, va al residuo */
        for (unsigned r = 0; r < p->factor && ok; ++r) ok = emitir_copia(&out, ir, c); /* k copias */
        poner(&out, IR_JUMP, etq_m, NULL, NULL, NULL, c->linea);         /* Vuelve al principal */
        copiar_rango(&out, ir, c->cabecera, ir->cantidad);               /* Residuo + resto del programa */
        ok = ok && lista_agregar(hechos, c->etq_ini);                    /* El residuo no se vuelve a desenrollar */
    } else {
        /* PARCIAL con N conocido: se sabe que sobran exactamente N mod k vueltas,
         * asi que van como copias sueltas en vez de un ciclo residuo:
         *   $Lm: [cabecera] k copias goto $Lm
         *   $Lf: (N mod k) copias */
        etq_m = nombre_nuevo(ir, 'L');                                   /* Cabecera del principal */
        etq_f = nombre_nuevo(ir, 'L');                                   /* Salida del principal */
        if (ref_ini) poner(&out, IR_LABEL, c->etq_ini, NULL, NULL, NULL, c->linea);
        ok = etq_m && etq_f && emitir_cabecera(&out, ir, c, p->factor, etq_m, etq_f);
        for (unsigned r = 0; r < p->factor && ok; ++r) ok = emitir_copia(&out, ir, c); /* k copias */
        poner(&out, IR_JUMP, etq_m, NULL, NULL, NULL, c->linea);         /* goto $Lm */
        poner(&out, IR_LABEL, etq_f, NULL, NULL, NULL, c->linea);        /* $Lf: */
        for (unsigned long long r = 0; r < p->n % p->factor && ok; ++r) ok = emitir_copia(&out, ir, c); /* Sobrantes */
        if (ref_fin) poner(&out, IR_LABEL, c->etq_fin, NULL, NULL, NULL, c->linea);
        copiar_rango(&out, ir, c->salida + 1, ir->cantidad);             /* Lo que sigue al ciclo */
    }
    if (ok && etq_m) ok = lista_agregar(hechos, etq_m);   /* El ciclo principal tampoco se repite */
    if (!ok || out.error) {                               /* Algo fallo: se descarta la IR nueva */
        free(etq_m); free(etq_f);
        liberar_instrucciones(out.v, out.n);
        return 2;
    }
    /* El mensaje se escribe ANTES de reemplazar, porque *c apunta a
     * cadenas de la IR vieja que reemplazar() va a liberar. */
    if (log) {
        fprintf(log, "[unroll] ciclo %s (linea %zu), iterador %s, limite %s, paso %lld: ",
                c->etq_ini, c->linea, c->iter, c->limite, c->paso);
        if (p->tipo == PLAN_TOTAL)
            fprintf(log, "%llu vueltas constantes -> desenrollado TOTAL (%llu copias, sin comparacion ni saltos)\n",
                    p->n, p->n);
        else if (p->tipo == PLAN_PARCIAL_VAR)
            fprintf(log, "vueltas desconocidas -> desenrollado PARCIAL factor %u + ciclo residuo %s\n",
                    p->factor, c->etq_ini);
        else
            fprintf(log, "%llu vueltas constantes -> desenrollado PARCIAL factor %u (%llu vueltas del ciclo %s, %llu en linea recta)\n",
                    p->n, p->factor, p->n / p->factor, etq_m, p->n % p->factor);
    }
    /* Detalle para el reporte (tambien antes de reemplazar, por lo mismo). */
    anotar(r, c, c->etq_ini, c->linea,
           p->tipo == PLAN_TOTAL ? LU_TOTAL : p->tipo == PLAN_PARCIAL_CONST ? LU_PARCIAL_CONST : LU_PARCIAL_VAR,
           p, NULL);
    free(etq_m); free(etq_f);                             /* Ya se copiaron dentro de out */
    reemplazar(ir, &out);                                 /* La IR nueva pasa a ser la oficial */
    return 0;
}

/* ===================================================================== */
/* 6. INTERFAZ PUBLICA (declarada en loop_unrolling.h)                    */
/* ===================================================================== */

/* Valores por defecto de la heuristica. */
void lu_opciones_defecto(LuOpciones *op) {
    op->factor = 4;               /* k: copias por vuelta en el parcial */
    op->max_vueltas_total = 16;   /* Total solo si N <= 16 */
    op->max_instr_total = 64;     /* ... y si N*(cuerpo+2) <= 64 */
    op->max_cuerpo = 32;          /* Cuerpos mas grandes no se tocan */
    op->max_instr_parcial = 64;   /* k*(cuerpo+2) <= 64, si no se baja k */
}

/* Punto de entrada: desenrolla todos los ciclos posibles de ir.
 * op puede ser NULL (usa los valores por defecto); res y log son opcionales. */
int lu_desenrollar(IrPrograma *ir, const LuOpciones *opc, LuResumen *res, FILE *log) {
    if (!ir) return 2;                               /* Argumento invalido */
    LuOpciones o;                                    /* Copia local de las opciones */
    if (opc) o = *opc; else lu_opciones_defecto(&o); /* Las del usuario o las por defecto */
    if (o.factor < 2) o.factor = 2;                  /* factor 1 no repetiria nada */
    opc = &o;
    LuResumen r = {0};                               /* Estadisticas */
    r.instr_antes = ir->cantidad;                    /* Tamano inicial */
    Lista hechos = {0};                              /* Etiquetas de ciclos ya procesados */
    int estado = 0, cambio_total = 0;                /* Error / si hubo algun cambio */

    /* Cada pasada busca el PRIMER ciclo que se pueda desenrollar, lo
     * transforma y empieza de nuevo (las posiciones cambiaron). Asi, si un
     * ciclo externo tenia un ciclo interno, despues de desenrollar el
     * interno el externo vuelve a revisarse y puede desenrollarse tambien.
     * El limite de 10000 pasadas es solo una proteccion. */
    for (int pasada = 0; pasada < 10000 && !estado; ++pasada) {
        int cambio = 0;                              /* Se transformo algo en esta pasada? */
        for (size_t h = 0; h < ir->cantidad && !cambio; ++h) {
            /* Solo etiquetas que no se hayan procesado ya. */
            if (ir->codigo[h].op != IR_LABEL || lista_buscar(&hechos, ir->codigo[h].dst) >= 0) continue;
            Ciclo c; Plan p; const char *motivo = NULL;
            if (reconocer(ir, h, &c, &motivo) != 1) continue;       /* No es ciclo canonico */
            if (verificar(ir, &c, opc, &motivo) != 1) continue;     /* No cumple precondiciones */
            if (!planear(ir, &c, opc, &p, &motivo)) continue;       /* La heuristica dice que no */
            if (aplicar(ir, &c, &p, &hechos, log, &r)) { estado = 2; break; } /* Falto memoria */
            if (p.tipo == PLAN_TOTAL) ++r.totales; else ++r.parciales;   /* Cuenta el resultado */
            cambio = cambio_total = 1;                              /* Reinicia la busqueda */
        }
        if (!cambio) break;                          /* Nada mas que hacer */
    }
    /* Pasada final: los ciclos que quedaron sin transformar se cuentan
     * como rechazados y se informa el motivo en el log. */
    for (size_t h = 0; h < ir->cantidad && !estado; ++h) {
        if (ir->codigo[h].op != IR_LABEL || lista_buscar(&hechos, ir->codigo[h].dst) >= 0) continue;
        Ciclo c; Plan p; const char *motivo = "forma no canonica";
        int k = reconocer(ir, h, &c, &motivo);       /* Vuelve a revisar para obtener el motivo */
        if (k == 0) continue;                        /* Esa etiqueta no es un ciclo */
        if (k == 1 && verificar(ir, &c, opc, &motivo) == 1) planear(ir, &c, opc, &p, &motivo);
        ++r.rechazados;
        size_t linea = c.linea ? c.linea : ir->codigo[h].linea;  /* Linea para mostrar */
        if (log) fprintf(log, "[unroll] ciclo %s (linea %zu) no se desenrolla: %s\n",
                         ir->codigo[h].dst, linea, motivo);
        anotar(&r, &c, ir->codigo[h].dst, linea, LU_RECHAZADO, NULL, motivo); /* Para el reporte */
    }
    lista_liberar(&hechos);                          /* Ya no se necesita */
    /* Si se cambio la IR, se recalculan los bloques basicos y el CFG para
     * que las siguientes fases (DCE, reordenamiento, VLIW) los usen. */
    if (cambio_total && ir_bloques_basicos(ir)) estado = 2;
    r.ciclos = r.totales + r.parciales + r.rechazados; /* Total de ciclos vistos */
    r.instr_despues = ir->cantidad;                  /* Tamano final */
    if (res) *res = r;                               /* Devuelve las estadisticas */
    else free(r.detalle);                            /* Nadie lo va a usar */
    return estado;                                   /* 0 = exito */
}

/* Libera el detalle de ciclos y lo deja vacio. */
void lu_resumen_liberar(LuResumen *res) {
    if (!res) return;
    free(res->detalle);                              /* Arreglo de LuDetalle */
    res->detalle = NULL;
    res->cantidad_detalle = 0;
}
