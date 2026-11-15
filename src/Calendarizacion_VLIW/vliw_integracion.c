/* Comprueba el contrato físico de integración sin producir instrucciones ni planes.
 * Consume VliwEntrada/VliwDestinos prestados; CB y S2 reconocen la ISA existente.
 * Las reservas y contextos locales son privados: solo se devuelve un diagnóstico.
 * La coherencia comprobada no acredita semántica BAL, direcciones dinámicas o ABI. */
#include "vliw_integracion.h"
#include "codificacion_binaria.h"
#include "sim_decode.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MEM (VLIW_EF_LEE_MEMORIA | VLIW_EF_ESCRIBE_MEMORIA)
#define GLOBAL (VLIW_EF_BOVEDA | VLIW_EF_ESTADO | VLIW_EF_EXCEPCION | VLIW_EF_DESCONOCIDOS | VLIW_EF_BARRERA)
#define EFECTOS (MEM | GLOBAL)

/* Publica el primer rechazo con índices del contrato, o el éxito final. No
 * reserva memoria; el mensaje es literal y queda prestado al diagnóstico. */
static VliwIntegracionEstado informar(VliwIntegracionDiagnostico *d,
    VliwIntegracionEstado estado, size_t i, size_t a, size_t b, size_t t,
    const char *mensaje)
{
    if (d) *d = (VliwIntegracionDiagnostico){estado,i,a,b,t,mensaje};
    return estado;
}
#define ERROR(s,i,a,b,t,m) informar(d,s,i,a,b,t,m)
#define VAC VLIW_VACIO

/* Obtiene el bit de un GPR real, sin x0 ni el marcador de registro ausente S2.
 * No asigna registros: se usa solo para contrastar operandos ya decodificados. */
static uint64_t bit_registro(unsigned r)
{
    return r > 0 && r < 32 ? UINT64_C(1) << r : 0;
}

/* Acepta la forma del operando directo que S8-A sustituirá, sin interpretar su
 * valor antiguo como PC. La cadena privada ya fue recortada y es ASCII. */
static bool destino_lexico(const char *s)
{
    if (isalpha((unsigned char)*s) || *s == '_') {
        for (++s; *s; ++s)
            if (!isalnum((unsigned char)*s) && *s != '_') return false;
        return true;
    }
    if (*s == '+' || *s == '-') ++s;
    unsigned base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base=16; s+=2; }
    else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) { base=2; s+=2; }
    if (!*s) return false;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (base == 16 ? !isxdigit(c) : base == 2 ? c != '0' && c != '1' : !isdigit(c))
            return false;
    }
    return true;
}

/* Reconoce una operación mediante CB/S2 sobre un contexto privado de un slot.
 * Solo normaliza el destino directo que se reemplazará al emitir; no obtiene
 * destinos del texto ni publica ensamblador. Libera CB y el flujo en todo caso. */
