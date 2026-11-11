#include "ir.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* El generador conserva el programa destino, el flujo de errores y el estado.
 * Estado 0: correcto, 1: nodo no soportado, 2: memoria o argumentos. */
typedef struct { IrPrograma *ir; FILE *err; int estado; } Generador;
/* Devuelve el hijo ubicado en la posicion idx del nodo AST.
 * Si el padre no existe o tiene menos hijos, devuelve NULL. */
static const AstNodo *hijo(const AstNodo *n, unsigned idx) {
    const AstNodo *p = n ? n->primer_hijo : NULL;
    while (p && idx--) p = p->siguiente;
    return p;
}
/* Reserva una copia independiente de una cadena.
 * El llamador debe liberar el resultado con free(). */
static char *copia(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    if (n == SIZE_MAX) return NULL;
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}
/* Crea nombres internos para temporales ($tN) y etiquetas ($LN). */
static char *nombre(char tipo, unsigned n) {
    char texto[40];
    snprintf(texto, sizeof texto, "$%c%u", tipo, n);
    return copia(texto);
}
/* Registra solo el primer error y muestra su posicion en el AST.
 * El estado 1 identifica elementos no implementados en esta etapa. */
static void error(Generador *g, const AstNodo *n, const char *motivo) {
    if (g->estado) return;
    g->estado = 1;
    if (g->err) fprintf(g->err, "IR:%zu:%zu: %s (AST %s)\n",
        n ? n->ubicacion.first_line : 0,
        n ? n->ubicacion.first_column : 0,
        motivo, n ? ast_nombre_tipo(n->tipo) : "nulo");
}
/* Duplica todos los campos: una instruccion jamas depende de la vida del AST. */
static int emitir(Generador *g, IrOp op, const char *dst, const char *a,
                  const char *b, const char *operador, const AstNodo *n) {
    if (g->estado) return 0;
    IrPrograma *ir = g->ir;
    /* Aumenta la capacidad del arreglo dinamico cuando se llena.
     * La comprobacion evita desbordamientos al calcular el tamano. */
    if (ir->cantidad == ir->capacidad) {
        if (ir->capacidad > SIZE_MAX / 2 / sizeof *ir->codigo) {g->estado=2;return 0;}
        size_t cap = ir->capacidad ? ir->capacidad*2 : 32;
        IrInstr *p = realloc(ir->codigo, cap*sizeof *p);
        if (!p) {g->estado=2;return 0;}
        ir->codigo=p;ir->capacidad=cap;
    }
    /* Cada campo se duplica para que la IR sobreviva al AST original. */
    IrInstr i={op,copia(dst),copia(a),copia(b),copia(operador),n?n->ubicacion.first_line:0};
    if ((dst&&!i.dst)||(a&&!i.a)||(b&&!i.b)||(operador&&!i.operador)) {
        free(i.dst);free(i.a);free(i.b);free(i.operador);g->estado=2;return 0;
    }
    ir->codigo[ir->cantidad++]=i;
    return 1;
}
static char *expresion(Generador *g, const AstNodo *n);
static char *referencia(Generador *g, const AstNodo *n);
static void inicializar_coleccion(Generador *g, const AstNodo *literal, const char *ref);
static void sentencia(Generador *g, const AstNodo *n);
/* Genera un temporal unico y evita desbordar el contador. */
static char *temporal(Generador *g) {
    if (g->ir->temporales == UINT32_MAX) {g->estado=2;return NULL;}
    char *s=nombre('t', ++g->ir->temporales);
    if (!s) g->estado=2;
    return s;
}
/* Genera una etiqueta unica para saltos y uniones de control. */
static char *etiqueta(Generador *g) {
    if (g->ir->etiquetas == UINT32_MAX) {g->estado=2;return NULL;}
    char *s=nombre('L', ++g->ir->etiquetas);
    if (!s) g->estado=2;
    return s;
}
/* Produce una referencia indexable sin leer todavia el elemento.
 * Se evalua cada indice una unica vez y se conserva el orden del AST. */
