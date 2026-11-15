/* Prueba la cadena consumidora real con operaciones físicas, no BAL compilado.
 * Reutiliza fixtures de la nueva prueba de proyección, sin cambiar suites previas.
 * Verifica las fases, publicación, recursos, layout y artefactos CB consumibles
 * por B1/B2. Las garantías ABI de cada fixture se explican, no se infieren. */
#define VLIW_PROYECCION_SIN_MAIN
#include "test_vliw_proyeccion_fisica.c"
#include "vliw_cadena_fisica.h"
#ifdef FIS_FALLOS
static VliwCadenaFase fallo_etapa;static bool verificar_listo,codificar_listo;
VliwEstado __real_vliw_planificar(const VliwEntrada *,VliwPlan **,VliwDiagnostico *);
VliwEstado __real_vliw_verificar(const VliwEntrada *,const VliwPlan *,VliwDiagnostico *);
VliwEmisorEstado __real_vliw_emitir(const VliwEntrada *,const VliwPlan *,const VliwDestinos *,char **,VliwEmisorDiagnostico *);
int __real_cb_ensamblar(const char *,CbPrograma *,FILE *);
VliwVisualEstado __real_vliw_visualizar(const VliwEntrada *,const VliwPlan *,const VliwDestinos *,VliwVisualFormato,const VliwVisualOpciones *,char **,VliwVisualDiagnostico *);
FILE *__real_tmpfile(void);
/* Las sondas fuerzan rechazos puntuales de consumidores. Fuera del caso
 * armado se llaman las APIs reales; los positivos no sustituyen ninguna fase. */
VliwEstado __wrap_vliw_planificar(const VliwEntrada *e,VliwPlan **p,VliwDiagnostico *d)
{
    if(fallo_etapa==VLIW_CAD_PLAN){if(d)*d=(VliwDiagnostico){VLIW_CAPACIDAD,VAC,VAC,"capacidad S7 inyectada"};return VLIW_CAPACIDAD;}
    VliwEstado s=__real_vliw_planificar(e,p,d);verificar_listo=s==VLIW_OK;return s;
}
/* El armado posterior a planificar distingue el verificador explícito de
 * la cadena de las verificaciones internas del propio scheduler. */
VliwEstado __wrap_vliw_verificar(const VliwEntrada *e,const VliwPlan *p,VliwDiagnostico *d)
{
    if(fallo_etapa==VLIW_CAD_VERIFICACION && verificar_listo){if(d)*d=(VliwDiagnostico){VLIW_PLAN_INVALIDO,0,VAC,"plan inválido inyectado"};return VLIW_PLAN_INVALIDO;}
    return __real_vliw_verificar(e,p,d);
}
/* CB también se usa dentro de S8-A. Armamos su fallo tardío únicamente
 * después de que el emisor real termine para comprobar la codificación final. */
VliwEmisorEstado __wrap_vliw_emitir(const VliwEntrada *e,const VliwPlan *p,const VliwDestinos *ds,char **s,VliwEmisorDiagnostico *d)
{
    if(fallo_etapa==VLIW_CAD_EMISION){if(d){memset(d,0,sizeof *d);d->instruccion=0;snprintf(d->mensaje,sizeof d->mensaje,"emisor rechazado");}return VLIW_EM_CODIFICADOR;}
    VliwEmisorEstado r=__real_vliw_emitir(e,p,ds,s,d);codificar_listo=r==VLIW_EM_OK;return r;
}
/* El rechazo conserva la interfaz real entero+FILE y no publica CbPrograma. */
int __wrap_cb_ensamblar(const char *s,CbPrograma *p,FILE *d)
{
    if(fallo_etapa==VLIW_CAD_CODIFICACION && codificar_listo){if(d)fputs("rechazo CB inyectado\n",d);return 1;}
    return __real_cb_ensamblar(s,p,d);
}
/* Un error de visualización debe liberar incluso ASM, plan y código ya válidos. */
VliwVisualEstado __wrap_vliw_visualizar(const VliwEntrada *e,const VliwPlan *p,const VliwDestinos *ds,VliwVisualFormato f,const VliwVisualOpciones *o,char **s,VliwVisualDiagnostico *d)
{
    if(fallo_etapa==VLIW_CAD_VISUALIZACION){if(d)*d=(VliwVisualDiagnostico){VLIW_VIS_MEMORIA,VLIW_OK,VAC,VAC,"visualización sin memoria"};return VLIW_VIS_MEMORIA;}
    return __real_vliw_visualizar(e,p,ds,f,o,s,d);
}
/* La única E/S fallida que se acredita aquí es crear el flujo de diagnóstico;
 * no se afirma que se hayan inyectado fallos de lectura/escritura/cierre. */
