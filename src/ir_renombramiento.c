/* Versiona definiciones escalares dentro de regiones rectilíneas puras.
 * Las lecturas posteriores usan su versión; al cerrar la región se publican
 * las últimas versiones en los objetos BAL. Las publicaciones mantienen los
 * observables y permiten a reorder separar dependencias falsas anteriores.
 * No transforma registros físicos, referencias ni valores subpalabra: el
 * callback debe acreditar identidad, localidad y ancho int32. */
#include "ir_renombramiento.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

enum { RN_MAX = 4092 };
typedef struct { const char *nombre; char version[48]; size_t linea; } Version;

/* Cada campo copiado pasa a ser propiedad de la IR privada. Ante un fallo
 * se conserva el parcial únicamente para liberarlo, nunca para publicarlo. */
static int agregar(IrPrograma *p, const IrInstr *x)
{
    if (p->cantidad == p->capacidad) return 0;
    IrInstr *d=&p->codigo[p->cantidad++];
    *d=(IrInstr){.op=x->op,.linea=x->linea};
    const char *campos[]={x->dst,x->a,x->b,x->operador};
    char **destinos[]={&d->dst,&d->a,&d->b,&d->operador};
    for(size_t i=0;i<4;++i) if(campos[i]) {
        size_t n=strlen(campos[i])+1;
        *destinos[i]=malloc(n); if(!*destinos[i])return 0;
        memcpy(*destinos[i],campos[i],n);
    }
    return 1;
}
/* Solo operaciones sin efectos ni fallos pueden retrasar sus escrituras.
 * Divisiones/potencias se excluyen aunque un literal pudiera probarlas seguras;
 * el criterio deliberadamente conserva las fronteras de error originales. */
static int pura(const IrInstr *x)
{
    if(x->op==IR_COPY || x->op==IR_UNARY)return 1;
    if(x->op!=IR_BIN || !x->operador)return 0;
    return strcmp(x->operador,"euclides") && strcmp(x->operador,"euler") &&
           strcmp(x->operador,"descartes");
}
/* El mapa presta la última versión de esta región; no cambia su propiedad. */
static const char *lectura(const char *s,const Version *v,size_t n)
{
    if(s)for(size_t i=0;i<n;++i)if(!strcmp(s,v[i].nombre))return v[i].version;
    return s;
}
/* Los objetos se publican antes de una frontera. No existen versiones que
 * crucen un salto, por lo que no hacen falta phi ni supuestos sobre rutas. */
static int publicar(IrPrograma *p,Version *v,size_t n,IrRenombramiento *r)
{
    for(size_t i=0;i<n;++i) {
        IrInstr copia={.op=IR_COPY,.dst=(char *)v[i].nombre,
                       .a=v[i].version,.linea=v[i].linea};
        if(!agregar(p,&copia))return 0;
        ++r->escrituras_publicadas;
    }
    return 1;
}
/* Construcción privada acotada a 4092 instrucciones; se libera cualquier
 * parcial en error y se publica solo después de reconstruir el CFG. */
int ir_renombrar_local(const IrPrograma *in,IrPrograma *out,
                     IrNombreRenombrable elegible,void *ctx,IrRenombramiento *res)
{
    if(!in || !out || !res || !elegible || out->codigo || out->bloques ||
       out->cantidad || (in->cantidad && !in->codigo))return 2;
    if(in->cantidad>RN_MAX || in->temporales>UINT_MAX-RN_MAX)return 1;
    IrPrograma p={0};IrRenombramiento r={0};size_t n=0;int estado=2;
    Version *v=calloc(in->cantidad?in->cantidad:1,sizeof *v);
    const char **leidos=calloc(2*in->cantidad+1,sizeof *leidos);size_t nl=0;
    p.capacidad=2*in->cantidad+1;p.temporales=in->temporales;p.etiquetas=in->etiquetas;
    p.codigo=calloc(p.capacidad,sizeof *p.codigo);
    if(!v || !leidos || !p.codigo)goto fin;
    for(size_t i=0;i<in->cantidad;++i) {
        const IrInstr *x=&in->codigo[i];
        if(!pura(x)) {
            if(!publicar(&p,v,n,&r))goto fin;
            n=0;nl=0;if(!agregar(&p,x))goto fin;
            continue;
        }
        IrInstr y=*x;
        y.a=(char *)lectura(x->a,v,n);y.b=(char *)lectura(x->b,v,n);
        size_t j=0;char nueva[48];
        if(x->dst && elegible(x->dst,ctx)) {
            for(;j<n;++j)if(!strcmp(x->dst,v[j].nombre))break;
            /* y puede leer la versión anterior de su propio destino. Por eso
             * copiamos la instrucción antes de actualizar el mapa de nombres. */
            snprintf(nueva,sizeof nueva,"$t%u",++p.temporales);y.dst=nueva;
            if(!agregar(&p,&y))goto fin;
            /* Estas cuentas describen dependencias falsas sobre el nombre
             * original, antes del mapeo físico. Cada versión tiene un único
             * escritor; las lecturas ya emitidas mantienen su valor anterior. */
            if(j<n)++r.waw_eliminadas;
            for(size_t k=0;k<nl;++k)if(!strcmp(leidos[k],x->dst)){++r.war_eliminadas;break;}
            if(j==n){v[n].nombre=x->dst;++n;}
            memcpy(v[j].version,nueva,strlen(nueva)+1);v[j].linea=x->linea;
            ++r.versiones;
        } else if(!agregar(&p,&y))goto fin;
        if(x->a && elegible(x->a,ctx))leidos[nl++]=x->a;
        if(x->b && elegible(x->b,ctx))leidos[nl++]=x->b;
    }
    if(!publicar(&p,v,n,&r) || ir_bloques_basicos(&p))goto fin;
    *out=p;memset(&p,0,sizeof p);*res=r;estado=0;
fin:
    free(leidos);free(v);ir_liberar(&p);return estado;
}
