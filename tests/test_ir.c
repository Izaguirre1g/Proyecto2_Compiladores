#include "ir.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Ubicacion comun para los nodos fabricados por estas pruebas unitarias. */
static AstUbicacion loc={1,1,1,1};
/* Construye un nodo AST y detiene la prueba si no se pudo reservar. */
static AstNodo *n(Ast *ast, AstTipo t, const char *texto) {
    AstNodo *r=ast_crear(ast,t,texto,loc);assert(r);return r;
}
/* Agrega un hijo conservando el orden esperado por el generador IR. */
static void add(AstNodo *p,AstNodo *c){ast_agregar_hijo(p,c);}
/* Comprueba un ciclo whale con inicializacion, condicion, incremento
 * y salto de retorno al encabezado del ciclo. */
static void prueba_factorial(void) {
    Ast ast={0};IrPrograma ir={0};
    /* Construimos el AST manualmente para aislar la IR del parser. */
    AstNodo *prog=n(&ast,AST_PROGRAMA,NULL);ast.raiz=prog;
    AstNodo *fn=n(&ast,AST_FUNCION,NULL);add(prog,fn);
    add(fn,n(&ast,AST_IDENTIFICADOR,"main"));add(fn,n(&ast,AST_TIPO,"declare_infinite_void"));
    add(fn,n(&ast,AST_PARAMETROS,NULL));
    AstNodo *bloque=n(&ast,AST_BLOQUE,NULL);add(fn,bloque);
    AstNodo *d=n(&ast,AST_DECLARACION,NULL);add(bloque,d);
    add(d,n(&ast,AST_IDENTIFICADOR,"x"));add(d,n(&ast,AST_TIPO,"declare_int"));add(d,n(&ast,AST_ENTERO,"0"));
    /* El ciclo debe producir al menos un bloque de condicion y otro cuerpo. */
    AstNodo *wh=n(&ast,AST_MIENTRAS,NULL);add(bloque,wh);
    AstNodo *cond=n(&ast,AST_BINARIO,"<");add(wh,cond);
    add(cond,n(&ast,AST_IDENTIFICADOR,"x"));add(cond,n(&ast,AST_ENTERO,"5"));
    AstNodo *wb=n(&ast,AST_BLOQUE,NULL);add(wh,wb);
    AstNodo *as=n(&ast,AST_ASIGNACION,NULL);add(wb,as);
    add(as,n(&ast,AST_IDENTIFICADOR,"x"));
    AstNodo *suma=n(&ast,AST_BINARIO,"gauss");add(as,suma);
    add(suma,n(&ast,AST_IDENTIFICADOR,"x"));add(suma,n(&ast,AST_ENTERO,"1"));
    /* La traduccion debe terminar sin error antes de revisar la salida. */
    assert(ir_desde_ast(&ast,&ir,stderr)==0);
    /* El ejemplo tiene una secuencia esperada de diez instrucciones. */
    assert(ir.cantidad==10); /* func, copy, label, compare, jfalse, add, copy, jump, label, endfunc */
    /* El ciclo requiere separar condicion, cuerpo y salida del CFG. */
    assert(ir.cantidad_bloques>=3);
    assert(ir.codigo[3].op==IR_BIN && !strcmp(ir.codigo[3].operador,"<"));
    assert(ir.codigo[4].op==IR_JFALSE);
    assert(ir.codigo[7].op==IR_JUMP);
    assert(ir_imprimir(&ir,stdout)==0);
    ir_liberar(&ir);ast_liberar(&ast);
}
/* Asegura que un nodo incompleto no produzca una IR parcial valida.
 * La funcion debe reportar el rechazo y liberar lo construido. */
static void prueba_no_soportada(void) {
    Ast ast={0};IrPrograma ir={0};
    AstNodo *p=n(&ast,AST_PROGRAMA,NULL);ast.raiz=p;
    AstNodo *f=n(&ast,AST_FUNCION,NULL);add(p,f);
    add(f,n(&ast,AST_IDENTIFICADOR,"main"));add(f,n(&ast,AST_TIPO,"declare_infinite_void"));
    add(f,n(&ast,AST_PARAMETROS,NULL));
    AstNodo *b=n(&ast,AST_BLOQUE,NULL);add(f,b);add(b,n(&ast,AST_RECORRIDO,NULL));
    assert(ir_desde_ast(&ast,&ir,NULL)==1);
    assert(!ir.codigo&&!ir.bloques);
    ast_liberar(&ast);
}
/* Crea el esqueleto minimo de un programa BAL con main y su cuerpo.
 * Evita repetir la misma estructura en cada prueba. */