FILE *__wrap_tmpfile(void)
{
    if(fallo_etapa==VLIW_CAD_PROYECCION)return NULL;
    return __real_tmpfile();
}
#endif
/* El fixture declara completitud local: todas las operaciones, efectos y
 * referencias están presentes; no hay direcciones materiales por reubicar.
 * Esta declaración no se deriva automáticamente de un resultado del constructor. */
static VliwCadenaEntrada entrada_cadena(const Fisica *f)
{
    return (VliwCadenaEntrada){&f->flujo,f->efectos,f->flujo.cantidad_operaciones,
        f->textos,f->flujo.cantidad_operaciones,71,true,true,{0,true,true}};
}
/* Verifica también el descriptor del coordinador, además del almacenamiento
 * del fixture. Los punteros son identidades prestadas, no índices físicos. */
static void conservar_entrada_cadena(const VliwCadenaEntrada *a,const VliwCadenaEntrada *b)
{
    EXIGIR(a->flujo==b->flujo && a->efectos_adicionales==b->efectos_adicionales && a->cantidad_efectos==b->cantidad_efectos &&
        a->textos==b->textos && a->cantidad_textos==b->cantidad_textos && a->generacion==b->generacion &&
        a->efectos_acreditados==b->efectos_acreditados && a->abi_acreditado==b->abi_acreditado);
    EXIGIR(a->garantias.pendientes==b->garantias.pendientes && a->garantias.completitud_declarada==b->garantias.completitud_declarada &&
        a->garantias.reubicaciones_resueltas==b->garantias.reubicaciones_resueltas);
}
/* La captura usa campos: el constructor no puede alterar entradas ni siquiera
 * cuando falla tarde. El estado especial -1 exige cualquier rechazo real, usado
 * solo en la inyección exhaustiva de reservas de las dependencias existentes. */
static VliwCadena *construir_cadena(Fisica *f,VliwCadenaEntrada *e,VliwCadenaEstado esperado,VliwCadenaFase fase)
{
    Fisica antes=*f;VliwCadenaEntrada entrada_antes=*e;VliwCadena *c=NULL;VliwCadenaDiagnostico d;
    VliwCadenaEstado estado=vliw_cadena_construir(e,&c,&d);
    if(esperado!=(VliwCadenaEstado)-1 && estado!=esperado)fprintf(stderr,"cadena %d != %d; fase %d, original %d: %s\n",estado,esperado,d.fase,d.estado_original,d.mensaje);
    EXIGIR(esperado==(VliwCadenaEstado)-1?estado!=VLIW_CAD_OK:estado==esperado);
    EXIGIR(d.estado==estado && *d.mensaje && d.generacion==e->generacion);
    EXIGIR((c!=NULL)==(estado==VLIW_CAD_OK));if(esperado!=(VliwCadenaEstado)-1)EXIGIR(d.fase==fase);
    conservar_fisica(&antes,f);conservar_entrada_cadena(&entrada_antes,e);++escenarios;return c;
}
/* Oráculos de extremo a extremo: aristas/destinos exactos de objetos reales,
 * plan verificado, ocupación coherente y palabras S2 reconocidas. No se acepta
 * equivalencia por la cantidad de bundles ni se sustituyen dependencias. */
