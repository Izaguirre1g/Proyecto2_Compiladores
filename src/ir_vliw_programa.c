/* Programa BAL desde IR optimizada: usa frontend, símbolos/marcos, pasadas,
 * spilling, CB/S2 y cadena física existentes. No interpreta BAL ni recompila
 * el AST original después de optimizar. Toda construcción es privada hasta
 * estabilizar las reubicaciones. Los símbolos del AST/tabla son préstamos;
 * operaciones, IR intermedias y código final tienen propiedad explícita.
 * Límites deliberados: una unidad, 64 funciones, 4092 operaciones físicas,
 * marcos con spills direccionables y tipos escalares/arreglos estáticos.
 * No hay HALT: main retorna al bucle de arranque mediante la ABI existente. */
#define _POSIX_C_SOURCE 200809L
#include "ir_vliw_programa.h"
#include "generacion_adaptador.h"
#include "generacion_basica.h"
#include "funciones_marco.h"
#include "ir_spilling_memoria.h"
#include "ir_registros_fisicos.h"
#include "dce.h"
#include "ir_renombramiento.h"
#include "loop_unrolling.h"
#include "reordenamiento.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

enum { MAX_OP=4092, MAX_FUN=64, AUX_A=28, AUX_B=29 };
#define VAC VLIW_VACIO

typedef struct { char nombre[96]; size_t posicion, funcion; } Simbolo;
typedef struct { size_t primera, simbolo; } Reubicacion;
typedef struct {
    const char *nombre;
    IrSpillingBackend backend;
    size_t inicio, fin, argumentos, pendientes;
    size_t guardados, area_args;
} Funcion;
typedef struct {
    const SimTabla *tabla;
    const SimSimbolo **identidades;
    size_t cantidad_identidades, registros;
    Funcion funciones[MAX_FUN]; size_t nf, actual;
    VliwCfgOperacion operaciones[MAX_OP];
    char textos[MAX_OP][160]; const char *ptrs[MAX_OP]; unsigned efectos[MAX_OP];
    size_t origen[MAX_OP];
    Simbolo simbolos[MAX_OP]; size_t ns;
    VliwCfgSimbolo simbolos_cfg[MAX_OP];
    VliwCfgTransferencia transferencias[MAX_OP]; size_t nt;
    size_t destinos[MAX_OP];
    size_t posibles[MAX_OP];
    Reubicacion reubicaciones[MAX_OP]; size_t nr;
    VliwCfgRegion regiones[MAX_OP];size_t nreg;
    VliwCfgFuncion funciones_cfg[MAX_FUN];
    size_t cantidad, linea, indice_ir;
    FILE *diag;
} Productivo;

/* Un solo diagnóstico causal conserva la posición IR de la expansión. */
static int fallo(Productivo *p,const char *mensaje)
{
    if(p->diag) fprintf(p->diag,"O2 IR[%zu] línea %zu: %s\n",p->indice_ir,p->linea,mensaje);
    return 0;
}
/* Los campos IR se duplican para separar la vida útil de cada pasada. */
static char *duplicar(const char *s) { return s ? strdup(s) : NULL; }

/* La identidad nace del símbolo semántico, no de un offset aproximado ni
 * del número de línea. El nombre interno sobrevive a copias de las pasadas. */
static const SimSimbolo *identidad(const Productivo *p,const char *s)
{
    size_t n;char extra;
    if(!s || sscanf(s,"__bal_v%zu%c",&n,&extra)!=1 || n>=p->cantidad_identidades) return NULL;
    return p->identidades[n];
}
/* La enumeración es interna: un ID semántico nunca se usa como offset. */
static size_t indice_identidad(const Productivo *p,const SimSimbolo *s)
{
    for(size_t i=0;i<p->cantidad_identidades;++i) if(p->identidades[i]==s) return i;
    return VAC;
}
/* Clona solo para vincular nombres antes de llamar al generador IR real.
 * La búsqueda lexical es la del backend existente, incluidos shadowing y
 * visibilidad dentro de una misma línea. El AST prestado nunca se modifica. */
static AstNodo *vincular(Productivo *p,Ast *copia,const AstNodo *n,
                         const char *funcion,unsigned profundidad)
{
    if(!n || profundidad>256) return NULL;
    const char *texto=n->texto;char alias[48];
    if(n->tipo==AST_FUNCION) funcion=n->primer_hijo?n->primer_hijo->texto:NULL;
    bool nombre_funcion=n->tipo==AST_IDENTIFICADOR && n->padre &&
        ((n->padre->tipo==AST_FUNCION || n->padre->tipo==AST_LLAMADA) && n->padre->primer_hijo==n);
    if(n->tipo==AST_IMPORTACION) {fallo(p,"módulos pendientes de enlace IR; sin fallback");return NULL;}
    if(n->tipo==AST_IDENTIFICADOR && !nombre_funcion) {
        const SimSimbolo *s=NULL;
        for(size_t i=0;i<p->cantidad_identidades;++i)
            if(p->identidades[i]->declaracion==n) {s=p->identidades[i];break;}
        if(!s) s=gd_resolver_simbolo(p->tabla,funcion,n);
        size_t id=indice_identidad(p,s);
        if(!s || id==VAC || s->clase==SIM_FUNCION) {fallo(p,"identificador sin vínculo semántico inequívoco");return NULL;}
        snprintf(alias,sizeof alias,"__bal_v%zu",id);texto=alias;
    }
    AstNodo *r=ast_crear(copia,n->tipo,texto,n->ubicacion);if(!r)return NULL;
    for(const AstNodo *h=n->primer_hijo;h;h=h->siguiente) {
        AstNodo *x=vincular(p,copia,h,funcion,profundidad+1);if(!x)return NULL;ast_agregar_hijo(r,x);
    }
    return r;
}
/* Solo int32 locales sin alias pueden cambiar a versiones virtuales. Los
 * parámetros, globales y bytes mantienen sus accesos y conversiones físicas. */
static int renombrable(const char *nombre,void *ctx)
{
    const SimSimbolo *s=identidad(ctx,nombre);
    return s && s->clase==SIM_VARIABLE && !s->es_parametro &&
           s->tipo==SIM_TIPO_INT && !s->dimensiones && s->almacen==SIM_ALMACEN_LOCAL;
}
/* Duplica IR para que la evidencia anterior permanezca inmutable mientras
 * las pasadas in-place reconstruyen sus CFG. Libera parciales en el caller. */