static char *referencia(Generador *g, const AstNodo *n) {
    if (!n || g->estado) return NULL;
    if (n->tipo == AST_IDENTIFICADOR && n->texto) {
        char *r = copia(n->texto);
        if (!r) g->estado = 2;
        return r;
    }
    /* Un indice produce una referencia logica; todavia no lee memoria.
     * Una matriz genera referencias anidadas, una por dimension. */
    if (n->tipo == AST_INDICE) {
        char *base = referencia(g, hijo(n,0));
        char *indice = expresion(g, hijo(n,1));
        char *r = temporal(g);
        if (!g->estado) emitir(g,IR_INDEX_REF,r,base,indice,NULL,n);
        free(base);free(indice);
        if (g->estado) {free(r);return NULL;}
        return r;
    }
    error(g,n,"referencia de memoria no soportada");
    return NULL;
}
/* Inicializa recursivamente arreglos y matrices en orden de filas.
 * Cada elemento terminal se almacena por medio de una referencia indexada. */
static void inicializar_coleccion(Generador *g, const AstNodo *literal, const char *ref) {
    if (!literal || literal->tipo != AST_LITERAL_COLECCION) {
        error(g,literal,"se esperaba literal de coleccion");return;
    }
    /* Recorre los elementos en orden. Si hay otra coleccion dentro,
     * desciende recursivamente antes de pasar al siguiente elemento. */
    size_t indice = 0;
    for (const AstNodo *p=literal->primer_hijo;p && !g->estado;p=p->siguiente,++indice) {
        char numero[32];
        snprintf(numero,sizeof numero,"%zu",indice);
        char *destino=temporal(g);
        if (!g->estado) emitir(g,IR_INDEX_REF,destino,ref,numero,NULL,p);
        if (!g->estado) {
            if (p->tipo == AST_LITERAL_COLECCION) {
                inicializar_coleccion(g,p,destino);
            } else {
                char *valor=expresion(g,p);
                if (!g->estado) emitir(g,IR_STORE,destino,valor,NULL,NULL,p);
                free(valor);
            }
        }
        free(destino);
    }
}
/* Reserva la coleccion logica y registra las dimensiones por separado.
 * El numero y orden de dimensiones se obtiene de AST_DIMENSIONES. */
static void declarar_arreglo(Generador *g, const AstNodo *n, const char *nombre,
                           const AstNodo *tipo, const AstNodo *valor) {
    const AstNodo *dims=hijo(tipo,1);
    if (!dims || dims->tipo != AST_DIMENSIONES) {
        error(g,n,"arreglo sin dimensiones");return;
    }
    /* Cuenta las dimensiones declaradas antes de emitir sus longitudes. */
    size_t cuenta=0;
    for(const AstNodo *p=dims->primer_hijo;p;p=p->siguiente) ++cuenta;
    char total[32];snprintf(total,sizeof total,"%zu",cuenta);
    emitir(g,IR_ARRAY_DECL,nombre,total,NULL,NULL,n);
    /* Registra cada tamano de dimension por separado, en orden. */
    size_t k=0;
    for(const AstNodo *p=dims->primer_hijo;p && !g->estado;p=p->siguiente,++k) {
        const AstNodo *tam=hijo(p,0);
        if (!tam) {error(g,p,"dimension vacia en declaracion");break;}
        char *longitud=expresion(g,tam);
        char pos[32];snprintf(pos,sizeof pos,"%zu",k);
        if (!g->estado) emitir(g,IR_ARRAY_DIM,nombre,pos,longitud,NULL,p);
        free(longitud);
    }
    if (valor && !g->estado) inicializar_coleccion(g,valor,nombre);
}
/* Traduce una expresion a un operando. Las operaciones compuestas crean
 * temporales y emiten instrucciones antes de devolver su nombre. */