static void comprobar_cadena(const Fisica *f,const VliwCadena *c)
{
    Fisica origen=*f;origen.cfg=c->cfg;origen.dag=c->dag;
    for(size_t i=0;i<c->cantidad_metadatos;++i)origen.meta[i]=c->metadatos[i];
    comprobar_copia(&origen,c->proyeccion);
    EXIGIR(c->generacion==71 && c->plan && c->asm_texto && c->visualizacion_json);
    EXIGIR(vliw_integracion_validar(&c->proyeccion->entrada,&c->proyeccion->destinos,NULL)==VLIW_INT_OK);
    EXIGIR(vliw_verificar(&c->proyeccion->entrada,c->plan,NULL)==VLIW_OK);
    EXIGIR(c->codigo.cantidad==c->plan->cantidad_bundles);
    EXIGIR(c->estadisticas.instrucciones_utiles==f->flujo.cantidad_operaciones && c->estadisticas.bundles_totales==c->plan->cantidad_bundles);
    EXIGIR(c->estadisticas.slots_utilizados==f->flujo.cantidad_operaciones);
    EXIGIR(c->codificado==(f->flujo.cantidad_operaciones!=0));
    for(size_t k=0;k<c->dag->cantidad_dependencias;++k){VliwDependencia a=c->dag->dependencias[k];
        EXIGIR(a.antes<a.despues && c->plan->bundle_por_instruccion[a.despues]>=c->plan->bundle_por_instruccion[a.antes]+a.distancia);}
    for(size_t q=0;q<c->codigo.cantidad;++q)for(unsigned s=0;s<4;++s){size_t i=c->plan->bundles[q].instrucciones[s];
        if(i==VAC)EXIGIR(c->codigo.bundles[q].slots[s]==0);
        else {SimInstruccion real;EXIGIR(sim_decode_palabra(c->codigo.bundles[q].slots[s],s,&real)==SIM_DECODE_OK);EXIGIR(real.operacion==c->metadatos[i].operacion);}}
}
/* Secuencias, x0 y slots concurrentes atraviesan toda la cadena. Destruimos
 * el fixture y volvemos a verificar para detectar préstamos colgantes. */
static void positivas_cadena(void)
{
    for(size_t n=0;n<=8;++n){Fisica f;iniciar_fisica(&f,n);VliwCadenaEntrada e=entrada_cadena(&f);
        VliwCadena *c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);comprobar_cadena(&f,c);
        memset(&f,0,sizeof f);EXIGIR(vliw_verificar(&c->proyeccion->entrada,c->plan,NULL)==VLIW_OK);
        vliw_cadena_liberar(&c);EXIGIR(!c);vliw_cadena_liberar(&c);}
    Fisica f;iniciar_fisica(&f,2);operacion(&f,0,0,"sumai x0,x0,7");operacion(&f,1,1,"cargai x5,x0,0");
    VliwCadenaEntrada e=entrada_cadena(&f);VliwCadena *c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);
    comprobar_cadena(&f,c);EXIGIR(c->plan->bundles[0].operaciones==2);vliw_cadena_liberar(&c);vliw_cadena_liberar(NULL);
}
/* Distancias manuales y grupos con slots en conflicto ejercitan planificación
 * real; las dos ALU no caben simultáneamente aunque carezcan de riesgos GPR. */
static void riesgos_cadena(void)
{
    const char *a[]={"sumai x4,x0,7","sumai x5,x4,1","sumai x4,x0,7","cargai x4,x2,0","fsl x6,x2,0,1"};
    const char *b[]={"sumai x5,x4,1","sumai x4,x0,7","sumai x4,x0,9","guardap x2,x4,0","sumai x4,x7,1"};
    for(size_t k=0;k<5;++k){Fisica f;iniciar_fisica(&f,2);operacion(&f,0,k==3?1:k==4?3:0,a[k]);operacion(&f,1,k==3?1:0,b[k]);
        VliwCadenaEntrada e=entrada_cadena(&f);VliwCadena *c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);
        comprobar_cadena(&f,c);EXIGIR(c->dag->cantidad_dependencias==1);
        EXIGIR(c->plan->bundle_por_instruccion[1]>=c->plan->bundle_por_instruccion[0]+(k==0 || k>=3?3u:1u));vliw_cadena_liberar(&c);}
}
/* Backedges protegen B0 y conservan tipo de salto. Call/ret usa SYE/x1 y no
 * direcciones materiales antiguas; solo acredita este pequeño flujo físico,
 * sin certificar retorno de main ni preservar un callee BAL desconocido. */