static int copiar_ir(const IrPrograma *a,IrPrograma *b)
{
    b->codigo=calloc(a->cantidad,sizeof *b->codigo);if(a->cantidad && !b->codigo)return 0;
    b->cantidad=b->capacidad=a->cantidad;b->temporales=a->temporales;b->etiquetas=a->etiquetas;
    for(size_t i=0;i<a->cantidad;++i) {
        const IrInstr *s=&a->codigo[i];IrInstr *d=&b->codigo[i];
        *d=(IrInstr){s->op,duplicar(s->dst),duplicar(s->a),duplicar(s->b),duplicar(s->operador),s->linea};
        if((s->dst&&!d->dst)||(s->a&&!d->a)||(s->b&&!d->b)||(s->operador&&!d->operador))return 0;
    }
    return ir_bloques_basicos(b)==0;
}
/* Solo se emiten funciones alcanzables desde el arranque. Una función sin
 * llamadores no tiene destinos de retorno acreditables en el CFG físico;
 * quitarla evita inventar continuaciones. El grafo usa identidades IR de
 * llamadas directas, nunca una coincidencia en el ensamblador final. */
static int podar_funciones(Productivo *p,IrPrograma *ir)
{
    const char *nombres[MAX_FUN];bool viva[MAX_FUN]={false};size_t nf=0;
    for(size_t i=0;i<ir->cantidad;++i)if(ir->codigo[i].op==IR_FUNC) {
        if(nf==MAX_FUN)return fallo(p,"capacidad de funciones excedida");
        nombres[nf]=ir->codigo[i].dst;
        viva[nf]=!strcmp(nombres[nf],"main") || !strcmp(nombres[nf],"__o2_init_global");++nf;
    }
    bool cambio=true;
    while(cambio) {
        cambio=false;size_t f=VAC;
        for(size_t i=0;i<ir->cantidad;++i) {
            const IrInstr *x=&ir->codigo[i];
            if(x->op==IR_FUNC) {
                f=0;while(f<nf && strcmp(nombres[f],x->dst))++f;
            } else if(x->op==IR_CALL && f<nf && viva[f]) {
                size_t d=0;while(d<nf && strcmp(nombres[d],x->a))++d;
                if(d==nf)return fallo(p,"llamada sin funcion definida");
                if(!viva[d]){viva[d]=true;cambio=true;}
            }
        }
    }
    IrInstr *seleccion=calloc(ir->cantidad?ir->cantidad:1,sizeof *seleccion);
    if(!seleccion)return 0;
    IrPrograma vista={.codigo=seleccion,.temporales=ir->temporales,.etiquetas=ir->etiquetas},nueva={0};size_t f=VAC;
    for(size_t i=0;i<ir->cantidad;++i) {
        if(ir->codigo[i].op==IR_FUNC){f=0;while(f<nf && strcmp(nombres[f],ir->codigo[i].dst))++f;}
        if(f<nf && viva[f])seleccion[vista.cantidad++]=ir->codigo[i];
    }
    int ok=copiar_ir(&vista,&nueva);free(seleccion);
    if(!ok){ir_liberar(&nueva);return 0;}
    ir_liberar(ir);*ir=nueva;return 1;
}
/* Los errores constantes conservan el rechazo del backend BAL. Las variantes
 * dinámicas usan las secuencias existentes y su control de error en ejecución. */
static int validar_aritmetica(Productivo *p,const IrPrograma *ir)
{
    for(size_t i=0;i<ir->cantidad;++i) {
        const IrInstr *x=&ir->codigo[i];if(x->op!=IR_BIN || !x->b || !x->operador)continue;
        char *fin;errno=0;long long b=strtoll(x->b,&fin,10);
        if(errno || fin==x->b || *fin)continue;
        if((!strcmp(x->operador,"euclides") || !strcmp(x->operador,"euler")) && !b)
            return fallo(p,"division/residuo por cero constante");
        if(!strcmp(x->operador,"descartes") && b<0)return fallo(p,"exponente negativo constante");
    }
    return 1;
}
/* Solo $tN participa en el mapa de tipos previo a la asignación física. */
static unsigned numero_temporal(const char *s)
{
    unsigned n;char extra;return s && sscanf(s,"$t%u%c",&n,&extra)==1?n:0;
}
/* Las referencias conservan stride y ancho ANTES de spilling/unrolling.
 * Después de asignar registros ya no sería válido deducir su tipo de $rN. */
static int tipar_referencias(Productivo *p,IrPrograma *ir)
{
    typedef struct {const SimSimbolo *s;unsigned dimension, ancho;} TipoRef;
    if(ir->temporales>100000)return fallo(p,"capacidad de temporales excedida");
    TipoRef *refs=calloc((size_t)ir->temporales+1,sizeof *refs);if(!refs)return 0;
    int ok=1;
    for(size_t i=0;i<ir->cantidad && ok;++i) {
        IrInstr *x=&ir->codigo[i];unsigned t=numero_temporal(x->a),d=numero_temporal(x->dst);
        if(x->op==IR_INDEX_REF) {
            TipoRef base={identidad(p,x->a),0,0};if(!base.s && t<=ir->temporales)base=refs[t];
            const SimSimbolo *s=base.s;
            if(!s || base.dimension>=s->dimensiones || !d || d>ir->temporales) {ok=fallo(p,"referencia sin tipo/dimensión acreditados");break;}
            unsigned ancho=s->tipo==SIM_TIPO_INT?4u:1u;size_t stride=ancho;
            for(unsigned j=base.dimension+1;j<s->dimensiones;++j) {
                if(!s->longitudes || !s->longitudes[j] || s->longitudes[j]>65536/stride) {ok=0;break;}
                stride*=s->longitudes[j];
            }
            if(!ok)break;
            char meta[48];snprintf(meta,sizeof meta,"%zu:%u",stride,ancho);
            free(x->operador);x->operador=duplicar(meta);if(!x->operador){ok=0;break;}
            refs[d]=(TipoRef){s,base.dimension+1,ancho};
        } else if(x->op==IR_LOAD || x->op==IR_STORE) {
            unsigned ref=numero_temporal(x->op==IR_LOAD?x->a:x->dst);
            if(!ref || ref>ir->temporales || !refs[ref].s || refs[ref].dimension!=refs[ref].s->dimensiones) {ok=fallo(p,"load/store sin referencia completa");break;}
            free(x->operador);x->operador=duplicar(refs[ref].ancho==4?"4":"1");if(!x->operador)ok=0;
        }
    }
    free(refs);return ok;
}
/* Cada operación se codifica con CB y se decodifica por S2. Los metadatos
 * y riesgos físicos los construye la cadena; aquí no se inventan máscaras. */