static VliwIntegracionEstado reconocer(const VliwInstruccion *in, size_t i,
    SimInstruccion *real, VliwIntegracionDiagnostico *d)
{
    const char *s = in->texto;
    if (!s || !*s) return ERROR(VLIW_INT_ISA,i,VAC,in->bloque,VAC,"falta texto ISA no-NOP");
    size_t n = 0;
    // Impedimos que una operación introduzca otra línea, etiqueta o slot. El
    // límite es el mismo de S8-A; el llamador aporta una cadena accesible.
    for (; n < 160 && s[n]; ++n) {
        unsigned char c = (unsigned char)s[n];
        if ((c < 32 && c != '\t') || c > 126 || strchr("|:#;/\\",c))
            return ERROR(VLIW_INT_ISA,i,VAC,in->bloque,VAC,"texto ISA con separadores o caracteres inválidos");
    }
    if (n == 160) return ERROR(VLIW_INT_ISA,i,VAC,in->bloque,VAC,"texto ISA demasiado largo");
    char op[160]; memcpy(op,s,n+1);
    while (n && (op[n-1]==' ' || op[n-1]=='\t')) op[--n]='\0';
    char nombre[160]; size_t k=0, inicio=0;
    while (op[inicio]==' ' || op[inicio]=='\t') ++inicio;
    while (op[inicio+k] && op[inicio+k]!=' ' && op[inicio+k]!='\t') {
        nombre[k]=(char)tolower((unsigned char)op[inicio+k]); ++k;
    }
    nombre[k]='\0';
    bool directo = !strcmp(nombre,"sye") || !strcmp(nombre,"igualsi") ||
        !strcmp(nombre,"igualno") || !strcmp(nombre,"menora") || !strcmp(nombre,"mayoroigual");
    if (directo) {
        // Un destino lógico no es codificable sin layout. Se valida su forma y
        // se consulta el parser con offset cero, que no añade una reubicación.
        char *coma=strrchr(op,',');
        if (!coma) return ERROR(VLIW_INT_ISA,i,VAC,in->bloque,VAC,"falta operando de destino directo");
        char *ultimo=coma+1;
        while (*ultimo==' ' || *ultimo=='\t') ++ultimo;
        if (!destino_lexico(ultimo))
            return ERROR(VLIW_INT_ISA,i,VAC,in->bloque,VAC,"destino textual malformado");
        coma[1]='0'; coma[2]='\0';
    }
    const char *slots[4]={"nop","nop","nop","nop"}; slots[in->slot]=op;
    char contexto[256];
    (void)snprintf(contexto,sizeof contexto,"%s | %s | %s | %s\n",slots[0],slots[1],slots[2],slots[3]);
    FILE *diag=tmpfile();
    if (!diag) return ERROR(VLIW_INT_IO,i,VAC,in->bloque,VAC,"no se pudo crear diagnóstico privado CB");
    CbPrograma cb={0};
    int resultado=cb_ensamblar(contexto,&cb,diag);
    char detalle[256]={0};
    bool io=fflush(diag)!=0 || fseek(diag,0,SEEK_SET)!=0;
    if (!io && !fgets(detalle,sizeof detalle,diag)) io=ferror(diag)!=0;
    if (fclose(diag)!=0) io=true;
    VliwIntegracionEstado estado=VLIW_INT_OK;
    // CB no expone un código específico de memoria. Conservamos su convenio
    // actual, como S8-A; un mensaje desconocido se rechaza, nunca se acepta.
    if (io) estado=ERROR(VLIW_INT_IO,i,VAC,in->bloque,VAC,"falló el diagnóstico privado CB");
    else if (resultado) estado=ERROR(strstr(detalle,"sin memoria") ? VLIW_INT_MEMORIA : VLIW_INT_ISA,
        i,VAC,in->bloque,VAC,"CB rechazó la operación o no pudo reservar sus recursos");
    else if (cb.cantidad!=1 || sim_decode_palabra(cb.bundles[0].slots[in->slot],in->slot,real)!=SIM_DECODE_OK)
        estado=ERROR(VLIW_INT_ISA,i,VAC,in->bloque,VAC,"S2 exige una operación ISA válida y distinta de NOP");
    cb_liberar(&cb);
    return estado;
}

/* Contrasta operandos y efectos mínimos con la instrucción real S2. No devuelve
 * metadatos nuevos: rechaza los recibidos si describen otra operación. */