static char *expresion(Generador *g, const AstNodo *n) {
    if (g->estado) return NULL;
    if (!n) {error(g,n,"expresion incompleta");return NULL;}
    switch (n->tipo) {
    /* Identificadores y literales ya son operandos: no necesitan temporal. */
    case AST_IDENTIFICADOR: case AST_ENTERO: case AST_BOOLEANO:
    case AST_CADENA: case AST_CARACTER:
        if (!n->texto) {error(g,n,"literal sin texto");return NULL;}
        {char *r=copia(n->texto);if(!r)g->estado=2;return r;}
    /* Leer una celda requiere obtener su referencia y emitir IR_LOAD. */
    case AST_INDICE: {
        char *ref=referencia(g,n),*r=temporal(g);
        if (!g->estado) emitir(g,IR_LOAD,r,ref,NULL,NULL,n);
        free(ref);
        if(g->estado){free(r);return NULL;}
        return r;
    }
    /* size(...) consulta el tamano de la coleccion mediante IR_SIZE. */
    case AST_TAMANO: {
        char *coleccion=referencia(g,hijo(n,0)),*r=temporal(g);
        if(!g->estado) emitir(g,IR_SIZE,r,coleccion,NULL,NULL,n);
        free(coleccion);
        if(g->estado){free(r);return NULL;}
        return r;
    }
    /* Las expresiones binarias producen un resultado temporal.
     * Las operaciones logicas requieren un tratamiento especial. */
    case AST_BINARIO: {
        /* Los operadores logicos && y || se bajan con cortocircuito.
         * El resultado es 0/1; no se ejecuta el segundo operando si no procede. */
        int and_op = n->texto && !strcmp(n->texto,"&&");
        int or_op = n->texto && !strcmp(n->texto,"||");
        if (and_op || or_op) {
            char *resultado=temporal(g),*salida=etiqueta(g);
            char *izq=expresion(g,hijo(n,0));
            if (!g->estado) emitir(g,IR_COPY,resultado,and_op?"0":"1",NULL,NULL,n);
            if (!g->estado) emitir(g,and_op?IR_JFALSE:IR_JTRUE,salida,izq,NULL,NULL,n);
            free(izq);
            char *der=expresion(g,hijo(n,1));
            if (!g->estado) emitir(g,IR_UNARY,resultado,der,NULL,"bool",n);
            if (!g->estado) emitir(g,IR_LABEL,salida,NULL,NULL,NULL,n);
            free(der);free(salida);
            if (g->estado) {free(resultado);return NULL;}
            return resultado;
        }
        /* Evalua primero el operando izquierdo y despues el derecho.
         * Emite una unica operacion binaria sobre sus resultados. */
        char *a=expresion(g,hijo(n,0));
        char *b=expresion(g,hijo(n,1)),*r=temporal(g);
        if (!g->estado) emitir(g,IR_BIN,r,a,b,n->texto,n);
        free(a);free(b);
        if (g->estado) {free(r);return NULL;}
        return r;
    }
    /* Una operacion unaria usa un operando y un destino temporal. */
    case AST_UNARIO: {
        char *a=expresion(g,hijo(n,0)),*r=temporal(g);
        if (!g->estado) emitir(g,IR_UNARY,r,a,NULL,n->texto,n);
        free(a);
        if (g->estado) {free(r);return NULL;}
        return r;
    }
    /* Evalua los argumentos en orden y los emite con IR_ARG.
     * Las llamadas mediante alias de modulo aun no tienen enlazado IR. */
    case AST_LLAMADA: {
        const AstNodo *dest=hijo(n,0),*args=hijo(n,1);
        if (!dest || dest->tipo!=AST_IDENTIFICADOR || !dest->texto) {
            error(g,n,"llamada a modulo pendiente de enlazado de IR");return NULL;
        }
        size_t cuenta=0;
        for (const AstNodo *p=args?args->primer_hijo:NULL;p;p=p->siguiente) {
            char *v=expresion(g,p);
            if (!g->estado) emitir(g,IR_ARG,NULL,v,NULL,NULL,p);
            free(v);++cuenta;
            if (g->estado) return NULL;
        }
        char numero[32];snprintf(numero,sizeof numero,"%zu",cuenta);
        char *r=temporal(g);
        if (!g->estado) emitir(g,IR_CALL,r,dest->texto,numero,NULL,n);
        if (g->estado) {free(r);return NULL;}
        return r;
    }
    default: error(g,n,"expresion aun no soportada en IR");return NULL;
    }
}
/* Traduce whether/alif/also como una cadena de pruebas y saltos.
 * Todas las ramas que continuan se reunen en la misma etiqueta final. */