static int emitir(Productivo *p,unsigned slot,const char *fmt,...)
{
    if(p->cantidad>=MAX_OP || slot>2)return fallo(p,"capacidad de operaciones físicas excedida");
    char texto[160],fuente[512];va_list a;va_start(a,fmt);int n=vsnprintf(texto,sizeof texto,fmt,a);va_end(a);
    if(n<0 || (size_t)n>=sizeof texto)return 0;
    snprintf(fuente,sizeof fuente,"%s | %s | %s | nop\n",slot==0?texto:"nop",slot==1?texto:"nop",slot==2?texto:"nop");
    CbPrograma cb={0};if(cb_ensamblar(fuente,&cb,p->diag)) {cb_liberar(&cb);return 0;}
    SimInstruccion real;uint32_t palabra=cb.bundles[0].slots[slot];
    if(sim_decode_palabra(palabra,slot,&real)!=SIM_DECODE_OK || !palabra){cb_liberar(&cb);return 0;}
    size_t i=p->cantidad++;p->operaciones[i]=(VliwCfgOperacion){i+1,palabra,(VliwSlot)slot,real.salto,slot==2?VLIW_CONTROL_SALTO:VLIW_CONTROL_NINGUNO};
    memcpy(p->textos[i],texto,(size_t)n+1);p->ptrs[i]=p->textos[i];p->origen[i]=p->indice_ir;cb_liberar(&cb);return 1;
}
/* Identidades estructuradas: las referencias se resuelven por esta tabla,
 * nunca buscando una dirección en el ensamblador final de S8-A. */
static size_t simbolo(Productivo *p,const char *nombre)
{
    for(size_t i=0;i<p->ns;++i)if(!strcmp(p->simbolos[i].nombre,nombre))return i;
    if(p->ns==MAX_OP || strlen(nombre)>=sizeof p->simbolos[0].nombre)return VAC;
    size_t i=p->ns++;snprintf(p->simbolos[i].nombre,sizeof p->simbolos[i].nombre,"%s",nombre);p->simbolos[i].posicion=VAC;return i;
}
/* Una definición fija la posición de referencia; el PC se obtiene del plan. */
static int etiqueta(Productivo *p,const char *nombre)
{
    size_t s=simbolo(p,nombre);if(s==VAC || p->simbolos[s].posicion!=VAC)return fallo(p,"etiqueta duplicada o capacidad");
    p->simbolos[s].posicion=p->cantidad;p->simbolos[s].funcion=p->actual;return 1;
}
/* Acredita intención de control y continuación por índices físicos. La ABI
 * de llamadas se expresa aparte de las máscaras GPR decodificadas por S2. */
static int transferencia(Productivo *p,const char *dest,VliwDestinoTipo tipo,VliwControl control)
{
    if(!p->cantidad || p->nt==MAX_OP)return 0;
    size_t s=dest?simbolo(p,dest):VAC;if(dest && s==VAC)return 0;
    size_t n=p->nt++,i=p->cantidad-1;p->operaciones[i].control=control;
    if(control==VLIW_CONTROL_LLAMADA)p->efectos[i]|=VLIW_EF_DESCONOCIDOS;
    p->transferencias[n]=(VliwCfgTransferencia){i,tipo,VAC,
        (tipo==VLIW_DEST_CONDICIONAL || control==VLIW_CONTROL_LLAMADA)?i+1:VAC,NULL,0,true};
    p->destinos[n]=s;return 1;
}
/* Relajación conservadora ANTES del CFG: invertir la condición sobre un
 * salto SYE crea un destino local corto y deja el alcance largo al inmediato
 * de 16 bits. Cada nuevo borde se acredita estructuralmente; S8-A no recibe
 * un plan que luego haya que expandir o reempaquetar. */
static int condicion_larga(Productivo *p,const char *op,unsigned a,unsigned b,const char *dest)
{
    const char *inversa=!strcmp(op,"igualsi")?"igualno":!strcmp(op,"igualno")?"igualsi":!strcmp(op,"menora")?"mayoroigual":!strcmp(op,"mayoroigual")?"menora":NULL;
    if(!inversa)return 0;
    char seguir[64];snprintf(seguir,sizeof seguir,"__o2_skip_%zu",p->cantidad);
    return emitir(p,2,"%s x%u,x%u,0",inversa,a,b) && transferencia(p,seguir,VLIW_DEST_CONDICIONAL,VLIW_CONTROL_SALTO) &&
        emitir(p,2,"sye x0,0") && transferencia(p,dest,VLIW_DEST_DIRECTO,VLIW_CONTROL_SALTO) && etiqueta(p,seguir);
}
/* Las direcciones de llamada usan siete operaciones de forma fija. Variar
 * sus bits no altera longitud/lecturas/escrituras; aun así se reconstruye y
 * verifica toda la cadena por generación hasta demostrar convergencia. */
static int constante(Productivo *p,unsigned r,uint32_t v,bool fija)
{
    if(!fija && v<=1023)return emitir(p,0,"sumai x%u,x0,%u",r,v);
    if(!emitir(p,0,"sumai x%u,x0,%u",r,v>>24))return 0;
    for(int k=16;k>=0;k-=8)
        if(!emitir(p,0,"cizqi x%u,x%u,8",r,r)||!emitir(p,0,"ori x%u,x%u,%u",r,r,(v>>k)&255u))return 0;
    return 1;
}
/* Reserva una expansión de longitud fija para evitar que el valor de la
 * dirección cambie el tamaño del propio layout durante el enlace. */
static int direccion_simbolo(Productivo *p,unsigned r,const char *nombre)
{
    size_t s=simbolo(p,nombre);if(s==VAC || p->nr==MAX_OP)return 0;
    p->reubicaciones[p->nr++]=(Reubicacion){p->cantidad,s};return constante(p,r,0,true);
}
/* Los literales conservan bits int32 y los booleanos del frontend. Char
 * admite aquí un byte explícito; otros formatos se rechazan sin truncarlos. */
static int inmediato(const char *s,uint32_t *v)
{
    if(!s)return 0;
    if(!strcmp(s,"declare_true")){*v=1;return 1;}if(!strcmp(s,"declare_false")){*v=0;return 1;}
    size_t n=strlen(s);if(n==3 && s[0]=='$' && s[2]=='$'){*v=(unsigned char)s[1];return 1;}
    errno=0;char *fin;long long x=strtoll(s,&fin,10);
    if(errno || fin==s || *fin || x<INT32_MIN || x>INT32_MAX)return 0;
    *v=(uint32_t)x;return 1;
}
/* Una identidad física no necesita una operación extra ni una dependencia. */
static int mover(Productivo *p,unsigned d,unsigned a)
{ return d==a || emitir(p,0,"sumai x%u,x%u,0",d,a); }
/* Los accesos lejanos se expanden antes de CFG/DAG. Se conserva el ancho
 * de byte de char/boolean; un parámetro tiene home ABI de cuatro bytes. */
static int acceso(Productivo *p,bool store,unsigned r,unsigned base,size_t off,unsigned ancho)
{
    if(off>65535)return fallo(p,"offset fuera de DMEM");
    if(off>1023) {
        unsigned aux=(r==AUX_B || base==AUX_B)?12:AUX_B;
        if(!constante(p,aux,(uint32_t)off,false)||!emitir(p,0,"suma x%u,x%u,x%u",aux,base,aux))return 0;
        base=aux;off=0;
    }
    return emitir(p,1,"%s x%u,x%u,%zu",store?(ancho==1?"guardab":"guardap"):(ancho==1?"cargabai":"cargai"),store?base:r,store?r:base,off);
}
/* Resuelve valores desde símbolos acreditados, registros asignados o
 * literales. Un arreglo local produce dirección; un parámetro carga su
 * dirección prestada del home slot. No se reserva memoria para sus elementos. */