static AstNodo *programa_main(Ast *a, AstNodo **cuerpo) {
    AstNodo *p=n(a,AST_PROGRAMA,NULL);a->raiz=p;
    AstNodo *f=n(a,AST_FUNCION,NULL);add(p,f);
    add(f,n(a,AST_IDENTIFICADOR,"main"));add(f,n(a,AST_TIPO,"declare_infinite_void"));
    add(f,n(a,AST_PARAMETROS,NULL));
    *cuerpo=n(a,AST_BLOQUE,NULL);add(f,*cuerpo);return p;
}
/* Verifica el cortocircuito de && y los saltos de whether/also. */
static void prueba_logica_y_alternativas(void) {
    Ast a={0};IrPrograma ir={0};AstNodo *body=NULL;
    (void)programa_main(&a,&body);
    AstNodo *si=n(&a,AST_SI,"whether");add(body,si);
    AstNodo *and=n(&a,AST_BINARIO,"&&");add(si,and);
    add(and,n(&a,AST_BOOLEANO,"declare_true"));add(and,n(&a,AST_BOOLEANO,"declare_false"));
    AstNodo *then=n(&a,AST_BLOQUE,NULL);add(si,then);
    AstNodo *as=n(&a,AST_ASIGNACION,NULL);add(then,as);
    add(as,n(&a,AST_IDENTIFICADOR,"x"));add(as,n(&a,AST_ENTERO,"1"));
    AstNodo *els=n(&a,AST_BLOQUE,NULL);add(si,els);
    assert(ir_desde_ast(&a,&ir,stderr)==0);
    /* Contamos las etiquetas y saltos para comprobar el control de flujo. */
    int saltos=0, labels=0;
    for(size_t i=0;i<ir.cantidad;++i) {
        if(ir.codigo[i].op==IR_JFALSE)++saltos;
        if(ir.codigo[i].op==IR_LABEL)++labels;
    }
    assert(saltos>=2&&labels>=2);
    ir_liberar(&ir);ast_liberar(&a);
}
/* Comprueba la traduccion de cycle con limite exclusivo y paso 2. */
static void prueba_cycle(void) {
    Ast a={0};IrPrograma ir={0};AstNodo *body=NULL;
    (void)programa_main(&a,&body);
    AstNodo *cycle=n(&a,AST_RECORRIDO,NULL);add(body,cycle);
    add(cycle,n(&a,AST_IDENTIFICADOR,"i"));
    add(cycle,n(&a,AST_ENTERO,"0"));
    add(cycle,n(&a,AST_ENTERO,"10"));
    add(cycle,n(&a,AST_ENTERO,"2"));
    add(cycle,n(&a,AST_BLOQUE,NULL));
    assert(ir_desde_ast(&a,&ir,stderr)==0);
    assert(ir.cantidad_bloques>=3);
    assert(ir.codigo[ir.cantidad-2].op==IR_LABEL);
    ir_liberar(&ir);ast_liberar(&a);
}