static VliwIntegracionEstado fisica(const VliwInstruccion *in, size_t i,
    const SimInstruccion *r, VliwIntegracionDiagnostico *d)
{
    uint64_t lee=bit_registro(r->rs1)|bit_registro(r->rs2);
    uint64_t escribe=bit_registro(r->rd)|bit_registro(r->rd2);
    if ((in->lee & ~UINT64_C(1))!=lee || (in->escribe & ~UINT64_C(1))!=escribe)
        return ERROR(VLIW_INT_METADATOS,i,VAC,in->bloque,VAC,"máscaras no coinciden con los operandos físicos S2");
    unsigned minimo=0;
    // Estos mínimos son los que ya comprueba S8-A. Efectos adicionales pueden
    // ser conservadores, pero nunca justifican omitir un efecto del opcode.
    switch (r->operacion) {
    case SIM_OP_CARGAI: case SIM_OP_CARGABAI: minimo=VLIW_EF_LEE_MEMORIA; break;
    case SIM_OP_GUARDAP: case SIM_OP_GUARDAB: minimo=VLIW_EF_ESCRIBE_MEMORIA; break;
    case SIM_OP_FSL: case SIM_OP_FSLI: minimo=VLIW_EF_EXCEPCION; break;
    case SIM_OP_ELL: minimo=VLIW_EF_BOVEDA|VLIW_EF_EXCEPCION; break;
    case SIM_OP_SETPWD: minimo=VLIW_EF_BOVEDA|VLIW_EF_ESTADO|VLIW_EF_EXCEPCION; break;
    case SIM_OP_VCR: minimo=VLIW_EF_LEE_MEMORIA|VLIW_EF_ESTADO; break;
    case SIM_OP_CAMCON: minimo=MEM|VLIW_EF_BOVEDA|VLIW_EF_EXCEPCION; break;
    default: break;
    }
    if ((in->efectos & minimo)!=minimo ||
        (in->slot==VLIW_SLOT_LSU && !(in->efectos & MEM)) ||
        ((in->efectos & MEM) && in->slot!=VLIW_SLOT_LSU && in->slot!=VLIW_SLOT_CRIPTO) ||
        ((in->efectos & (VLIW_EF_BOVEDA|VLIW_EF_ESTADO)) && in->slot!=VLIW_SLOT_CRIPTO) ||
        (in->control==VLIW_CONTROL_LLAMADA && !(in->efectos & VLIW_EF_DESCONOCIDOS)))
        return ERROR(VLIW_INT_EFECTOS,i,VAC,in->bloque,VAC,"faltan efectos reales o son incompatibles con la unidad");
    return VLIW_INT_OK;
}

/* Busca una arista CFG en el arreglo prestado. No interpreta el orden como
 * tomado/caída: los sucesores representan un conjunto de bloques. */
static bool contiene(const VliwBloque *b, size_t destino)
{
    for (size_t j=0;j<b->cantidad_sucesores;++j)
        if (b->sucesores[j]==destino) return true;
    return false;
}

/* Comprueba una transferencia con la tabla explícita de S8-A y la forma S2.
 * Solo las directas tienen tomado estático; no calcula direcciones indirectas. */
static VliwIntegracionEstado transferencia(const VliwEntrada *e, size_t i,
    const SimInstruccion *r, const VliwDestino *t, size_t it, VliwIntegracionDiagnostico *d)
{
    const VliwInstruccion *in=&e->instrucciones[i]; const VliwBloque *b=&e->bloques[in->bloque];
    if (!t || (unsigned)t->tipo>VLIW_DEST_NO_RESUELTO)
        return ERROR(VLIW_INT_DESTINO,i,VAC,in->bloque,it,"falta destino único o su tipo es inválido");
    if (t->tipo==VLIW_DEST_NO_RESUELTO)
        return ERROR(VLIW_INT_NO_RESUELTO,i,VAC,in->bloque,it,"el destino requiere resolución externa");
    bool cond=t->tipo==VLIW_DEST_CONDICIONAL, directo=t->tipo==VLIW_DEST_DIRECTO;
    size_t siguiente=in->bloque+1<e->cantidad_bloques ? in->bloque+1 : VAC;
    size_t caida=(cond || in->control==VLIW_CONTROL_LLAMADA) ? siguiente : VAC;
    if (t->caida!=caida)
        return ERROR(VLIW_INT_DESTINO,i,VAC,in->bloque,it,"continuación o caída incompatible");
    if (cond || directo) {
        if ((cond && (r->salto!=SIM_SALTO_COND_PC || in->control!=VLIW_CONTROL_SALTO)) ||
            (directo && (r->operacion!=SIM_OP_SYE || in->control==VLIW_CONTROL_RETORNO)) ||
            t->tomado>=e->cantidad_bloques)
            return ERROR(VLIW_INT_DESTINO,i,VAC,in->bloque,it,"forma o destino directo incompatible");
        // El conjunto exacto evita que un CFG invente una tercera ruta. Tomado
        // y caída pueden coincidir sin duplicar sucesores.
        size_t cantidad=1+(caida!=VAC && caida!=t->tomado);
        if (b->cantidad_sucesores!=cantidad || !contiene(b,t->tomado) ||
            (caida!=VAC && !contiene(b,caida)))
            return ERROR(VLIW_INT_DESTINO,i,VAC,in->bloque,it,"destinos directos no coinciden con el CFG");
    } else {
        bool retorno=t->tipo==VLIW_DEST_RETORNO;
        if (r->operacion!=SIM_OP_SYER || t->tomado!=VAC ||
            retorno!=(in->control==VLIW_CONTROL_RETORNO) ||
            (caida!=VAC && !contiene(b,caida)))
            return ERROR(VLIW_INT_DESTINO,i,VAC,in->bloque,it,"indirecto/retorno o continuación incompatible");
    }
    return VLIW_INT_OK;
}