static int valor(Productivo *p,const char *s,unsigned aux,unsigned *r)
{
    if(ir_fisico_mapear(s,p->registros,r)==0)return 1;
    const SimSimbolo *v=identidad(p,s);
    if(v) {
        if(v->desplazamiento<0 || (v->almacen!=SIM_ALMACEN_LOCAL && v->almacen!=SIM_ALMACEN_PARAMETRO && v->almacen!=SIM_ALMACEN_GLOBAL))return fallo(p,"variable sin ubicación física");
        unsigned base=v->almacen==SIM_ALMACEN_GLOBAL?3:2;
        if(v->dimensiones && v->almacen!=SIM_ALMACEN_PARAMETRO) {
            if(!constante(p,aux,(uint32_t)v->desplazamiento,false)||!emitir(p,0,"suma x%u,x%u,x%u",aux,base,aux))return 0;
        } else {
            unsigned ancho=v->almacen==SIM_ALMACEN_PARAMETRO || v->tipo==SIM_TIPO_INT?4:1;
            if(!acceso(p,false,aux,base,(size_t)v->desplazamiento,ancho))return 0;
        }
        *r=aux;return 1;
    }
    uint32_t n;if(!inmediato(s,&n))return fallo(p,"operando/literal no soportado");
    if(!constante(p,aux,n,false))return 0;
    *r=aux;return 1;
}
/* El ancho procede del símbolo, no del registro: las escrituras de byte
 * deben conservar los objetos adyacentes. Parámetros usan homes ABI de 4B. */
static int guardar_valor(Productivo *p,const char *s,unsigned r)
{
    unsigned d;if(ir_fisico_mapear(s,p->registros,&d)==0)return mover(p,d,r);
    const SimSimbolo *v=identidad(p,s);
    if(!v || v->dimensiones || v->desplazamiento<0)return fallo(p,"destino sin símbolo escalar");
    unsigned ancho=v->almacen==SIM_ALMACEN_PARAMETRO || v->tipo==SIM_TIPO_INT?4:1;
    return acceso(p,true,r,v->almacen==SIM_ALMACEN_GLOBAL?3:2,(size_t)v->desplazamiento,ancho);
}
/* Los helpers existentes emiten texto lineal con etiquetas propias. Se
 * importan sus operaciones y referencias ANTES de planificar, sin utilizar
 * ese texto para deducir destinos físicos posteriores al scheduling. */
static int importar_lineal(Productivo *p,FILE *f)
{
    if(fflush(f) || fseek(f,0,SEEK_SET))return 0;
    char linea[256];
    while(fgets(linea,sizeof linea,f)) {
        char *x=linea;while(*x==' '||*x=='\t')++x;
        char *nl=strpbrk(x,"\r\n");if(nl)*nl=0;if(!*x)continue;
        size_t l=strlen(x);if(x[l-1]==':'){x[l-1]=0;if(!etiqueta(p,x))return 0;continue;}
        char op[32];if(sscanf(x,"%31s",op)!=1)return 0;
        bool bru=!strcmp(op,"sye")||!strcmp(op,"syer")||!strcmp(op,"igualsi")||!strcmp(op,"igualno")||!strcmp(op,"menora")||!strcmp(op,"mayoroigual");
        unsigned slot=bru?2:(!strncmp(op,"carga",5)||!strncmp(op,"guarda",6))?1:0;
        if(bru && strcmp(op,"syer")) {
            char *coma=strrchr(x,',');if(!coma)return 0;char *dest=coma+1;while(*dest==' ')++dest;
            char nombre[96];if(strlen(dest)>=sizeof nombre)return 0;strcpy(nombre,dest);strcpy(coma+1,"0");
            if(!strcmp(op,"sye")) {
                if(!emitir(p,2,"%s",x)||!transferencia(p,nombre,VLIW_DEST_DIRECTO,VLIW_CONTROL_SALTO))return 0;
            } else {
                unsigned a,b;
                if(sscanf(x,"%*s x%u, x%u,",&a,&b)!=2 || !condicion_larga(p,op,a,b,nombre))return 0;
            }
        } else if(!emitir(p,slot,"%s",x))return 0;
    }
    return !ferror(f);
}
/* Las funciones tienen marcos particulares y el total global de ranuras
 * que exige la pasada existente. ARG se apila lógicamente en slots privados:
 * una llamada anidada consume solo sus últimos N argumentos. */
static int preparar_funciones(Productivo *p,const IrPrograma *ir,size_t ranuras)
{
    p->nf=1;p->funciones[0].nombre="__o2_arranque";size_t f=0,pend=0,max=0;
    for(size_t i=0;i<ir->cantidad;++i) {
        const IrInstr *x=&ir->codigo[i];
        if(x->op==IR_FUNC) {
            if(f || p->nf==MAX_FUN)return fallo(p,"anidamiento/capacidad de funciones IR");
            f=p->nf++;p->funciones[f].nombre=x->dst;pend=max=0;
        } else if(x->op==IR_END_FUNC) {
            if(!f || pend)return fallo(p,"argumentos o función sin cerrar");
            Funcion *fn=&p->funciones[f];fn->argumentos=max;
            if(ranuras>SIZE_MAX-16-max || (!strcmp(fn->nombre,"__o2_init_global") ? ir_spilling_backend_preparar_expandido(4,ranuras+16+max,0,&fn->backend) : ir_spilling_backend_desde_tabla_referencias(p->tabla,fn->nombre,ranuras+16+max,&fn->backend)))return fallo(p,"marco de función/spills no acreditado o capacidad LSU");
            if(fn->backend.marco.bytes_marco>32768)return fallo(p,"marco excede zona de pila O2");
            fn->guardados=fn->backend.marco.inicio_spilling+4*ranuras;fn->area_args=fn->guardados+64;f=0;
        } else if(x->op==IR_ARG) {if(!f || ++pend>255)return fallo(p,"capacidad de argumentos");if(pend>max)max=pend;}
        else if(x->op==IR_CALL) {char *fin;unsigned long n=x->b?strtoul(x->b,&fin,10):ULONG_MAX;if(!x->b||*fin||n>pend)return fallo(p,"pila lógica de argumentos incoherente");pend-=n;}
        else if(!f)return fallo(p,"IR global aún no soportada por este productor");
    }
    return !f;
}
/* Las llamadas se vinculan con una entrada única de función física. */
static size_t buscar_funcion(const Productivo *p,const char *s)
{
    for(size_t i=1;i<p->nf;++i)if(!strcmp(p->funciones[i].nombre,s))return i;
    return VAC;
}
/* Protocolo callee-saved explícito para x16..31. No se inventan clobbers de
 * un SYER: las salvaguardas/restauraciones son instrucciones LSU reales. */