static void condicional(Generador *g, const AstNodo *n) {
    char *fin=etiqueta(g);
    const AstNodo *rama=n;
    /* Cada rama condicional salta a la siguiente si resulta falsa. */
    while (rama && rama->tipo==AST_SI && !g->estado) {
        char *siguiente=etiqueta(g);
        char *cond=expresion(g,hijo(rama,0));
        if (!g->estado) emitir(g,IR_JFALSE,siguiente,cond,NULL,NULL,rama);
        free(cond);
        sentencia(g,hijo(rama,1));
        if (!g->estado) emitir(g,IR_JUMP,fin,NULL,NULL,NULL,rama);
        if (!g->estado) emitir(g,IR_LABEL,siguiente,NULL,NULL,NULL,rama);
        free(siguiente);
        rama=hijo(rama,2);
    }
    if (rama && !g->estado) sentencia(g,rama);
    if (!g->estado) emitir(g,IR_LABEL,fin,NULL,NULL,NULL,n);
    free(fin);
}
/* Recorre las sentencias conservando el orden observable de ejecucion.
 * Las escrituras en memoria se representan como IR_STORE, no como copias. */
static void sentencia(Generador *g, const AstNodo *n) {
    if (g->estado||!n) return;
    switch (n->tipo) {
    /* Los bloques solo agrupan sentencias; mantienen el orden fuente. */
    case AST_BLOQUE: case AST_DECLARACIONES:
        for(const AstNodo *p=n->primer_hijo;p && !g->estado;p=p->siguiente) sentencia(g,p);
        break;
    /* Una declaracion escalar inicializada se transforma en IR_COPY.
     * Los arreglos requieren IR_ARRAY_DECL e IR_ARRAY_DIM. */
    case AST_DECLARACION: case AST_CONSTANTE: {
        const AstNodo *id=hijo(n,0),*valor=hijo(n,2);
        const AstNodo *tipo=hijo(n,1);
        if (!id || !id->texto || !tipo) {
            error(g,n,"declaracion incompleta");break;
        }
        if (tipo->tipo==AST_TIPO_ARREGLO) {
            declarar_arreglo(g,n,id->texto,tipo,valor);
            break;
        }
        if (tipo->tipo!=AST_TIPO) {
            error(g,n,"tipo de declaracion no soportado");break;
        }
        if (valor) {
            char *v=expresion(g,valor);
            if(!g->estado) emitir(g,IR_COPY,id->texto,v,NULL,NULL,n);
            free(v);
        }
        break;
    }
    /* Distingue la asignacion simple de la escritura en memoria: 
     * variable = valor frente a store referencia, valor. */
    case AST_ASIGNACION: {
        const AstNodo *id=hijo(n,0);
        if (!id || (id->tipo!=AST_IDENTIFICADOR && id->tipo!=AST_INDICE)) {
            error(g,n,"destino de asignacion no soportado");break;
        }
        /* Se calcula la direccion antes que la expresion asignada. */
        char *ref=id->tipo==AST_INDICE?referencia(g,id):NULL;
        char *v=expresion(g,hijo(n,1));
        if(!g->estado) {
            if (id->tipo==AST_INDICE) emitir(g,IR_STORE,ref,v,NULL,NULL,n);
            else emitir(g,IR_COPY,id->texto,v,NULL,NULL,n);
        }
        free(ref);free(v);break;
    }
    /* Crea la lista y agrega los valores del inicializador en orden. */
    case AST_LISTA: {
        const AstNodo *id=hijo(n,0),*valor=hijo(n,2);
        if (!id || !id->texto) {error(g,n,"lista sin nombre");break;}
        emitir(g,IR_LIST_DECL,id->texto,NULL,NULL,NULL,n);
        if (valor && !g->estado) {
            if (valor->tipo!=AST_LITERAL_COLECCION) {
                error(g,valor,"inicializador de lista invalido");break;
            }
            for(const AstNodo *e=valor->primer_hijo;e && !g->estado;e=e->siguiente) {
                char *v=expresion(g,e);
                if(!g->estado) emitir(g,IR_LIST_ADD,id->texto,v,NULL,NULL,e);
                free(v);
            }
        }
        break;
    }
    /* add/remove modifican una coleccion y por ello tienen efectos. */
    case AST_AGREGAR: case AST_ELIMINAR: {
        char *dest=referencia(g,hijo(n,0));
        char *v=expresion(g,hijo(n,1));
        if (!g->estado) emitir(g,n->tipo==AST_AGREGAR?IR_LIST_ADD:IR_LIST_REMOVE,dest,v,NULL,NULL,n);
        free(dest);free(v);break;
    }
    /* Delega la cadena de condiciones al generador de saltos. */
    case AST_SI: condicional(g,n);break;
    /* whale: etiqueta de entrada, evaluacion, cuerpo y salto de retorno.
     * La etiqueta de salida se alcanza cuando la condicion es falsa. */
    case AST_MIENTRAS: {
        char *inicio=etiqueta(g),*fin=etiqueta(g);
        if(!g->estado) emitir(g,IR_LABEL,inicio,NULL,NULL,NULL,n);
        char *cond=expresion(g,hijo(n,0));
        if(!g->estado) emitir(g,IR_JFALSE,fin,cond,NULL,NULL,n);
        free(cond);
        sentencia(g,hijo(n,1));
        if(!g->estado) emitir(g,IR_JUMP,inicio,NULL,NULL,NULL,n);
        if(!g->estado) emitir(g,IR_LABEL,fin,NULL,NULL,NULL,n);
        free(inicio);free(fin);break;
    }
    case AST_RECORRIDO: {
        /* Orden AST: id, inicio, limite (exclusivo), paso, cuerpo.
         * Por ahora solo pasos enteros literales positivos: los pasos dinamicos
         * o negativos requieren decidir el sentido y proteger paso cero. */
        const AstNodo *id=hijo(n,0),*paso=hijo(n,3);
        if (!id || id->tipo!=AST_IDENTIFICADOR || !paso ||
            paso->tipo!=AST_ENTERO || !paso->texto || paso->texto[0]=='-' ||
            strtoul(paso->texto,NULL,10)==0) {
            error(g,n,"cycle requiere paso constante positivo en esta etapa");break;
        }
        /* Inicializa el iterador antes de comprobar el limite exclusivo. */
        char *desde=expresion(g,hijo(n,1));
        if (!g->estado) emitir(g,IR_COPY,id->texto,desde,NULL,NULL,n);
        free(desde);
        char *inicio=etiqueta(g),*fin=etiqueta(g);
        if (!g->estado) emitir(g,IR_LABEL,inicio,NULL,NULL,NULL,n);
        char *limite=expresion(g,hijo(n,2)),*cond=temporal(g);
        if (!g->estado) emitir(g,IR_BIN,cond,id->texto,limite,"<",n);
        if (!g->estado) emitir(g,IR_JFALSE,fin,cond,NULL,NULL,n);
        free(limite);free(cond);
        sentencia(g,hijo(n,4));
        /* Incrementa el iterador solo despues de ejecutar el cuerpo. */
        char *suma=temporal(g);
        if (!g->estado) emitir(g,IR_BIN,suma,id->texto,paso->texto,"gauss",n);
        if (!g->estado) emitir(g,IR_COPY,id->texto,suma,NULL,NULL,n);
        if (!g->estado) emitir(g,IR_JUMP,inicio,NULL,NULL,NULL,n);
        if (!g->estado) emitir(g,IR_LABEL,fin,NULL,NULL,NULL,n);
        free(suma);free(inicio);free(fin);break;
    }
    /* Emite retorno con un valor opcional. */
    case AST_RETORNO: {
        char *valor=hijo(n,0)?expresion(g,hijo(n,0)):NULL;
        if(!g->estado) emitir(g,IR_RETURN,NULL,valor,NULL,NULL,n);
        free(valor);break;
    }
    /* Una llamada como sentencia conserva su efecto aunque se descarte
     * el valor temporal que la representa. */
    case AST_LLAMADA: {
        char *ignorar=expresion(g,n);free(ignorar);break;
    }
    default: error(g,n,"sentencia aun no soportada en IR");break;
    }
}
/* Punto de entrada del generador: valida el AST, recorre funciones,
 * construye la secuencia IR y calcula los bloques basicos.
 * Si algo falla, libera la salida parcial antes de devolver el error. */