/* Verifica que la IR represente dimensiones, matrices y accesos a memoria. */
static void prueba_arreglos_y_matrices(void) {
    Ast a={0}; IrPrograma ir={0}; AstNodo *body=NULL;
    (void)programa_main(&a,&body);
    AstNodo *d=n(&a,AST_DECLARACION,NULL);add(body,d);
    add(d,n(&a,AST_IDENTIFICADOR,"matriz"));
    /* Declaramos una matriz de dos dimensiones: 2 filas y 3 columnas. */
    AstNodo *tipo=n(&a,AST_TIPO_ARREGLO,NULL);add(d,tipo);
    add(tipo,n(&a,AST_TIPO,"declare_int"));
    AstNodo *dims=n(&a,AST_DIMENSIONES,NULL);add(tipo,dims);
    for(int k=0;k<2;++k) {
        AstNodo *dim=n(&a,AST_DIMENSION,NULL);add(dims,dim);
        add(dim,n(&a,AST_ENTERO,k==0?"2":"3"));
    }
    /* Construimos el inicializador [[1,2,3],[4,5,6]] en el AST. */
    AstNodo *literal=n(&a,AST_LITERAL_COLECCION,NULL);add(d,literal);
    for(int i=0;i<2;++i) {
        AstNodo *fila=n(&a,AST_LITERAL_COLECCION,NULL);add(literal,fila);
        for(int j=0;j<3;++j) {
            char numero[10];snprintf(numero,sizeof numero,"%d",i*3+j+1);
            add(fila,n(&a,AST_ENTERO,numero));
        }
    }
    /* La asignacion matriz[1][2] = 7 debe transformarse en IR_STORE. */
    AstNodo *as=n(&a,AST_ASIGNACION,NULL);add(body,as);
    AstNodo *indice1=n(&a,AST_INDICE,NULL);add(as,indice1);
    AstNodo *indice2=n(&a,AST_INDICE,NULL);add(indice1,indice2);
    add(indice2,n(&a,AST_IDENTIFICADOR,"matriz"));
    add(indice2,n(&a,AST_ENTERO,"1"));
    add(indice1,n(&a,AST_ENTERO,"2"));
    add(as,n(&a,AST_ENTERO,"7"));
    /* La lectura posterior de matriz[0] debe generar IR_LOAD. */
    AstNodo *decl=n(&a,AST_DECLARACION,NULL);add(body,decl);
    add(decl,n(&a,AST_IDENTIFICADOR,"x"));
    add(decl,n(&a,AST_TIPO,"declare_int"));
    AstNodo *read=n(&a,AST_INDICE,NULL);add(decl,read);
    add(read,n(&a,AST_IDENTIFICADOR,"matriz"));
    add(read,n(&a,AST_ENTERO,"0"));
    assert(ir_desde_ast(&a,&ir,stderr)==0);
    /* Verificamos cantidades concretas de efectos y accesos a memoria. */
    size_t numdim=0, stores=0, loads=0, refs=0;
    for(size_t i=0;i<ir.cantidad;++i) {
        numdim+=ir.codigo[i].op==IR_ARRAY_DIM;
        stores+=ir.codigo[i].op==IR_STORE;
        loads+=ir.codigo[i].op==IR_LOAD;
        refs+=ir.codigo[i].op==IR_INDEX_REF;
    }
    assert(numdim==2 && stores==7 && loads==1 && refs>=9);
    assert(ir.codigo[1].op==IR_ARRAY_DECL);
    ir_liberar(&ir);ast_liberar(&a);
}
/* Las llamadas a add y remove son efectos visibles y no operaciones puras. */
/* Comprueba creacion e inicializacion de listas, add, remove y size. */
static void prueba_listas(void) {
    Ast a={0};IrPrograma ir={0};AstNodo *body=NULL;
    (void)programa_main(&a,&body);
    AstNodo *list=n(&a,AST_LISTA,NULL);add(body,list);
    add(list,n(&a,AST_IDENTIFICADOR,"valores"));
    add(list,n(&a,AST_TIPO,"declare_int"));
    AstNodo *init=n(&a,AST_LITERAL_COLECCION,NULL);add(list,init);
    add(init,n(&a,AST_ENTERO,"3"));add(init,n(&a,AST_ENTERO,"4"));
    /* Agregar un elemento debe emitir una instruccion con efectos. */
    AstNodo *add_op=n(&a,AST_AGREGAR,NULL);add(body,add_op);
    add(add_op,n(&a,AST_IDENTIFICADOR,"valores"));add(add_op,n(&a,AST_ENTERO,"5"));
    /* Eliminar un elemento tambien modifica la lista. */
    AstNodo *remove_op=n(&a,AST_ELIMINAR,NULL);add(body,remove_op);
    add(remove_op,n(&a,AST_IDENTIFICADOR,"valores"));add(remove_op,n(&a,AST_ENTERO,"0"));
    AstNodo *decl=n(&a,AST_DECLARACION,NULL);add(body,decl);
    add(decl,n(&a,AST_IDENTIFICADOR,"cantidad"));add(decl,n(&a,AST_TIPO,"declare_int"));
    /* size debe producir un temporal que se asigna a cantidad. */
    AstNodo *size=n(&a,AST_TAMANO,NULL);add(decl,size);
    add(size,n(&a,AST_IDENTIFICADOR,"valores"));
    assert(ir_desde_ast(&a,&ir,stderr)==0);
    /* El inicializador aporta dos add y la sentencia aporta el tercero. */
    size_t nadd=0,nremove=0,nsize=0;
    for(size_t i=0;i<ir.cantidad;++i) {
        nadd+=ir.codigo[i].op==IR_LIST_ADD;
        nremove+=ir.codigo[i].op==IR_LIST_REMOVE;
        nsize+=ir.codigo[i].op==IR_SIZE;
    }
    assert(nadd==3 && nremove==1 && nsize==1);
    ir_liberar(&ir);ast_liberar(&a);
}

/* Ejecuta todas las pruebas; cualquier assert fallido detiene el programa.
 * El mensaje OK solo se imprime si todos los casos fueron exitosos. */
int main(void){prueba_factorial();prueba_no_soportada();prueba_logica_y_alternativas();prueba_cycle();prueba_arreglos_y_matrices();prueba_listas();puts("OK: IR factorial, CFG, matrices, listas y rechazo explicito");return 0;}