/* Identifica las barreras que S7 aísla conservadoramente: CRIPTO, estado,
 * excepciones, efectos desconocidos y límites de llamada/retorno. */
static bool barrera(const VliwInstruccion *in)
{
    return in->slot==VLIW_SLOT_CRIPTO || (in->efectos & GLOBAL) ||
        in->control==VLIW_CONTROL_LLAMADA || in->control==VLIW_CONTROL_RETORNO;
}

/* Valida el DAG externo y la cobertura de riesgos por caminos ponderados.
 * Las aristas crecen en posición original, por lo que ese orden ya es
 * topológico. La reserva temporal se libera también en el primer rechazo. */
static VliwIntegracionEstado dependencias(const VliwEntrada *e, VliwIntegracionDiagnostico *d)
{
    size_t n=e->cantidad_instrucciones,m=e->cantidad_dependencias;
    for (size_t a=0;a<m;++a) {
        const VliwDependencia *ar=&e->dependencias[a];
        if (ar->antes>=n || ar->despues>=n || ar->antes>=ar->despues ||
            (unsigned)ar->tipo>VLIW_DEP_BARRERA || !ar->distancia ||
            (ar->tipo==VLIW_DEP_RAW && ar->distancia<VLIW_RAW_DISTANCIA))
            return ERROR(VLIW_INT_DEPENDENCIA,ar->despues,a,VAC,VAC,"índice, orden DAG, tipo o distancia inválidos");
        if (ar->distancia>=VLIW_MAX_BUNDLES)
            return ERROR(VLIW_INT_CAPACIDAD,ar->despues,a,VAC,VAC,"distancia imposible dentro de IMEM ordinaria");
        const VliwInstruccion *u=&e->instrucciones[ar->antes],*v=&e->instrucciones[ar->despues];
        uint64_t raw=u->escribe & v->lee & ~UINT64_C(1);
        uint64_t war=u->lee & v->escribe & ~UINT64_C(1);
        uint64_t waw=u->escribe & v->escribe & ~UINT64_C(1);
        if ((ar->tipo==VLIW_DEP_RAW && !raw) || (ar->tipo==VLIW_DEP_WAR && !war) ||
            (ar->tipo==VLIW_DEP_WAW && !waw))
            return ERROR(VLIW_INT_DEPENDENCIA,ar->despues,a,VAC,VAC,"tipo de dependencia contradice las máscaras físicas");
    }
    size_t *dist=n ? malloc(n*sizeof *dist) : NULL;
    if (n && !dist) return ERROR(VLIW_INT_MEMORIA,VAC,VAC,VAC,VAC,"sin memoria para comprobar caminos del DAG");
    VliwIntegracionEstado estado=VLIW_INT_OK;
    for (size_t origen=0;origen<n && !estado;++origen) {
        for (size_t j=0;j<n;++j) dist[j]=VAC;
        dist[origen]=0;
        // Cada camino suma distancias mínimas. Saturar en 1023 evita overflow
        // y permite rechazar cadenas imposibles; no se calcula un plan aquí.
        for (size_t j=origen;j<n;++j) if (dist[j]!=VAC) {
            for (size_t a=0;a<m;++a) {
                const VliwDependencia *ar=&e->dependencias[a];
                if (ar->antes!=j) continue;
                size_t valor=dist[j]+ar->distancia;
                if (valor>=VLIW_MAX_BUNDLES) valor=VLIW_MAX_BUNDLES;
                if (dist[ar->despues]==VAC || valor>dist[ar->despues]) dist[ar->despues]=valor;
            }
        }
        for (size_t j=origen+1;j<n;++j) {
            const VliwInstruccion *u=&e->instrucciones[origen],*v=&e->instrucciones[j];
            uint64_t raw=u->escribe & v->lee & ~UINT64_C(1);
            uint64_t otros=((u->lee & v->escribe)|(u->escribe & v->escribe)) & ~UINT64_C(1);
            unsigned minimo=raw ? 3u : otros ? 1u : 0u;
            if ((u->efectos & MEM) && (v->efectos & MEM) && minimo<1) minimo=1;
            if ((u->control!=VLIW_CONTROL_NINGUNO || v->control!=VLIW_CONTROL_NINGUNO) && minimo<1) minimo=1;
            if (barrera(u) || barrera(v)) minimo=3;
            if (dist[j]!=VAC && dist[j]>=VLIW_MAX_BUNDLES) {
                estado=ERROR(VLIW_INT_CAPACIDAD,j,VAC,v->bloque,VAC,"camino del DAG no cabe en IMEM"); break;
            }
            if (minimo && (dist[j]==VAC || dist[j]<minimo)) {
                estado=ERROR(VLIW_INT_DEPENDENCIA,j,VAC,v->bloque,VAC,"falta un camino suficiente para un riesgo físico visible"); break;
            }
        }
    }
    free(dist);
    return estado;
}