int ir_desde_ast(const Ast *ast, IrPrograma *ir, FILE *diagnosticos) {
    if (!ast||!ir||!ast->raiz||ast->raiz->tipo!=AST_PROGRAMA) return 2;
    /* contrato: el llamador entrega IrPrograma inicializado a cero */
    if (ir->codigo||ir->bloques||ir->cantidad||ir->cantidad_bloques) return 2;
    Generador g={ir,diagnosticos,0};
    /* Los imports ya se revisaron semanticamente, pero el enlazado de
     * codigo importado sigue pendiente. Aqui solo emitimos funciones locales. */
    for(const AstNodo *p=ast->raiz->primer_hijo;p&&!g.estado;p=p->siguiente) {
        if (p->tipo==AST_IMPORTACION) continue; /* validada antes por semantica */
        if(p->tipo!=AST_FUNCION) {error(&g,p,"declaracion global aun no soportada");break;}
        const AstNodo *id=hijo(p,0),*params=hijo(p,2),*cuerpo=hijo(p,3);
        if (!id||!id->texto) {error(&g,p,"funcion sin nombre");break;}
        emitir(&g,IR_FUNC,id->texto,NULL,NULL,NULL,p);
        /* Declara los parametros en el mismo orden del AST. */
        for(const AstNodo *par=params?params->primer_hijo:NULL;par&&!g.estado;par=par->siguiente) {
            const AstNodo *nombre_par=hijo(par,0);
            if(!nombre_par||!nombre_par->texto) {error(&g,par,"parametro invalido");break;}
            emitir(&g,IR_PARAM_DECL,nombre_par->texto,NULL,NULL,NULL,par);
        }
        sentencia(&g,cuerpo);
        if(!g.estado) emitir(&g,IR_END_FUNC,id->texto,NULL,NULL,NULL,p);
    }
    if(g.estado) {ir_liberar(ir);return g.estado;}
    int estado=ir_bloques_basicos(ir);
    if(estado) ir_liberar(ir);
    return estado;
}
/* Libera cadenas, instrucciones y aristas del CFG; deja la salida vacia. */
void ir_liberar(IrPrograma *ir) {
    if(!ir) return;
    for(size_t k=0;k<ir->cantidad;++k) {
        free(ir->codigo[k].dst);free(ir->codigo[k].a);
        free(ir->codigo[k].b);free(ir->codigo[k].operador);
    }
    free(ir->codigo);
    for(size_t k=0;k<ir->cantidad_bloques;++k) free(ir->bloques[k].sucesores);
    free(ir->bloques);
    *ir=(IrPrograma){0};
}
/* Agrega una arista sin duplicados. Un bloque admite dos sucesores
 * porque un salto condicional tiene camino verdadero y falso. */
