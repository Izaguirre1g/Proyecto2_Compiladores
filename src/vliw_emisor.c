/* Conserva slots, NOP y etiquetas de protección de S7. La única sustitución
 * textual es el destino directo BRU; CB resuelve y valida el desplazamiento. */
#include "vliw_emisor.h"
#include "codificacion_binaria.h"
#include "sim_decode.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *texto; size_t longitud, capacidad; } Cadena;
typedef enum { FORMA_NORMAL, FORMA_COND, FORMA_SYE, FORMA_SYER, FORMA_RET } Forma;

static VliwEmisorEstado error(VliwEmisorDiagnostico *d, VliwEmisorEstado s,
                               size_t i, size_t b, const char *mensaje)
{
    if (d) {
        d->estado = s; d->instruccion = i; d->bloque = b;
        snprintf(d->mensaje,sizeof(d->mensaje),"%s",mensaje);
    }
    return s;
}
#define FALLAR(s,i,b,m) error(d,s,i,b,m)
/* La reserva geométrica no publica el buffer. Se comprueba el tamaño antes
 * de sumar el terminador; realloc fallido conserva el bloque liberable. */
static bool agregar(Cadena *c, const char *s, size_t n)
{
    if (n > SIZE_MAX-c->longitud-1) return false;
    size_t requerido = c->longitud+n+1;
    if (requerido > c->capacidad) {
        size_t nueva = c->capacidad ? c->capacidad : 128;
        while (nueva < requerido) nueva = nueva > SIZE_MAX/2 ? requerido : nueva*2;
        char *p = realloc(c->texto,nueva);
        if (!p) return false;
        c->texto = p; c->capacidad = nueva;
    }
    memcpy(c->texto+c->longitud,s,n); c->longitud += n; c->texto[c->longitud] = '\0';
    return true;
}
static bool literal(Cadena *c,const char *s) { return agregar(c,s,strlen(s)); }
static void etiqueta(char nombre[64],size_t b)
{
    /* Prefijo reservado interno. Ningún texto puede declarar etiquetas: los
     * índices únicos de bloque garantizan nombres únicos sin colisiones. */
    (void)snprintf(nombre,64,"__vliw_b%zu",b);
}
static char *recortar(char *s)
{
    while (*s==' ' || *s=='\t') ++s;
    size_t n = strlen(s);
    while (n && (s[n-1]==' ' || s[n-1]=='\t')) s[--n]='\0';
    return s;
}
static bool token_destino(const char *s)
{
    /* Solo valida la forma del operando que se DESCARTARÁ. No obtiene un PC,
     * no convierte enteros ni infiere bloques desde el desplazamiento viejo. */
    if (isalpha((unsigned char)*s) || *s=='_') {
        for (++s; *s; ++s) if (!isalnum((unsigned char)*s) && *s!='_') return false;
        return true;
    }
    if (*s=='+' || *s=='-') ++s;
    int base=10;
    if (s[0]=='0' && (s[1]=='x' || s[1]=='X')) { base=16; s+=2; }
    else if (s[0]=='0' && (s[1]=='b' || s[1]=='B')) { base=2; s+=2; }
    if (!*s) return false;
    for (; *s; ++s) {
        if (base==16 ? !isxdigit((unsigned char)*s) : base==2 ? (*s!='0' && *s!='1') : !isdigit((unsigned char)*s)) return false;
    }
    return true;
}
static VliwEmisorEstado validar_texto(const VliwInstruccion *in,size_t i,
                                     Forma *forma,VliwEmisorDiagnostico *d)
{
    const char *s=in->texto;
    if (!s || !*s) return FALLAR(VLIW_EM_TEXTO,i,in->bloque,"falta texto de instrucción");
    size_t n=0;
    for (; n<160 && s[n]; ++n) {
        unsigned char c=(unsigned char)s[n];
        if ((c<32 && c!='\t') || c>126 || strchr("|:#;/\\",c))
            return FALLAR(VLIW_EM_TEXTO,i,in->bloque,"texto contiene separadores, etiquetas, comentarios o caracteres de control");
    }
    if (n==160) return FALLAR(VLIW_EM_TEXTO,i,in->bloque,"texto excede el límite del codificador");
    char copia[160]; memcpy(copia,s,n+1);
    char *inicio=recortar(copia);
    if (!*inicio) return FALLAR(VLIW_EM_TEXTO,i,in->bloque,"texto vacío o solo espacios");
    *forma=FORMA_NORMAL;
    if (in->control==VLIW_CONTROL_NINGUNO) return VLIW_EM_OK;
    /* Reconocimiento limitado a las formas BRU reales. El resto de la ISA
     * se deja al parser existente, sin duplicar su gramática ni sus tablas. */
    char *resto=inicio+strcspn(inicio," \t");
    if (*resto) *resto++='\0';
    for (char *p=inicio; *p; ++p) *p=(char)tolower((unsigned char)*p);
    unsigned esperados;
    if (!strcmp(inicio,"sye")) { *forma=FORMA_SYE; esperados=2; }
    else if (!strcmp(inicio,"syer")) { *forma=FORMA_SYER; esperados=3; }
    else if (!strcmp(inicio,"ret")) { *forma=FORMA_RET; esperados=0; }
    else if (!strcmp(inicio,"igualsi") || !strcmp(inicio,"igualno") ||
             !strcmp(inicio,"menora") || !strcmp(inicio,"mayoroigual")) { *forma=FORMA_COND; esperados=3; }
    else return FALLAR(VLIW_EM_TEXTO,i,in->bloque,"forma BRU no reconocida");
    resto=recortar(resto); unsigned cantidad=0; char *ultimo=NULL;
    if (*resto) for (char *p=resto;;) {
        char *coma=strchr(p,','); if (coma) *coma='\0';
        ultimo=recortar(p);
        if (!*ultimo) return FALLAR(VLIW_EM_TEXTO,i,in->bloque,"operando BRU vacío");
        ++cantidad;
        if (!coma) break;
        p=coma+1;
    }
    if (cantidad!=esperados || ((*forma==FORMA_COND || *forma==FORMA_SYE) && !token_destino(ultimo)))
        return FALLAR(VLIW_EM_TEXTO,i,in->bloque,"operandos BRU malformados");
    return VLIW_EM_OK;
}
static bool contiene(const VliwBloque *b,size_t destino)
{
    for (size_t j=0;j<b->cantidad_sucesores;++j) if (b->sucesores[j]==destino) return true;
    return false;
}
/* El CFG se interpreta como conjunto: tomado y caída pueden coincidir,
 * pero el orden de sus elementos nunca decide cuál ruta se toma. */