static void control_cadena(void)
{
    for(unsigned cond=0;cond<2;++cond){Fisica f;iniciar_fisica(&f,3);salto_fisico(&f,1,0,cond!=0);
        VliwCadenaEntrada e=entrada_cadena(&f);VliwCadena *c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);
        comprobar_cadena(&f,c);EXIGIR(c->estadisticas.bundles_proteccion>=4);vliw_cadena_liberar(&c);}
    Fisica f;llamada_fisica(&f);VliwCadenaEntrada e=entrada_cadena(&f);
    VliwCadena *c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);comprobar_cadena(&f,c);
    EXIGIR(c->cfg->cantidad_destinos==3 && c->cfg->destinos[2].tipo==VLIW_DEST_RETORNO);vliw_cadena_liberar(&c);
    llamada_fisica(&f);e=entrada_cadena(&f);e.abi_acreditado=false;
    c=construir_cadena(&f,&e,VLIW_CAD_ABI,VLIW_CAD_INICIO);EXIGIR(!c);
    llamada_fisica(&f);e=entrada_cadena(&f);e.garantias.reubicaciones_resueltas=false;
    c=construir_cadena(&f,&e,VLIW_CAD_REUBICACION,VLIW_CAD_INICIO);EXIGIR(!c);
    llamada_fisica(&f);e=entrada_cadena(&f);f.efectos[0]=0;
    c=construir_cadena(&f,&e,VLIW_CAD_DEPENDENCIAS,VLIW_CAD_DAG);EXIGIR(!c);
    // Un SYER con ABI declarado no basta si su conjunto de destinos sigue
    // incompleto. CFG puede describirlo parcialmente; DAG impide promoverlo.
    llamada_fisica(&f);operacion(&f,0,2,"syer x1,x13,0");f.op[0].control=VLIW_CONTROL_LLAMADA;
    f.posibles[1]=600;f.transferencias[1].tipo=VLIW_DEST_INDIRECTO;
    f.transferencias[1].tomado=VAC;f.transferencias[1].posibles=f.posibles+1;
    f.transferencias[1].cantidad_posibles=1;f.transferencias[1].completos=false;
    e=entrada_cadena(&f);c=construir_cadena(&f,&e,VLIW_CAD_DEPENDENCIAS,VLIW_CAD_DAG);EXIGIR(!c);
}
/* Las seis CRIPTO conservan barrera3 y ambos destinos Feistel. No se ejecuta
 * una operación sin AUTH esperando éxito semántico: aquí se comprueba su código. */
static void cripto_cadena(void)
{
    const char *ops[]={"fsl x6,x2,0,1","fsli x8,x4,3,2","ell 2,0,x4,x10","vcr 0","camcon 16,7","setpwd x25"};
    for(size_t k=0;k<6;++k){Fisica f;iniciar_fisica(&f,3);operacion(&f,1,3,ops[k]);
        VliwCadenaEntrada e=entrada_cadena(&f);VliwCadena *c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);
        comprobar_cadena(&f,c);EXIGIR(c->plan->bundle_por_instruccion[1]>=c->plan->bundle_por_instruccion[0]+3);
        EXIGIR(c->plan->bundle_por_instruccion[2]>=c->plan->bundle_por_instruccion[1]+3);vliw_cadena_liberar(&c);}
}
/* Cada mutación debe detener la fase responsable antes de publicar. Se
 * distinguen errores ISA, CFG, garantías y texto, sin reparaciones silenciosas. */