/* Recorre el contrato por capas, antes de cada acceso dependiente. No corrige
 * entradas ni cambia metadatos_completos; todas las salidas son diagnósticos
 * por valor y los recursos adquiridos pertenecen solo a la llamada. */
VliwIntegracionEstado vliw_integracion_validar(const VliwEntrada *e,
    const VliwDestinos *destinos, VliwIntegracionDiagnostico *d)
{
    if (!e) return ERROR(VLIW_INT_ARGUMENTO,VAC,VAC,VAC,VAC,"entrada requerida");
    size_t n=e->cantidad_instrucciones, nb=e->cantidad_bloques, m=e->cantidad_dependencias;
    size_t nt=destinos ? destinos->cantidad : 0;
    // Acotamos tamaños antes de productos o recorridos. Los punteros no se
    // pueden autenticar en C: el llamador garantiza las regiones accesibles.
    if (n>VLIW_MAX_BUNDLES*(size_t)VLIW_SLOTS)
        return ERROR(VLIW_INT_CAPACIDAD,VAC,VAC,VAC,VAC,"demasiadas operaciones para IMEM");
    if (m>SIZE_MAX/sizeof(VliwDependencia) || nb>n || nt>n ||
        (n && (!e->instrucciones || !e->bloques || !nb)) ||
        (m && !e->dependencias) || (nt && !destinos->items) || (!n && (nb || m || nt)))
        return ERROR(VLIW_INT_ESTRUCTURA,VAC,VAC,VAC,VAC,"cantidades o arreglos requeridos incoherentes");
    if (!n) return ERROR(VLIW_INT_OK,VAC,VAC,VAC,VAC,"entrada vacía válida");
    if (!e->metadatos_completos)
        return ERROR(VLIW_INT_NO_ACREDITADOS,VAC,VAC,VAC,VAC,"falta declaración externa de metadatos completos");
    size_t siguiente=0;
    for (size_t b=0;b<nb;++b) {
        const VliwBloque *bb=&e->bloques[b];
        if (bb->inicio!=siguiente || bb->fin<=bb->inicio || bb->fin>n ||
            bb->cantidad_sucesores>nb || (bb->cantidad_sucesores && !bb->sucesores))
            return ERROR(VLIW_INT_CFG,VAC,VAC,b,VAC,"partición o sucesores de bloque inválidos");
        for (size_t s=0;s<bb->cantidad_sucesores;++s) {
            if (bb->sucesores[s]>=nb) return ERROR(VLIW_INT_CFG,VAC,VAC,b,VAC,"sucesor fuera del CFG");
            for (size_t t=0;t<s;++t) if (bb->sucesores[t]==bb->sucesores[s])
                return ERROR(VLIW_INT_CFG,VAC,VAC,b,VAC,"sucesor repetido");
        }
        for (size_t i=bb->inicio;i<bb->fin;++i)
            if (e->instrucciones[i].bloque!=b)
                return ERROR(VLIW_INT_CFG,i,VAC,b,VAC,"instrucción fuera de su bloque declarado");
        siguiente=bb->fin;
    }
    if (siguiente!=n) return ERROR(VLIW_INT_CFG,VAC,VAC,VAC,VAC,"cobertura incompleta de instrucciones");
    // La tabla se revisa antes de buscar por instrucción: no aceptamos una
    // entrada duplicada ni un destino que no corresponda a una BRU real.
    for (size_t t=0;t<nt;++t) {
        size_t i=destinos->items[t].instruccion;
        if (i>=n || e->instrucciones[i].control==VLIW_CONTROL_NINGUNO)
            return ERROR(VLIW_INT_DESTINO,i,VAC,VAC,t,"destino sin transferencia correspondiente");
        for (size_t q=0;q<t;++q) if (destinos->items[q].instruccion==i)
            return ERROR(VLIW_INT_DESTINO,i,VAC,VAC,t,"destino duplicado");
    }
    for (size_t i=0;i<n;++i) {
        const VliwInstruccion *in=&e->instrucciones[i];
        if ((unsigned)in->slot>=VLIW_SLOTS) return ERROR(VLIW_INT_SLOT,i,VAC,in->bloque,VAC,"slot fuera de0..3");
        if (in->lee>UINT32_MAX || in->escribe>UINT32_MAX)
            return ERROR(VLIW_INT_REGISTRO,i,VAC,in->bloque,VAC,"máscara contiene registros fuera de x0..x31");
        if (in->efectos & ~EFECTOS) return ERROR(VLIW_INT_EFECTOS,i,VAC,in->bloque,VAC,"bits de efectos desconocidos");
        if ((unsigned)in->control>VLIW_CONTROL_RETORNO ||
            ((in->slot==VLIW_SLOT_BRU)!=(in->control!=VLIW_CONTROL_NINGUNO)) ||
            (in->control!=VLIW_CONTROL_NINGUNO && i+1!=e->bloques[in->bloque].fin))
            return ERROR(VLIW_INT_CFG,i,VAC,in->bloque,VAC,"control incompatible con slot o final de bloque");
        for (size_t j=0;j<i;++j) if (in->id==e->instrucciones[j].id)
            return ERROR(VLIW_INT_ESTRUCTURA,i,VAC,in->bloque,VAC,"identificador de operación repetido");
        SimInstruccion real;
        VliwIntegracionEstado estado=reconocer(in,i,&real,d);
        if (estado) return estado;
        estado=fisica(in,i,&real,d); if (estado) return estado;
        if (in->control!=VLIW_CONTROL_NINGUNO) {
            const VliwDestino *t=NULL;size_t it=VAC;
            for (size_t q=0;q<nt;++q) if (destinos->items[q].instruccion==i) {t=&destinos->items[q];it=q;break;}
            estado=transferencia(e,i,&real,t,it,d);if (estado) return estado;
        }
    }
    for (size_t b=0;b<nb;++b) {
        const VliwBloque *bb=&e->bloques[b];
        if (e->instrucciones[bb->fin-1].control==VLIW_CONTROL_NINGUNO) {
            size_t cantidad=b+1<nb ? 1 : 0;
            if (bb->cantidad_sucesores!=cantidad || (cantidad && bb->sucesores[0]!=b+1))
                return ERROR(VLIW_INT_CFG,bb->fin-1,VAC,b,VAC,"caída secuencial incorrecta");
        }
    }
    VliwIntegracionEstado estado=dependencias(e,d);
    if (estado) return estado;
    return ERROR(VLIW_INT_OK,VAC,VAC,VAC,VAC,"coherencia local válida; garantías semánticas a cargo del productor");
}