static int prologo(Productivo *p,Funcion *fn)
{
    FILE *f=tmpfile();if(!f)return 0;
    int ok=fm_emitir_prologo(f,fn->backend.marco.bytes_marco,fn->backend.numero_parametros)==FM_OK && importar_lineal(p,f);fclose(f);
    /* x29 participa en direcciones lejanas. Se salva primero usando x12,
     * antes de que otra salvaguarda pueda reutilizarlo como auxiliar. */
    if(ok)ok=acceso(p,true,29,2,fn->guardados+4*(29-16),4);
    for(unsigned r=16;ok && r<32;++r)
        if(r!=29)ok=acceso(p,true,r,2,fn->guardados+4*(r-16),4);
    return ok;
}
/* Restaura el marco particular y registra el retorno con destinos posibles
 * derivados exclusivamente de llamadas reales, incluida la recursión. */
static int epilogo(Productivo *p,Funcion *fn)
{
    /* Se restaura x29 al final: las cargas anteriores pueden necesitarlo
     * para calcular su dirección. Así se preserva también en marcos grandes. */
    for(unsigned r=16;r<32;++r)
        if(r!=29 && !acceso(p,false,r,2,fn->guardados+4*(r-16),4))return 0;
    if(!acceso(p,false,29,2,fn->guardados+4*(29-16),4))return 0;
    FILE *f=tmpfile();if(!f)return 0;
    if(fm_emitir_epilogo(f,fn->backend.marco.bytes_marco)!=FM_OK || fflush(f) || fseek(f,0,SEEK_SET)){fclose(f);return 0;}
    /* El helper termina en SYER retorno: se registra su intención y sus
     * continuaciones estructuradas, sin inferirlas de la lectura de x1. */
    int ok=importar_lineal(p,f);fclose(f);
    return ok && transferencia(p,NULL,VLIW_DEST_RETORNO,VLIW_CONTROL_RETORNO);
}
/* ARG ocupa slots privados antes de preparar los registros ABI. Las
 * llamadas anidadas consumen sus últimos argumentos, conservando los
 * exteriores pendientes; los argumentos extra viven en la pila del caller. */
static int llamada(Productivo *p,Funcion *fn,const IrInstr *x)
{
    size_t callee=buscar_funcion(p,x->a);if(callee==VAC)return fallo(p,"función llamada no definida");
    size_t n=(size_t)strtoul(x->b,NULL,10);if(n>fn->pendientes)return fallo(p,"ARG/CALL incoherentes");
    size_t desde=fn->pendientes-n,extra=n>4?4*(n-4):0;
    for(size_t k=0;k<n && k<4;++k)if(!acceso(p,false,8+(unsigned)k,2,fn->area_args+4*(desde+k),4))return 0;
    if(extra && !emitir(p,0,"restai x2,x2,%zu",extra))return 0;
    for(size_t k=4;k<n;++k)
        if(!acceso(p,false,12,2,extra+fn->area_args+4*(desde+k),4)||!acceso(p,true,12,2,4*(k-4),4))return 0;
    if(!direccion_simbolo(p,13,x->a)||!emitir(p,2,"syer x1,x13,0")||!transferencia(p,x->a,VLIW_DEST_INDIRECTO,VLIW_CONTROL_LLAMADA))return 0;
    /* Todas las llamadas tienen una continuación física incluso si no hay
     * argumentos en pila. La copia del retorno evita una caída fuera de función. */
    if(extra && !emitir(p,0,"sumai x2,x2,%zu",extra))return 0;
    fn->pendientes-=n;return guardar_valor(p,x->dst,8);
}
/* Expande cada operación sobre registros ya asignados. Se conservan marcas
 * de origen por operación física; regiones rectilíneas no admiten entradas
 * internas. Las expansiones con control tienen sus propios símbolos/CFG. */