static void negativas_cadena(void)
{
    for(unsigned k=0;k<20;++k){Fisica f;iniciar_fisica(&f,2);VliwCadenaEntrada e=entrada_cadena(&f);
        VliwCadenaEstado estado=VLIW_CAD_INCOMPLETO;VliwCadenaFase fase=VLIW_CAD_INICIO;
        switch(k){case 0:e.generacion++;break;case 1:e.garantias.pendientes=1;break;case 2:e.efectos_acreditados=false;break;
        case 3:e.garantias.completitud_declarada=false;break;
        case 4:e.garantias.reubicaciones_resueltas=false;estado=VLIW_CAD_REUBICACION;break;
        case 5:e.cantidad_textos=SIZE_MAX;estado=VLIW_CAD_ARGUMENTO;break;
        case 6:e.cantidad_efectos=0;estado=VLIW_CAD_ARGUMENTO;break;
        case 7:f.op[0].palabra=0;estado=VLIW_CAD_ISA;fase=VLIW_CAD_META;break;
        case 8:f.op[0].palabra=UINT32_MAX;estado=VLIW_CAD_ISA;fase=VLIW_CAD_META;break;
        case 9:f.op[0].slot=VLIW_SLOT_LSU;estado=VLIW_CAD_ISA;fase=VLIW_CAD_META;break;
        case 10:f.op[1].id=f.op[0].id;estado=VLIW_CAD_CONTROL;fase=VLIW_CAD_CFG;break;
        case 11:f.op[0].control=VLIW_CONTROL_SALTO;estado=VLIW_CAD_CONTROL;fase=VLIW_CAD_CFG;break;
        case 12:f.textos[0]="sumai x4,x0,99";estado=VLIW_CAD_METADATOS;fase=VLIW_CAD_PROYECCION;break;
        case 13:f.textos[0]=NULL;estado=VLIW_CAD_METADATOS;fase=VLIW_CAD_PROYECCION;break;
        case 14:f.efectos[0]=VLIW_EF_LEE_MEMORIA;estado=VLIW_CAD_DEPENDENCIAS;fase=VLIW_CAD_DAG;break;
        case 15:salto_fisico(&f,0,1,false);f.transferencias[0].tomado=999;estado=VLIW_CAD_CONTROL;fase=VLIW_CAD_CFG;break;
        case 16:salto_fisico(&f,0,1,false);f.transferencias[0].completos=false;estado=VLIW_CAD_CONTROL;fase=VLIW_CAD_CFG;break;
        case 17:salto_fisico(&f,0,1,true);f.transferencias[0].continuacion=0;estado=VLIW_CAD_CONTROL;fase=VLIW_CAD_CFG;break;
        case 18:salto_fisico(&f,0,0,true);f.fn[0].fin=1;f.fn[1]=(VliwCfgFuncion){20,1,2};
            f.flujo.cantidad_funciones=2;estado=VLIW_CAD_CONTROL;fase=VLIW_CAD_CFG;break;
        default:f.flujo.generacion_referencias++;estado=VLIW_CAD_CONTROL;fase=VLIW_CAD_CFG;break;}
        VliwCadena *c=construir_cadena(&f,&e,estado,fase);EXIGIR(!c);
    }
    Fisica f;iniciar_fisica(&f,0);VliwCadenaEntrada e=entrada_cadena(&f);VliwCadena *c=NULL;
    EXIGIR(vliw_cadena_construir(NULL,&c,NULL)==VLIW_CAD_ARGUMENTO);++escenarios;
    EXIGIR(vliw_cadena_construir(&e,NULL,NULL)==VLIW_CAD_ARGUMENTO);++escenarios;
    VliwCadena ocupado={0};c=&ocupado;EXIGIR(vliw_cadena_construir(&e,&c,NULL)==VLIW_CAD_ARGUMENTO && c==&ocupado);++escenarios;
    c=NULL;f.flujo.cantidad_operaciones=SIZE_MAX;EXIGIR(vliw_cadena_construir(&e,&c,NULL)==VLIW_CAD_CAPACIDAD && !c);++escenarios;
}
/* Fixtures dinámicos separan camino DAG y capacidad real S7. Cientos de
 * bloques independientes añaden guardas aunque el DAG no tenga aristas;
 * CRIPTO aproxima el límite por barreras. El condicional largo debe rechazarse
 * por S8-A, sin relajarlo ni invocar el enlazador tradicional. */