static bool cfg_exacto(const VliwBloque *b,size_t tomado,size_t caida)
{
    size_t cantidad=1+(size_t)(caida!=VLIW_VACIO && caida!=tomado);
    return b->cantidad_sucesores==cantidad && contiene(b,tomado) &&
        (caida==VLIW_VACIO || contiene(b,caida));
}
static VliwEmisorEstado validar_destino(const VliwEntrada *e,size_t i,Forma f,
                                       const VliwDestino *t,VliwEmisorDiagnostico *d)
{
    const VliwInstruccion *in=&e->instrucciones[i];size_t b=in->bloque;
    const VliwBloque *bb=&e->bloques[b];
    size_t siguiente=b+1<e->cantidad_bloques ? b+1 : VLIW_VACIO;
    if ((unsigned)t->tipo>VLIW_DEST_NO_RESUELTO)
        return FALLAR(VLIW_EM_DESTINO,i,b,"tipo de destino inválido");
    if (t->tipo==VLIW_DEST_NO_RESUELTO)
        return FALLAR(VLIW_EM_NO_RESUELTO,i,b,"destino pendiente de resolución externa; S8-A no enlaza módulos");
    if (t->tipo==VLIW_DEST_DIRECTO || t->tipo==VLIW_DEST_CONDICIONAL) {
        bool cond=t->tipo==VLIW_DEST_CONDICIONAL;
        if ((cond && (f!=FORMA_COND || in->control!=VLIW_CONTROL_SALTO)) ||
            (!cond && (f!=FORMA_SYE || in->control==VLIW_CONTROL_RETORNO)) ||
            t->tomado>=e->cantidad_bloques)
            return FALLAR(VLIW_EM_DESTINO,i,b,"forma o bloque destino directo inconsistente");
        size_t caida=(cond || in->control==VLIW_CONTROL_LLAMADA) ? siguiente : VLIW_VACIO;
        if (t->caida!=caida || !cfg_exacto(bb,t->tomado,caida))
            return FALLAR(VLIW_EM_DESTINO,i,b,"destino tomado/caída no coincide con el CFG");
    } else {
        bool retorno=t->tipo==VLIW_DEST_RETORNO;
        if ((retorno && (in->control!=VLIW_CONTROL_RETORNO || (f!=FORMA_SYER && f!=FORMA_RET))) ||
            (!retorno && (f!=FORMA_SYER || in->control==VLIW_CONTROL_RETORNO)) || t->tomado!=VLIW_VACIO)
            return FALLAR(VLIW_EM_DESTINO,i,b,"transferencia indirecta/retorno inconsistente");
        size_t caida=in->control==VLIW_CONTROL_LLAMADA ? siguiente : VLIW_VACIO;
        if (t->caida!=caida || (caida!=VLIW_VACIO && !contiene(bb,caida)))
            return FALLAR(VLIW_EM_DESTINO,i,b,"continuación de llamada indirecta inconsistente");
        /* No se inventa un destino estático para syer/ret. El CFG puede
         * enumerar destinos posibles; su dirección dinámica pertenece al programa. */
    }
    return VLIW_EM_OK;
}
static uint64_t registro(uint8_t r) { return r>0 && r<32 ? UINT64_C(1)<<r : 0; }
static VliwEmisorEstado verificar_codigo(const VliwEntrada *e,const VliwPlan *p,
                                         const CbPrograma *cb,VliwEmisorDiagnostico *d)
{
    if (cb->cantidad!=p->cantidad_bundles)
        return FALLAR(VLIW_EM_CODIFICADOR,VLIW_VACIO,VLIW_VACIO,"el codificador alteró la cantidad de bundles");
    for (size_t q=0;q<p->cantidad_bundles;++q) for (unsigned s=0;s<4;++s) {
        size_t indice=p->bundles[q].instrucciones[s]; uint32_t palabra=cb->bundles[q].slots[s];
        if (indice==VLIW_VACIO) {
            if (palabra) return FALLAR(VLIW_EM_CODIFICADOR,indice,p->bundles[q].bloque,"slot vacío no codificado como NOP");
            continue;
        }
        SimInstruccion real;
        if (sim_decode_palabra(palabra,s,&real)!=SIM_DECODE_OK)
            return FALLAR(VLIW_EM_TEXTO,indice,p->bundles[q].bloque,"la entrada debe representar una operación no-NOP válida de S2");
        const VliwInstruccion *in=&e->instrucciones[indice];
        if (s==VLIW_SLOT_BRU && in->control==VLIW_CONTROL_NINGUNO)
            return FALLAR(VLIW_EM_METADATOS,indice,in->bloque,"BRU real sin metadatos de transferencia");
        uint64_t lee=registro(real.rs1)|registro(real.rs2), escribe=registro(real.rd)|registro(real.rd2);
        unsigned efectos=0;
        switch(real.operacion) {
        case SIM_OP_CARGAI: case SIM_OP_CARGABAI: efectos=VLIW_EF_LEE_MEMORIA; break;
        case SIM_OP_GUARDAP: case SIM_OP_GUARDAB: efectos=VLIW_EF_ESCRIBE_MEMORIA; break;
        case SIM_OP_FSL: case SIM_OP_FSLI: efectos=VLIW_EF_EXCEPCION; break;
        case SIM_OP_ELL: efectos=VLIW_EF_BOVEDA|VLIW_EF_EXCEPCION; break;
        case SIM_OP_SETPWD: efectos=VLIW_EF_BOVEDA|VLIW_EF_ESTADO|VLIW_EF_EXCEPCION; break;
        case SIM_OP_VCR: efectos=VLIW_EF_LEE_MEMORIA|VLIW_EF_ESTADO; break;
        case SIM_OP_CAMCON: efectos=VLIW_EF_LEE_MEMORIA|VLIW_EF_ESCRIBE_MEMORIA|VLIW_EF_BOVEDA|VLIW_EF_EXCEPCION; break;
        default: break;
        }
        /* S2 obtiene los operandos de las palabras reales. Esta comprobación
         * no analiza dependencias: impide emitir un texto distinto del que
         * describían las máscaras o efectos aprobados por S7. */
        if ((in->lee & ~UINT64_C(1))!=lee || (in->escribe & ~UINT64_C(1))!=escribe || (in->efectos & efectos)!=efectos)
            return FALLAR(VLIW_EM_METADATOS,indice,in->bloque,"texto y metadatos físicos/efectos no coinciden con S2");
    }
    return VLIW_EM_OK;
}
void vliw_emisor_liberar(char **s) { if (s) { free(*s); *s=NULL; } }