static int bajar(Productivo *p,const IrPrograma *ir)
{
    p->indice_ir=VAC;p->actual=0;Funcion *fn=&p->funciones[0];fn->inicio=0;
    if(!etiqueta(p,fn->nombre)||!constante(p,2,65536,false)||!constante(p,3,16384,false))return 0;
    if(buscar_funcion(p,"__o2_init_global")!=VAC &&
       (!direccion_simbolo(p,13,"__o2_init_global")||!emitir(p,2,"syer x1,x13,0")||!transferencia(p,"__o2_init_global",VLIW_DEST_INDIRECTO,VLIW_CONTROL_LLAMADA)))return 0;
    if(!direccion_simbolo(p,13,"main")||!emitir(p,2,"syer x1,x13,0")||!transferencia(p,"main",VLIW_DEST_INDIRECTO,VLIW_CONTROL_LLAMADA)||!etiqueta(p,"__o2_fin")||!emitir(p,2,"sye x0,0")||!transferencia(p,"__o2_fin",VLIW_DEST_DIRECTO,VLIW_CONTROL_SALTO))return 0;
    fn->fin=p->cantidad;
    for(size_t i=0;i<ir->cantidad;++i) {
        const IrInstr *x=&ir->codigo[i];p->linea=x->linea;p->indice_ir=i;
        size_t inicio=p->cantidad,nt=p->nt;unsigned a,b,d;bool ok=true;
        switch(x->op) {
        case IR_FUNC:
            p->actual=buscar_funcion(p,x->dst);if(p->actual==VAC)return 0;fn=&p->funciones[p->actual];fn->inicio=p->cantidad;
            ok=etiqueta(p,x->dst)&&prologo(p,fn);break;
        case IR_END_FUNC:
            ok=p->cantidad>fn->inicio && p->operaciones[p->cantidad-1].control==VLIW_CONTROL_RETORNO;
            /* Una etiqueta posterior al último retorno necesita destino real;
             * el epílogo adicional evita símbolos apuntando fuera de función. */
            for(size_t j=0;j<p->ns;++j)
                if(p->simbolos[j].posicion==p->cantidad)ok=false;
            if(!ok)ok=epilogo(p,fn);
            fn->fin=p->cantidad;break;
        case IR_PARAM_DECL:case IR_ARRAY_DECL:case IR_ARRAY_DIM: break;
        case IR_LABEL:ok=etiqueta(p,x->dst);break;
        case IR_COPY:ok=valor(p,x->a,AUX_A,&a)&&guardar_valor(p,x->dst,a);break;
        case IR_SPILL_LOAD:case IR_SPILL_STORE: {
            size_t off;bool store=x->op==IR_SPILL_STORE;
            ok=!ir_spilling_offset_expandido(&fn->backend.marco,store?x->dst:x->a,&off) && !ir_fisico_mapear(store?x->a:x->dst,p->registros,&a) && acceso(p,store,a,2,off,4);break;
        }
        case IR_BIN: {
            ok=valor(p,x->a,AUX_A,&a)&&valor(p,x->b,AUX_B,&b);if(!ok)break;
            d=4;const char *op=x->operador,*isa=NULL;bool invertir=false;
            if(!strcmp(op,"gauss"))isa="suma";else if(!strcmp(op,"neumann"))isa="resta";
            else if(!strcmp(op,"<")||!strcmp(op,">=")){isa="mrq";invertir=!strcmp(op,">=");}
            else if(!strcmp(op,">")||!strcmp(op,"<=")){isa="myq";invertir=!strcmp(op,"<=");}
            else if(!strcmp(op,"&&"))isa="and";else if(!strcmp(op,"||"))isa="or";else if(!strcmp(op,"^"))isa="xor";
            if(isa)ok=emitir(p,0,"%s x4,x%u,x%u",isa,a,b);
            else if(!strcmp(op,"==")||!strcmp(op,"=/=")) {
                ok=emitir(p,0,"xor x4,x%u,x%u",a,b)&&emitir(p,0,"mrq x5,x4,x0")&&emitir(p,0,"myq x4,x4,x0")&&emitir(p,0,"or x4,x4,x5");invertir=!strcmp(op,"==");
            } else {
                FILE *f=tmpfile();if(!f){ok=false;break;}
                ok=mover(p,4,a)&&mover(p,5,b)&&!gb_expandir_aritmetica(op,f,p->diag)&&importar_lineal(p,f);fclose(f);
            }
            if(ok&&invertir)ok=emitir(p,0,"xori x4,x4,1");
            if(ok)ok=guardar_valor(p,x->dst,d);
            break;
        }
        case IR_UNARY:
            ok=valor(p,x->a,AUX_A,&a);
            if(ok && !strcmp(x->operador,"neumann"))ok=emitir(p,0,"resta x4,x0,x%u",a);
            else if(ok && !strcmp(x->operador,"~"))ok=emitir(p,0,"xori x4,x%u,1",a);
            else if(ok && !strcmp(x->operador,"bool"))ok=emitir(p,0,"mrq x4,x%u,x0",a)&&emitir(p,0,"myq x5,x%u,x0",a)&&emitir(p,0,"or x4,x4,x5");
            else ok=false;
            if(ok)ok=guardar_valor(p,x->dst,4);
            break;
        case IR_INDEX_REF: {
            unsigned stride,ancho;if(!x->operador || sscanf(x->operador,"%u:%u",&stride,&ancho)!=2){ok=false;break;}
            ok=valor(p,x->a,AUX_A,&a)&&valor(p,x->b,AUX_B,&b)&&mover(p,4,b);
            /* Strides potencia de dos usan el corrimiento ISA; otros reutilizan
             * la multiplicación del backend, preservando la base en x28. */
            unsigned k=0;while(k<16 && (1u<<k)<stride)++k;
            if(ok && (1u<<k)==stride)ok=emitir(p,0,"cizqi x4,x4,%u",k)&&emitir(p,0,"suma x4,x%u,x4",a);
            else if(ok) {
                ok=mover(p,AUX_A,a)&&constante(p,5,stride,false);FILE *f=tmpfile();if(!f){ok=false;break;}
                ok=ok&&!gb_expandir_aritmetica("pitagoras",f,p->diag)&&importar_lineal(p,f)&&emitir(p,0,"suma x4,x28,x4");fclose(f);
            }
            if(ok)ok=guardar_valor(p,x->dst,4);
            break;
        }
        case IR_LOAD:
            ok=valor(p,x->a,AUX_A,&a)&&acceso(p,false,4,a,0,x->operador&&!strcmp(x->operador,"1")?1:4)&&guardar_valor(p,x->dst,4);break;
        case IR_STORE:
            ok=valor(p,x->dst,AUX_A,&a)&&valor(p,x->a,AUX_B,&b)&&acceso(p,true,b,a,0,x->operador&&!strcmp(x->operador,"1")?1:4);break;
        case IR_JUMP:ok=emitir(p,2,"sye x0,0")&&transferencia(p,x->dst,VLIW_DEST_DIRECTO,VLIW_CONTROL_SALTO);break;
        case IR_JFALSE:case IR_JTRUE:
            ok=valor(p,x->a,AUX_A,&a)&&condicion_larga(p,x->op==IR_JFALSE?"igualsi":"igualno",a,0,x->dst);break;
        case IR_ARG:
            ok=fn->pendientes<fn->argumentos && valor(p,x->a,AUX_A,&a)&&acceso(p,true,a,2,fn->area_args+4*fn->pendientes,4);if(ok)++fn->pendientes;break;
        case IR_CALL:ok=llamada(p,fn,x);break;
        case IR_RETURN:
            ok=(!x->a || (valor(p,x->a,AUX_A,&a)&&mover(p,8,a)))&&epilogo(p,fn);break;
        default:ok=false;break;
        }
        if(!ok)return fallo(p,"lowering no soportado/recursos/rango; sin ejecutable parcial");
        if(p->cantidad>inicio+1 && p->nt==nt && x->op!=IR_FUNC && x->op!=IR_END_FUNC) {
            if(p->nreg==MAX_OP)return 0;
            p->regiones[p->nreg++]=(VliwCfgRegion){inicio,p->cantidad};
        }
    }
    return 1;
}
/* Vincula los únicos destinos dinámicos emitidos: llamadas locales y los
 * retornos a continuaciones de llamadas a esa función. Un destino de retorno
 * no es simplemente cualquier símbolo del programa. */
