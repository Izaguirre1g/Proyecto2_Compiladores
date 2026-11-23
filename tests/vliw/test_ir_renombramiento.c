/* Oráculos de versionado independientes del backend: interpretamos un
 * subconjunto COPY/suma y comparamos el objeto publicado con valores fijos.
 * Además inspeccionamos identidades de versiones para distinguir la pasada
 * de una mera asignación física. Los fallos de malloc/calloc se inyectan en
 * orden; cada rechazo debe conservar la salida vacía y la entrada intacta. */
#include "ir_renombramiento.h"
#include "dce.h"
#include "semantic_modules.h"
#include "memory.h"
#include "ir_vliw_programa.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static long cuenta=-1;
void *__real_malloc(size_t n);
void *__real_calloc(size_t n,size_t s);
void *__wrap_malloc(size_t n){if(cuenta==0)return NULL;if(cuenta>0)--cuenta;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s){if(cuenta==0)return NULL;if(cuenta>0)--cuenta;return __real_calloc(n,s);}
static char *texto(const char *s){if(!s)return NULL;size_t n=strlen(s)+1;char *p=malloc(n);assert(p);memcpy(p,s,n);return p;}
static void emitir(IrPrograma *p,IrOp op,const char *d,const char *a,const char *b)
{
    p->codigo[p->cantidad++]=(IrInstr){op,texto(d),texto(a),texto(b),op==IR_BIN?texto("gauss"):NULL,1};
}
static IrPrograma entrada(void)
{
    IrPrograma p={0};p.capacidad=16;p.codigo=calloc(p.capacidad,sizeof *p.codigo);assert(p.codigo);
    emitir(&p,IR_FUNC,"main",NULL,NULL);emitir(&p,IR_COPY,"a","3",NULL);
    emitir(&p,IR_COPY,"b","5",NULL);emitir(&p,IR_COPY,"copia","a",NULL);
    emitir(&p,IR_COPY,"a","b",NULL);emitir(&p,IR_COPY,"b","copia",NULL);
    emitir(&p,IR_BIN,"resultado","a","b");emitir(&p,IR_END_FUNC,"main",NULL,NULL);
    assert(!ir_bloques_basicos(&p));return p;
}
static int elegible(const char *s,void *ctx){(void)ctx;return s && s[0]!='$';}
typedef struct {const char *s;int32_t valor;} Valor;
static int32_t leer(Valor *v,size_t n,const char *s)
{
    for(size_t i=0;i<n;++i)if(!strcmp(v[i].s,s))return v[i].valor;
    char *fin;long x=strtol(s,&fin,10);assert(!*fin);return (int32_t)x;
}
static int32_t interpretar(const IrPrograma *p)
{
    Valor v[80];size_t n=0;
    for(size_t i=0;i<p->cantidad;++i){const IrInstr *x=&p->codigo[i];
        if(x->op!=IR_COPY && x->op!=IR_BIN)continue;
        int32_t valor=leer(v,n,x->a);if(x->op==IR_BIN)valor+=leer(v,n,x->b);
        size_t j=0;for(;j<n;++j)if(!strcmp(v[j].s,x->dst))break;
        if(j==n){assert(n<80);v[n++].s=x->dst;}v[j].valor=valor;
    }
    return leer(v,n,"resultado");
}
enum { MODO_NORMAL, MODO_RECURSOS, MODO_OBSERVABLES, MODO_DIVISION, MODO_EXPONENTE };
static int contiene(const IrPrograma *ir,const char *nombre)
{
    for(size_t i=0;i<ir->cantidad;++i) {
        const IrInstr *x=&ir->codigo[i];
        const char *campos[]={x->dst,x->a,x->b};
        for(size_t k=0;k<3;++k)if(campos[k] && !strcmp(campos[k],nombre))return 1;
    }
    return 0;
}
static int diagnostico_contiene(FILE *f,const char *fragmento)
{
    char buffer[4096]={0};rewind(f);
    size_t n=fread(buffer,1,sizeof buffer-1,f);buffer[n]='\0';
    return strstr(buffer,fragmento)!=NULL;
}
/* La tabla se prepara una vez. Cada fallo afecta únicamente la construcción
 * privada: ninguna salida parcial ni mutación del AST/tabla queda publicada.
 * Los modos observables/aritmética son regresiones del productor O2 completo. */