VliwEmisorEstado vliw_emitir(const VliwEntrada *e,const VliwPlan *p,
                             const VliwDestinos *destinos,char **salida,VliwEmisorDiagnostico *d)
{
    if (d) *d=(VliwEmisorDiagnostico){.instruccion=VLIW_VACIO,.bloque=VLIW_VACIO};
    if (!e || !p || !salida || *salida)
        return FALLAR(VLIW_EM_ARGUMENTO,VLIW_VACIO,VLIW_VACIO,"entrada, plan y salida NULL inicial requeridos");
    VliwDiagnostico sd;
    VliwEstado v=vliw_verificar(e,p,&sd);
    if (d) d->estado_plan=v;
    if (v!=VLIW_OK) return FALLAR(v==VLIW_MEMORIA?VLIW_EM_MEMORIA:VLIW_EM_PLAN,sd.instruccion,VLIW_VACIO,sd.mensaje);
    size_t n=e->cantidad_instrucciones, nt=destinos ? destinos->cantidad : 0;
    if (nt>n || (nt && !destinos->items))
        return FALLAR(VLIW_EM_DESTINO,VLIW_VACIO,VLIW_VACIO,"tabla de destinos inválida");
    /* vliw_verificar ya acotó cantidades, índices, slots, final de bloque y
     * guardas N+3. Este mapa adicional exige una única descripción por BRU. */
    size_t *por_indice=n ? malloc(n*sizeof(size_t)) : NULL;
    Forma *formas=n ? calloc(n,sizeof(Forma)) : NULL;
    Cadena c={0};CbPrograma cb={0};VliwEmisorEstado estado=VLIW_EM_OK;
    if (n && (!por_indice || !formas)) {
        estado=FALLAR(VLIW_EM_MEMORIA,VLIW_VACIO,VLIW_VACIO,"sin memoria para validar destinos");goto terminar;
    }
    for(size_t i=0;i<n;++i)por_indice[i]=VLIW_VACIO;
    for(size_t t=0;t<nt;++t) {
        size_t i=destinos->items[t].instruccion;
        if(i>=n || e->instrucciones[i].control==VLIW_CONTROL_NINGUNO || por_indice[i]!=VLIW_VACIO) {
            estado=FALLAR(VLIW_EM_DESTINO,i,VLIW_VACIO,"destino duplicado o asociado a una instrucción inexistente/no BRU");goto terminar;
        }
        por_indice[i]=t;
    }
    for(size_t b=0;b<e->cantidad_bloques;++b)for(size_t i=0;i<e->bloques[b].cantidad_sucesores;++i)
        for(size_t j=0;j<i;++j)if(e->bloques[b].sucesores[i]==e->bloques[b].sucesores[j]) {
            estado=FALLAR(VLIW_EM_DESTINO,VLIW_VACIO,b,"sucesor CFG duplicado");goto terminar;
        }
    for(size_t i=0;i<n;++i) {
        estado=validar_texto(&e->instrucciones[i],i,&formas[i],d);if(estado)goto terminar;
        if(e->instrucciones[i].control!=VLIW_CONTROL_NINGUNO) {
            if(por_indice[i]==VLIW_VACIO) {estado=FALLAR(VLIW_EM_DESTINO,i,e->instrucciones[i].bloque,"falta destino estructurado de transferencia");goto terminar;}
            estado=validar_destino(e,i,formas[i],&destinos->items[por_indice[i]],d);if(estado)goto terminar;
        }
    }
    /* El recorrido es por bundle y slot del plan, nunca por id externo.
     * La etiqueta precede al guard inicial; no consume una posición IMEM. */
    size_t bloque=0;
    for(size_t q=0;q<p->cantidad_bundles;++q) {
        if(bloque<e->cantidad_bloques && p->inicio_por_bloque[bloque]==q) {
            char nombre[64];etiqueta(nombre,bloque++);
            if(!literal(&c,nombre)||!literal(&c,":\n"))goto memoria;
        }
        for(unsigned s=0;s<4;++s) {
            if(s && !literal(&c," | "))goto memoria;
            size_t i=p->bundles[q].instrucciones[s];
            if(i==VLIW_VACIO) {if(!literal(&c,"nop"))goto memoria;continue;}
            const char *texto=e->instrucciones[i].texto;
            if(formas[i]==FORMA_COND || formas[i]==FORMA_SYE) {
                const char *coma=strrchr(texto,',');char nombre[64];
                etiqueta(nombre,destinos->items[por_indice[i]].tomado);
                if(!agregar(&c,texto,(size_t)(coma-texto)+1)||!literal(&c,nombre))goto memoria;
            } else if(!literal(&c,texto))goto memoria;
        }
        if(!literal(&c,"\n"))goto memoria;
    }
    if(!literal(&c,""))goto memoria;
    if(n) {
        /* Se valida el texto COMPLETO con el codificador real, incluidos
         * rangos y caminos RAW. No se implementa otro cálculo de inmediatos.
         * El tmpfile es privado y nunca contiene una salida publicada parcial. */
        FILE *diag=tmpfile();char detalle[256]={0};
        if(!diag) {estado=FALLAR(VLIW_EM_IO,VLIW_VACIO,VLIW_VACIO,"no se pudo crear el flujo privado de diagnóstico");goto terminar;}
        int resultado=cb_ensamblar(c.texto,&cb,diag);
        bool io=fflush(diag)!=0 || fseek(diag,0,SEEK_SET)!=0;
        if(!io) { if(!fgets(detalle,sizeof(detalle),diag))detalle[0]='\0';io=ferror(diag)!=0; }
        if(fclose(diag)!=0)io=true;
        if(io) {estado=FALLAR(VLIW_EM_IO,VLIW_VACIO,VLIW_VACIO,"fallo de escritura/lectura/cierre del diagnóstico privado");goto terminar;}
        if(resultado) {
            VliwEmisorEstado motivo=strstr(detalle,"sin memoria")?VLIW_EM_MEMORIA:
                strstr(detalle,"salto fuera de rango")?VLIW_EM_RANGO:VLIW_EM_CODIFICADOR;
            estado=FALLAR(motivo,VLIW_VACIO,VLIW_VACIO,motivo==VLIW_EM_RANGO ? "salto fuera de rango; requiere relajación posterior" : "codificador rechazó el ensamblador");
            if(d) {size_t len=strlen(d->mensaje);snprintf(d->mensaje+len,sizeof(d->mensaje)-len,": %s",detalle);}
            goto terminar;
        }
        estado=verificar_codigo(e,p,&cb,d);if(estado)goto terminar;
    }
    /* Único punto de publicación: solo después de validar el texto completo
     * y contrastar sus palabras con los metadatos mediante S2. */
    *salida=c.texto;c.texto=NULL;
    estado=FALLAR(VLIW_EM_OK,VLIW_VACIO,VLIW_VACIO,"ensamblador completo y validado");goto terminar;
memoria:
    estado=FALLAR(VLIW_EM_MEMORIA,VLIW_VACIO,VLIW_VACIO,"sin memoria para construir el ensamblador");
terminar:
    cb_liberar(&cb);free(c.texto);free(por_indice);free(formas);return estado;
}