static int resolver_cfg(Productivo *p)
{
    for(size_t i=0;i<p->ns;++i) {
        if(p->simbolos[i].posicion==VAC || p->simbolos[i].posicion>=p->cantidad)return fallo(p,"símbolo sin operación física");
        p->simbolos_cfg[i]=(VliwCfgSimbolo){i,p->simbolos[i].funcion,p->simbolos[i].posicion};
    }
    for(size_t i=0;i<p->nf;++i)p->funciones_cfg[i]=(VliwCfgFuncion){i,p->funciones[i].inicio,p->funciones[i].fin};
    size_t total=0;
    /* Las continuaciones son símbolos reales creados por la posición i+1.
     * Se registran antes de asignar sus arrays de posibles, que son préstamos. */
    for(size_t t=0;t<p->nt;++t)if(p->operaciones[p->transferencias[t].operacion].control==VLIW_CONTROL_LLAMADA) {
        char n[64];snprintf(n,sizeof n,"__cont_%zu",t);size_t s=simbolo(p,n);if(s==VAC)return 0;
        size_t pos=p->transferencias[t].continuacion,f=0;while(f+1<p->nf && pos>=p->funciones[f].fin)++f;
        p->simbolos[s].posicion=pos;p->simbolos[s].funcion=f;p->simbolos_cfg[s]=(VliwCfgSimbolo){s,f,pos};
    }
    for(size_t t=0;t<p->nt;++t) {
        VliwCfgTransferencia *x=&p->transferencias[t];VliwControl ctl=p->operaciones[x->operacion].control;
        if(x->tipo==VLIW_DEST_DIRECTO || x->tipo==VLIW_DEST_CONDICIONAL)x->tomado=p->destinos[t];
        else if(ctl==VLIW_CONTROL_LLAMADA) {
            if(total==MAX_OP)return 0;
            p->posibles[total]=p->destinos[t];x->posibles=&p->posibles[total++];x->cantidad_posibles=1;
        } else {
            size_t f=0;while(f+1<p->nf && x->operacion>=p->funciones[f].fin)++f;
            x->posibles=&p->posibles[total];x->cantidad_posibles=0;
            for(size_t j=0;j<p->nt;++j)if(p->operaciones[p->transferencias[j].operacion].control==VLIW_CONTROL_LLAMADA && p->simbolos[p->destinos[j]].funcion==f) {
                char n[64];snprintf(n,sizeof n,"__cont_%zu",j);size_t s=simbolo(p,n);if(s==VAC || total==MAX_OP)return 0;
                p->posibles[total++]=s;++x->cantidad_posibles;
            }
            if(!x->cantidad_posibles)return fallo(p,"función sin llamadores acreditados; retorno sin cobertura");
        }
    }
    return 1;
}
/* Materializaciones ya dimensionadas: se actualiza cada palabra mediante CB
 * y se vuelve a ejecutar metadatos/CFG/DAG/plan/verificador/emisor. La salida
 * solo se publica cuando la tabla calculada coincide con los valores usados. */
static int enlazar(Productivo *p,IrVliwPrograma *r)
{
    if(!resolver_cfg(p))return 0;
    for(unsigned iter=1;iter<=8;++iter) {
        VliwCfgEntrada flujo={0};flujo.operaciones=p->operaciones;flujo.cantidad_operaciones=p->cantidad;flujo.funciones=p->funciones_cfg;flujo.cantidad_funciones=p->nf;
        flujo.simbolos=p->simbolos_cfg;flujo.cantidad_simbolos=p->ns;flujo.transferencias=p->transferencias;flujo.cantidad_transferencias=p->nt;flujo.regiones=p->regiones;flujo.cantidad_regiones=p->nreg;
        flujo.generacion=flujo.generacion_referencias=iter;flujo.fronteras_acreditadas=true;
        VliwCadenaEntrada e={.flujo=&flujo,.efectos_adicionales=p->efectos,.cantidad_efectos=p->cantidad,.textos=p->ptrs,.cantidad_textos=p->cantidad,.generacion=iter,.efectos_acreditados=true,.abi_acreditado=true,.garantias={0,true,true}};
        VliwCadena *cad=NULL;VliwCadenaDiagnostico d={0};
        if(vliw_cadena_construir(&e,&cad,&d)!=VLIW_CAD_OK) {
            if(p->diag)fprintf(p->diag,"O2 cadena generación %u fase %d: %s\n",iter,(int)d.fase,d.mensaje);
            return 0;
        }
        bool iguales=true;
        for(size_t j=0;j<p->nr;++j) {
            Reubicacion *q=&p->reubicaciones[j];size_t pos=p->simbolos[q->simbolo].posicion;
            size_t bloque=cad->cfg->bloque_por_operacion[pos];uint32_t pc=(uint32_t)(16*cad->plan->inicio_por_bloque[bloque]);
            SimInstruccion decoded;
            if(sim_decode_palabra(p->operaciones[q->primera].palabra,0,&decoded)!=SIM_DECODE_OK){vliw_cadena_liberar(&cad);return 0;}
            unsigned reg=decoded.rd;
            char esperado[160];snprintf(esperado,sizeof esperado,"sumai x%u,x0,%u",reg,pc>>24);
            if(strcmp(esperado,p->textos[q->primera]))iguales=false;
            for(unsigned k=0;k<3;++k) {
                snprintf(esperado,sizeof esperado,"ori x%u,x%u,%u",reg,reg,(pc>>(16-8*k))&255u);
                if(strcmp(esperado,p->textos[q->primera+2+2*k]))iguales=false;
            }
            if(!iguales) {
                size_t guardar=p->cantidad;p->cantidad=q->primera;
                size_t origen=p->indice_ir;p->indice_ir=p->origen[q->primera];
                if(!constante(p,reg,pc,true)){vliw_cadena_liberar(&cad);return 0;}
                p->indice_ir=origen;p->cantidad=guardar;
            }
        }
        if(iguales) {
            r->cadena=cad;r->generaciones=iter;size_t fin=simbolo(p,"__o2_fin");r->pc_fin=16*cad->plan->inicio_por_bloque[cad->cfg->bloque_por_operacion[p->simbolos[fin].posicion]];return 1;
        }
        vliw_cadena_liberar(&cad);
    }
    return fallo(p,"reubicaciones no convergen tras ocho generaciones");
}
/* Libera exclusivamente propiedad del resultado. Acepta NULL y salidas
 * vacías; deja el objeto reutilizable sin tocar AST, símbolos ni archivos. */
void ir_vliw_programa_liberar(IrVliwPrograma *r)
{
    if(!r)return;
    ir_liberar(&r->original);ir_liberar(&r->optimizada);ir_liberar(&r->spilling);vliw_cadena_liberar(&r->cadena);free(r->reporte_json);memset(r,0,sizeof *r);
}
/* Orquesta copias privadas y publica solo una cadena enlazada estable.
 * Las rutas de error confluyen en la liberación de todas las pasadas. El
 * caller conserva AST/tabla durante la llamada y libera el resultado propio. */