static int sucesor(IrBloque *b,size_t destino) {
    for(size_t k=0;k<b->cantidad_sucesores;++k) if(b->sucesores[k]==destino)return 0;
    if(b->cantidad_sucesores>=2)return 2;
    size_t *p=realloc(b->sucesores,(b->cantidad_sucesores+1)*sizeof *p);
    if(!p)return 2;
    b->sucesores=p;b->sucesores[b->cantidad_sucesores++]=destino;return 0;
}
/* Encuentra lideres de bloque y enlaces de control entre instrucciones.
 * Las referencias de memoria no terminan un bloque basico. */
int ir_bloques_basicos(IrPrograma *ir) {
    if(!ir||ir->bloques)return 2;
    size_t n=ir->cantidad;
    if(!n)return 0;
    unsigned char *lider=calloc(n,1);
    if(!lider)return 2;
    /* Un bloque comienza al inicio, en una etiqueta o funcion, y
     * despues de cualquier instruccion que termine el flujo normal. */
    lider[0]=1;
    for(size_t i=0;i<n;++i) {
        IrOp op=ir->codigo[i].op;
        if(op==IR_LABEL||op==IR_FUNC) lider[i]=1;
        if((op==IR_JUMP||op==IR_JFALSE||op==IR_JTRUE||op==IR_RETURN||op==IR_END_FUNC) && i+1<n) lider[i+1]=1;
    }
    /* Cada lider delimita un nuevo intervalo de instrucciones. */
    size_t total=0;for(size_t i=0;i<n;++i)total+=lider[i]!=0;
    ir->bloques=calloc(total,sizeof *ir->bloques);
    if(!ir->bloques){free(lider);return 2;}
    ir->cantidad_bloques=total;
    size_t idx=0;
    for(size_t i=0;i<n;++i) if(lider[i]) {
        if(idx) ir->bloques[idx-1].fin=i;
        ir->bloques[idx++].inicio=i;
    }
    ir->bloques[total-1].fin=n;
    free(lider);
    /* Recorre cada bloque y conecta su salida con otros bloques. */
    for(size_t j=0;j<total;++j) {
        IrBloque *b=&ir->bloques[j];
        const IrInstr *end=&ir->codigo[b->fin-1];
        if(end->op==IR_JUMP||end->op==IR_JFALSE||end->op==IR_JTRUE) {
            size_t target=total;
            for(size_t q=0;q<total;++q) {
                const IrInstr *primero=&ir->codigo[ir->bloques[q].inicio];
                if(primero->op==IR_LABEL&&primero->dst&&end->dst&&!strcmp(primero->dst,end->dst)) {target=q;break;}
            }
            if(target==total||sucesor(b,target))return 2;
        }
        if(end->op!=IR_JUMP&&end->op!=IR_RETURN&&end->op!=IR_END_FUNC && j+1<total) {
            /* Los bloques nunca enlazan implicitamente funciones distintas. */
            if(ir->codigo[ir->bloques[j+1].inicio].op!=IR_FUNC && sucesor(b,j+1))return 2;
        }
    }
    return 0;
}
/* Imprime bloques, sucesores e instrucciones en formato legible.
 * La funcion no ejecuta el codigo intermedio ni genera instrucciones ISA. */