static void grande_cadena(size_t n,unsigned modo,VliwCadenaEstado esperado,VliwCadenaFase fase)
{
    Fisica base;iniciar_fisica(&base,1);operacion(&base,0,modo==1?0:3,modo==1?"sumai x0,x0,1":"vcr 0");
    VliwCfgOperacion *op=calloc(n,sizeof *op);unsigned *ef=calloc(n,sizeof *ef);
    const char **textos=calloc(n,sizeof *textos);size_t *fronteras=calloc(n,sizeof *fronteras);EXIGIR(op && ef && textos && fronteras);
    for(size_t i=0;i<n;++i){op[i]=base.op[0];op[i].id=100+7*i;textos[i]=modo==1?"sumai x0,x0,1":"vcr 0";fronteras[i]=i;}
    VliwCfgFuncion fn={10,0,n};VliwCfgSimbolo simbolo={500,10,n-1};
    VliwCfgTransferencia transferencia={0,VLIW_DEST_CONDICIONAL,500,1,NULL,0,true};
    VliwCfgEntrada f={.operaciones=op,.cantidad_operaciones=n,.funciones=&fn,.cantidad_funciones=1,
        .generacion=71,.generacion_referencias=71,.fronteras_acreditadas=true};
    if(modo==1){f.fronteras=fronteras+1;f.cantidad_fronteras=n-1;}
    if(modo==2){operacion(&base,0,2,"igualsi x0,x0,0");op[0]=base.op[0];textos[0]="igualsi x0,x0,0";
        f.simbolos=&simbolo;f.cantidad_simbolos=1;f.transferencias=&transferencia;f.cantidad_transferencias=1;}
    VliwCadenaEntrada e={&f,ef,n,textos,n,71,true,true,{0,true,true}};VliwCadena *c=NULL;VliwCadenaDiagnostico d;
    clock_t inicio=clock();VliwCadenaEstado estado=vliw_cadena_construir(&e,&c,&d);double t=(double)(clock()-inicio)/CLOCKS_PER_SEC;
    if(estado!=esperado)fprintf(stderr,"grande %d != %d, fase %d: %s\n",estado,esperado,d.fase,d.mensaje);
    EXIGIR(estado==esperado && d.fase==fase && (c!=NULL)==(estado==VLIW_CAD_OK));++escenarios;
    if(c){EXIGIR(c->plan->cantidad_bundles<=1023 && c->plan->cantidad_bundles>1000);
        printf("ESTRES cadena n=%zu modo=%u bundles=%zu aristas=%zu critico=%zu segundos=%.6f\n",n,modo,c->plan->cantidad_bundles,c->dag->cantidad_dependencias,c->dag->camino_critico,t);}
    else printf("ESTRES rechazo n=%zu modo=%u fase=%d original=%d segundos=%.6f\n",n,modo,d.fase,d.estado_original,t);
    vliw_cadena_liberar(&c);free(op);free(ef);free(textos);free(fronteras);
}
/* Cerca del límite no significa capacidad garantizada para todo flujo. Los
 * rechazos se ejercitan tanto antes de planificar como al emitir un offset. */
static void limites_cadena(void)
{
    grande_cadena(340,0,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);
    grande_cadena(341,1,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);
    grande_cadena(342,1,VLIW_CAD_CAPACIDAD,VLIW_CAD_PLAN);
    grande_cadena(26,2,VLIW_CAD_CAPACIDAD,VLIW_CAD_EMISION);
}
/* Se falla cada adquisición real del flujo call/ret. Las capturas verifican
 * inmutabilidad también después del error y cada iteración repite la cadena
 * sana. Códigos heredados de CB conservan su categoría original de rechazo. */
static void recursos_cadena(void)
{
#ifdef FIS_FALLOS
    Fisica f;llamada_fisica(&f);VliwCadenaEntrada e=entrada_cadena(&f);VliwCadena *c=NULL;
    llamadas=0;fallar=SIZE_MAX;activo=true;VliwCadenaEstado estado=vliw_cadena_construir(&e,&c,NULL);activo=false;
    EXIGIR(estado==VLIW_CAD_OK && c);size_t total=llamadas;EXIGIR(total>50);vliw_cadena_liberar(&c);
    for(size_t k=1;k<=total;++k){llamadas=0;fallar=k;activo=true;
        c=construir_cadena(&f,&e,(VliwCadenaEstado)-1,VLIW_CAD_INICIO);activo=false;EXIGIR(!c);
        c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);vliw_cadena_liberar(&c);}
    printf("RESERVAS cadena=%zu; fallos=%zu\n",total,total);
#else
    EXIGIR(false);
#endif
}
/* Rechazos controlados prueban que se detiene la fase correcta, se conserva
 * el estado original y no se publica código. Después se recupera con APIs reales. */