static bool ir_vacia(const IrPrograma *ir)
{
    return !ir->codigo && !ir->bloques && !ir->cantidad && !ir->capacidad &&
           !ir->temporales && !ir->etiquetas && !ir->cantidad_bloques;
}
int ir_vliw_programa(const Ast *ast,const SimTabla *tabla,size_t registros,IrVliwPrograma *out,FILE *diag)
{
    if(!ast || !ast->raiz || !tabla || !tabla->global || !out || out->cadena || out->original.codigo || out->optimizada.codigo || out->spilling.codigo || out->reporte_json || out->pc_fin || out->generaciones || out->original.cantidad || out->optimizada.cantidad || out->spilling.cantidad || !ir_vacia(&out->original) || !ir_vacia(&out->optimizada) || !ir_vacia(&out->spilling) || !ir_fisico_configuracion_valida(registros))return 2;
    Productivo *p=calloc(1,sizeof *p);if(!p)return 2;p->tabla=tabla;p->registros=registros;p->diag=diag;
    IrVliwPrograma r={0};Ast copia={0};DceResultado dce={0};LuResumen lu={0};RoResumen ro={0};int estado=1;
    p->cantidad_identidades=tabla->cantidad;p->identidades=calloc(tabla->cantidad,sizeof *p->identidades);if(tabla->cantidad&&!p->identidades){estado=2;goto fin;}
    size_t ns=0;for(const SimSimbolo *s=tabla->primero;s;s=s->siguiente_orden) {if(ns==tabla->cantidad){estado=2;goto fin;}p->identidades[ns++]=s;}
    if(ns!=tabla->cantidad){estado=2;goto fin;}
    copia.raiz=vincular(p,&copia,ast->raiz,NULL,0);if(!copia.raiz)goto fin;
    if(ir_desde_ast_programa(&copia,&r.original,diag) || !tipar_referencias(p,&r.original) || !copiar_ir(&r.original,&r.optimizada))goto fin;
    if(!podar_funciones(p,&r.optimizada) || !validar_aritmetica(p,&r.optimizada))goto fin;
    /* Unrolling necesita el patrón de inducción original. Después versionamos
     * nombres, DCE elimina versiones ya inútiles y reorder consume el CFG
     * reconstruido. Así DCE no oculta antes los casos WAR/WAW del renombrado. */
    /* O2 publica la memoria final de todos los objetos nombrados de main.
     * Por ello sus últimas definiciones son observables aunque no haya otra
     * lectura BAL. La política es conservadora, uniforme y ajena al manifiesto
     * de evaluación; DCE aún elimina escrituras reemplazadas y locales muertos
     * de otras funciones. O0/O1 conservan su contrato histórico. */
    const char *raices[MAX_OP]; size_t nraices=0; bool main_ir=false;
    for(size_t j=0;j<r.original.cantidad;++j) {
        const IrInstr *x=&r.original.codigo[j];
        if(x->op==IR_FUNC)main_ir=x->dst && !strcmp(x->dst,"main");
        if(identidad(p,x->dst) && (main_ir || identidad(p,x->dst)->almacen==SIM_ALMACEN_GLOBAL)) {
            if(nraices==MAX_OP){fallo(p,"capacidad de observables excedida");goto fin;}
            raices[nraices++]=x->dst;
        }
    }
    if(lu_desenrollar(&r.optimizada,NULL,&lu,diag))goto fin;
    IrPrograma versionada={0};IrRenombramiento rn={0};
    if(ir_renombrar_local(&r.optimizada,&versionada,renombrable,p,&rn))goto fin;
    ir_liberar(&r.optimizada);r.optimizada=versionada;r.renombradas=rn.versiones;r.war_eliminadas=rn.war_eliminadas;r.waw_eliminadas=rn.waw_eliminadas;
    if(dce_eliminar_con_observables(&r.optimizada,&dce,raices,nraices))goto fin;
    if(ro_reordenar(&r.optimizada,NULL,&ro))goto fin;
    r.eliminadas=dce.cantidad_eliminadas;r.desenrollados=lu.totales+lu.parciales;r.movidas=ro.movidas;
    if(ir_aplicar_spilling(&r.optimizada,&r.spilling,registros,&r.metricas) || ir_fisico_validar_programa(&r.spilling,registros,NULL) || !preparar_funciones(p,&r.spilling,r.metricas.ranuras) || !bajar(p,&r.spilling) || !enlazar(p,&r))goto fin;
    /* Metadata de observación: identidades/offsets provienen de la tabla real;
     * los marcos finales incluyen spills y preservación, nunca el máximo global. */
    FILE *f=tmpfile();if(!f){estado=2;goto fin;}
    fprintf(f,"{\"version\":1,\"nivel\":\"O2\",\"dce\":%zu,\"unroll\":%zu,\"rename\":%zu,\"reorder\":%zu,\"spills\":%zu,\"loads\":%zu,\"stores\":%zu,\"generaciones\":%zu,\"pc_fin\":%zu,\"bundles\":%zu,\"funciones\":[",r.eliminadas,r.desenrollados,r.renombradas,r.movidas,r.metricas.ranuras,r.metricas.cargas,r.metricas.almacenes,r.generaciones,r.pc_fin,r.cadena->codigo.cantidad);
    for(size_t j=1;j<p->nf;++j) {
        Funcion *fn=&p->funciones[j];size_t b=r.cadena->cfg->bloque_por_operacion[fn->inicio];
        fprintf(f,"%s{\"nombre\":\"%s\",\"marco\":%zu,\"inicio_pc\":%zu,\"fin_pc\":%zu}",j>1?",":"",fn->nombre,fn->backend.marco.bytes_marco,16*r.cadena->plan->inicio_por_bloque[b],16*(r.cadena->plan->bundle_por_instruccion[fn->fin-1]+1));
    }
    fprintf(f,"],\"objetos\":[");bool primero=true;
    for(size_t j=0;j<ns;++j) {
        const SimSimbolo *s=p->identidades[j];if(s->clase==SIM_FUNCION || s->desplazamiento<0)continue;
        const SimAmbito *a=s->ambito;while(a && a->padre && a->padre!=tabla->global)a=a->padre;
        const char *fn=a && a!=tabla->global?a->nombre:"global";
        fprintf(f,"%s{\"id\":%zu,\"nombre\":\"%s\",\"funcion\":\"%s\",\"linea\":%zu,\"offset\":%ld,\"bytes\":%zu}",primero?"":",",j,s->nombre,fn,s->linea,s->desplazamiento,s->bytes);primero=false;
    }
    fprintf(f,"],\"operaciones\":[");
    for(size_t j=0;j<p->cantidad;++j) {
        fprintf(f,"%s{\"indice\":%zu,\"ir_spilling\":",j?",":"",j);
        if(p->origen[j]==VAC)fputs("null",f);else fprintf(f,"%zu",p->origen[j]);
        fprintf(f,",\"bloque\":%zu,\"bundle\":%zu}",r.cadena->cfg->bloque_por_operacion[j],r.cadena->plan->bundle_por_instruccion[j]);
    }
    fprintf(f,"],\"war_eliminadas\":%zu,\"waw_eliminadas\":%zu,\"cantidad_ir_spilling\":%zu,\"cantidad_operaciones\":%zu}\n",r.war_eliminadas,r.waw_eliminadas,r.spilling.cantidad,p->cantidad);long n;if(fflush(f) || (n=ftell(f))<0 || fseek(f,0,SEEK_SET)){fclose(f);goto fin;}
    r.reporte_json=malloc((size_t)n+1);if(!r.reporte_json){fclose(f);estado=2;goto fin;}
    bool io=fread(r.reporte_json,1,(size_t)n,f)!=(size_t)n;r.reporte_json[n]=0;if(fclose(f))io=true;if(io)goto fin;
    *out=r;memset(&r,0,sizeof r);estado=0;
fin:
    dce_liberar_resultado(&dce);lu_resumen_liberar(&lu);ro_resumen_liberar(&ro);ast_liberar(&copia);ir_vliw_programa_liberar(&r);free(p->identidades);free(p);return estado;
}