static int prueba_programa(void *ctx,const Ast *ast,SimTabla *tabla,FILE *diag)
{
    int modo=ctx?*(const int *)ctx:MODO_NORMAL;
    MemProyectoMapa mapa={0};
    assert(mem_asignar_proyecto_referencias(tabla,16384,32768,32768,&mapa)==MEM_PROYECTO_OK);
    SimTabla tabla_antes=*tabla;Ast ast_antes=*ast;IrVliwPrograma out={0};
    assert(ir_vliw_programa(NULL,tabla,8,&out,NULL)==2);
    out.pc_fin=1;assert(ir_vliw_programa(ast,tabla,8,&out,NULL)==2);
    /* No se prueba memoria inaccesible: los argumentos siempre apuntan a
     * objetos reales y vivos, incluso en los rechazos de estructura. */
    out.pc_fin=0;
    if(modo==MODO_DIVISION || modo==MODO_EXPONENTE) {
        int rc=ir_vliw_programa(ast,tabla,8,&out,diag);
        const char *esperado=modo==MODO_DIVISION ?
            "division/residuo por cero constante" : "exponente negativo constante";
        assert(rc==1 && !out.cadena && !out.reporte_json && !out.original.codigo &&
               !out.optimizada.codigo && !out.spilling.codigo);
        assert(diagnostico_contiene(diag,esperado));
        assert(diagnostico_contiene(diag,"línea 2"));
        assert(!memcmp(tabla,&tabla_antes,sizeof *tabla));assert(!memcmp(ast,&ast_antes,sizeof *ast));
        return 0;
    }
    int exito=0;long limite=modo==MODO_RECURSOS?5000:1;
    for(long i=0;i<limite;++i) {
        cuenta=modo==MODO_RECURSOS?i:-1;
        int rc=ir_vliw_programa(ast,tabla,8,&out,diag);cuenta=-1;
        assert(!memcmp(tabla,&tabla_antes,sizeof *tabla));assert(!memcmp(ast,&ast_antes,sizeof *ast));
        if(!rc) {
            if(modo==MODO_RECURSOS)printf("fallos_reserva_productor=%ld\n",i);
            assert(out.cadena && out.reporte_json);
            if(modo==MODO_OBSERVABLES) {
                size_t id=0;bool encontrado=false;char alias[48];
                for(const SimSimbolo *s=tabla->primero;s;s=s->siguiente_orden,++id)
                    if(!strcmp(s->nombre,"basura")) {snprintf(alias,sizeof alias,"__bal_v%zu",id);encontrado=true;break;}
                assert(encontrado && !contiene(&out.optimizada,alias));
                bool auxiliar=false,llamada=false;
                for(size_t j=0;j<out.optimizada.cantidad;++j) {
                    const IrInstr *x=&out.optimizada.codigo[j];
                    if(x->op==IR_FUNC && x->dst && !strcmp(x->dst,"auxiliar"))auxiliar=true;
                    if(x->op==IR_CALL && x->a && !strcmp(x->a,"auxiliar"))llamada=true;
                }
                assert(auxiliar && llamada);
            }
            ir_vliw_programa_liberar(&out);exito=1;break;
        }
        assert(!out.cadena && !out.reporte_json && !out.original.codigo &&
               !out.optimizada.codigo && !out.spilling.codigo);
    }
    assert(exito);return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2 || argc==3);
    if(argc==3) {
        FILE *diag=tmpfile();assert(diag);
        int modo=MODO_NORMAL;
        if(!strcmp(argv[1],"recursos_programa"))modo=MODO_RECURSOS;
        else if(!strcmp(argv[1],"observables"))modo=MODO_OBSERVABLES;
        else if(!strcmp(argv[1],"division_cero"))modo=MODO_DIVISION;
        else if(!strcmp(argv[1],"exponente_negativo"))modo=MODO_EXPONENTE;
        else assert(!strcmp(argv[1],"positivas"));
        int rc=sem_visitar_proyecto(argv[2],diag,prueba_programa,&modo);
        fclose(diag);return rc;
    }
    IrPrograma p=entrada(),q={0};IrRenombramiento r={0};
    if(!strcmp(argv[1],"recursos")){
        int exito=0;for(long i=0;i<200;++i){cuenta=i;int rc=ir_renombrar_local(&p,&q,elegible,NULL,&r);cuenta=-1;
            assert(interpretar(&p)==8);if(!rc){printf("fallos_reserva_renombramiento=%ld\n",i);exito=1;ir_liberar(&q);break;}assert(!q.codigo && !q.bloques && !q.cantidad);
        }assert(exito);
    }else if(!strcmp(argv[1],"raices")){
        IrPrograma a={0};a.capacidad=4;a.codigo=calloc(4,sizeof *a.codigo);assert(a.codigo);
        emitir(&a,IR_FUNC,"main",NULL,NULL);emitir(&a,IR_COPY,"salida","37",NULL);emitir(&a,IR_END_FUNC,"main",NULL,NULL);assert(!ir_bloques_basicos(&a));
        DceResultado d={0};const char *raices[]={"salida"};assert(!dce_eliminar_con_observables(&a,&d,raices,1));assert(a.cantidad==3);dce_liberar_resultado(&d);
        assert(!dce_eliminar(&a,&d));assert(a.cantidad==2);dce_liberar_resultado(&d);ir_liberar(&a);
    }else if(!strcmp(argv[1],"rechazos")){
        assert(ir_renombrar_local(NULL,&q,elegible,NULL,&r)==2);
        assert(ir_renombrar_local(&p,&p,elegible,NULL,&r)==2);
        assert(ir_renombrar_local(&p,&q,NULL,NULL,&r)==2);
        IrPrograma falso={.cantidad=4093,.codigo=p.codigo};assert(ir_renombrar_local(&falso,&q,elegible,NULL,&r)==1);
        assert(!q.codigo);assert(interpretar(&p)==8);
    }else{
        assert(!ir_renombrar_local(&p,&q,elegible,NULL,&r));assert(r.versiones==6);assert(r.escrituras_publicadas==4);assert(r.waw_eliminadas==2);assert(r.war_eliminadas==2);
        assert(interpretar(&p)==8 && interpretar(&q)==8);
        assert(strcmp(q.codigo[1].dst,q.codigo[4].dst));
        assert(!strcmp(q.codigo[3].a,q.codigo[1].dst));
        assert(!strcmp(q.codigo[5].a,q.codigo[3].dst));
        assert(!q.temporales || q.codigo[1].dst[0]=='$');ir_liberar(&q);
    }
    ir_liberar(&p);return 0;
}