static void fases_cadena(void)
{
#ifdef FIS_FALLOS
    const VliwCadenaFase fases[]={VLIW_CAD_PLAN,VLIW_CAD_VERIFICACION,VLIW_CAD_EMISION,VLIW_CAD_CODIFICACION,VLIW_CAD_VISUALIZACION,VLIW_CAD_PROYECCION};
    const VliwCadenaEstado estados[]={VLIW_CAD_CAPACIDAD,VLIW_CAD_PLAN_INVALIDO,VLIW_CAD_EMISOR,VLIW_CAD_CODIGO,VLIW_CAD_MEMORIA,VLIW_CAD_IO};
    for(size_t k=0;k<sizeof fases/sizeof fases[0];++k){Fisica f;iniciar_fisica(&f,2);VliwCadenaEntrada e=entrada_cadena(&f);
        fallo_etapa=fases[k];verificar_listo=codificar_listo=false;
        VliwCadena *c=construir_cadena(&f,&e,estados[k],fases[k]);EXIGIR(!c);
        fallo_etapa=VLIW_CAD_INICIO;verificar_listo=codificar_listo=false;
        c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);vliw_cadena_liberar(&c);}
#else
    EXIGIR(false);
#endif
}
/* Publica artefactos únicamente en el directorio temporal del test Python.
 * El reference es ASM físico manual con separación N+3; scheduled proviene
 * íntegramente de la cadena real. B1 decide después su comparabilidad pública. */
static void artefactos_cadena(const char *directorio)
{
    Fisica f;iniciar_fisica(&f,4);operacion(&f,0,0,"sumai x4,x0,7");operacion(&f,1,1,"cargai x5,x0,0");
    operacion(&f,2,0,"suma x6,x4,x5");operacion(&f,3,1,"guardap x0,x6,4");
    VliwCadenaEntrada e=entrada_cadena(&f);VliwCadena *c=construir_cadena(&f,&e,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION);comprobar_cadena(&f,c);
    const char *referencia="sumai x4,x0,7 | nop | nop | nop\nnop\nnop\nnop | cargai x5,x0,0 | nop | nop\nnop\nnop\nsuma x6,x4,x5 | nop | nop | nop\nnop\nnop\nnop | guardap x0,x6,4 | nop | nop\nnop\nnop\n";
    CbPrograma cb={0};EXIGIR(cb_ensamblar(referencia,&cb,stderr)==0);
    const char *nombres[]={"scheduled.mem","scheduled.bin","reference.mem","reference.bin","scheduled.asm","visual.json"};
    for(size_t k=0;k<6;++k){char path[4096];int len=snprintf(path,sizeof path,"%s/%s",directorio,nombres[k]);EXIGIR(len>0 && (size_t)len<sizeof path);
        FILE *out=fopen(path,"wb");EXIGIR(out);
        if(k==0)EXIGIR(cb_escribir_mem(&c->codigo,out)==0);else if(k==1)EXIGIR(cb_escribir_bin(&c->codigo,out)==0);
        else if(k==2)EXIGIR(cb_escribir_mem(&cb,out)==0);else if(k==3)EXIGIR(cb_escribir_bin(&cb,out)==0);
        else EXIGIR(fputs(k==4?c->asm_texto:c->visualizacion_json,out)>=0);
        EXIGIR(fclose(out)==0);}
    printf("ARTEFACTOS instrucciones=%zu bundles=%zu critico=%zu utilizacion=%.6f\n",c->estadisticas.instrucciones_utiles,c->estadisticas.bundles_totales,c->dag->camino_critico,c->estadisticas.utilizacion_porcentual);
    cb_liberar(&cb);vliw_cadena_liberar(&c);
}
/* Cada grupo ejecuta escenarios reales y publica cantidades observadas. Los
 * artefactos requieren un destino explícito para impedir escrituras al proyecto. */
int main(int argc,char **argv)
{
    if(argc<2)return 2;
    if(!strcmp(argv[1],"positivas"))positivas_cadena();else if(!strcmp(argv[1],"riesgos"))riesgos_cadena();
    else if(!strcmp(argv[1],"control"))control_cadena();else if(!strcmp(argv[1],"cripto"))cripto_cadena();
    else if(!strcmp(argv[1],"negativas"))negativas_cadena();else if(!strcmp(argv[1],"limites"))limites_cadena();
    else if(!strcmp(argv[1],"recursos"))recursos_cadena();else if(!strcmp(argv[1],"fases"))fases_cadena();
    else if(!strcmp(argv[1],"artefactos") && argc==3)artefactos_cadena(argv[2]);else return 2;
    printf("PASS %s: escenarios=%zu; comprobaciones=%zu\n",argv[1],escenarios,comprobaciones);return 0;
}