int ir_imprimir(const IrPrograma *ir, FILE *f) {
    if(!ir||!f)return 2;
    for(size_t j=0;j<ir->cantidad_bloques;++j) {
        const IrBloque *bloque=&ir->bloques[j];
        fprintf(f,"\n[B%zu]",j);
        if(bloque->cantidad_sucesores) {
            fputs(" ->",f);
            for(size_t k=0;k<bloque->cantidad_sucesores;++k)fprintf(f," B%zu",bloque->sucesores[k]);
        }
        fputc('\n',f);
        /* Formatea cada operacion segun sus operandos y su semantica. */
        for(size_t i=bloque->inicio;i<bloque->fin;++i) {
            const IrInstr *x=&ir->codigo[i];
            fprintf(f,"%04zu | ",i);
            switch(x->op) {
            case IR_FUNC:fprintf(f,"func %s",x->dst);break;
            case IR_END_FUNC:fprintf(f,"endfunc %s",x->dst);break;
            case IR_PARAM_DECL:fprintf(f,"param_decl %s",x->dst);break;
            case IR_COPY:fprintf(f,"%s = %s",x->dst,x->a);break;
            case IR_BIN:fprintf(f,"%s = %s %s %s",x->dst,x->a,x->operador,x->b);break;
            case IR_UNARY:fprintf(f,"%s = %s %s",x->dst,x->operador,x->a);break;
            case IR_LABEL:fprintf(f,"%s:",x->dst);break;
            case IR_JUMP:fprintf(f,"goto %s",x->dst);break;
            case IR_JFALSE:fprintf(f,"ifFalse %s goto %s",x->a,x->dst);break;
            case IR_JTRUE:fprintf(f,"ifTrue %s goto %s",x->a,x->dst);break;
            case IR_ARG:fprintf(f,"arg %s",x->a);break;
            case IR_CALL:fprintf(f,"%s = call %s, %s",x->dst,x->a,x->b);break;
            case IR_RETURN:if(x->a)fprintf(f,"return %s",x->a);else fputs("return",f);break;
            case IR_ARRAY_DECL:fprintf(f,"array %s, dimensiones %s",x->dst,x->a);break;
            case IR_ARRAY_DIM:fprintf(f,"dim %s[%s] = %s",x->dst,x->a,x->b);break;
            case IR_INDEX_REF:fprintf(f,"%s = ref %s[%s]",x->dst,x->a,x->b);break;
            case IR_LOAD:fprintf(f,"%s = load %s",x->dst,x->a);break;
            case IR_STORE:fprintf(f,"store %s, %s",x->dst,x->a);break;
            case IR_LIST_DECL:fprintf(f,"list %s",x->dst);break;
            case IR_LIST_ADD:fprintf(f,"add %s, %s",x->dst,x->a);break;
            case IR_LIST_REMOVE:fprintf(f,"remove %s, %s",x->dst,x->a);break;
            case IR_SIZE:fprintf(f,"%s = size %s",x->dst,x->a);break;
            }
            fprintf(f," ; linea %zu\n",x->linea);
        }
    }
    return ferror(f)?2:0;
}
