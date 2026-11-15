/* Comprueba métricas, presentación, rechazo atómico y propiedad de datos.
 * El modo auxiliar lee fixtures numéricos; nunca analiza BAL ni dependencias. */
#include "vliw_visualizador.h"
#include <stdarg.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t casos, comprobaciones;
#define C(x) do { ++comprobaciones; if (!(x)) { fprintf(stderr,"FAIL S8-C %d: %s\n",__LINE__,#x); exit(1); } } while (0)
#define R(x) (UINT64_C(1) << (x))
typedef struct {
    VliwInstruccion i[16];
    VliwDependencia a[32];
    VliwBloque b[8]; size_t su[8][8];
    VliwDestino dest[8]; VliwDestinos destinos;
    VliwEntrada e;
} Fixture;
/* Inicializa arreglos y estado del fixture. */ static void iniciar(Fixture *f, size_t n)
{
    C(n <= 16); memset(f, 0, sizeof *f);
    f->e = (VliwEntrada){f->i,n,f->a,0,f->b,n?1:0,true};
    f->b[0] = (VliwBloque){0,n,NULL,0};
    f->destinos = (VliwDestinos){f->dest,0};
    for (size_t j=0; j<n; ++j) f->i[j] = (VliwInstruccion){.id=100+11*j,
        .slot=VLIW_SLOT_ALU,.texto="sumai x0,x0,0"};
}
/* Invoca S7 sobre los metadatos del fixture y exige un plan completo antes de usar sus índices. */ static VliwPlan *plan(Fixture *f)
{
    VliwPlan *p=NULL; VliwDiagnostico d;
    VliwEstado s=vliw_planificar(&f->e,&p,&d);
    if (s) fprintf(stderr,"S7: %s\n",d.mensaje);
    C(s==VLIW_OK && p); return p;
}
/* Solicita una representación al visualizador y exige éxito antes de examinar el texto propio. */ static char *mostrar(Fixture *f,VliwPlan *p,VliwVisualFormato formato)
{
    char *salida=NULL; VliwVisualDiagnostico d;
    C(vliw_visualizar(&f->e,p,&f->destinos,formato,NULL,&salida,&d)==VLIW_VIS_OK);
    C(salida && d.estado==VLIW_VIS_OK); return salida;
}
/* Obtiene estadísticas estáticas del plan y exige su validez antes de comparar contadores y cocientes. */ static VliwEstadisticas medir(Fixture *f,VliwPlan *p)
{
    VliwEstadisticas a; VliwVisualDiagnostico d;
    C(vliw_estadisticas(&f->e,p,&a,&d)==VLIW_VIS_OK); return a;
}
/* Invoca la API con una entrada adversarial y exige el error previsto, sin aceptar salidas parciales. */ static void rechazar(Fixture *f,VliwPlan *p,VliwVisualEstado esperado)
{
    for (unsigned formato=0; formato<=VLIW_VIS_JSON; ++formato) {
        char *s=NULL; VliwVisualDiagnostico d;
        C(vliw_visualizar(&f->e,p,&f->destinos,(VliwVisualFormato)formato,NULL,&s,&d)==esperado);
        C(!s && d.estado==esperado && d.mensaje);
    }
}
/* Define el bloque y sus límites en los metadatos de prueba para comprobar CFG y layout. */ static void bloque(Fixture *f,size_t b,size_t inicio,size_t fin)
{
    f->b[b]=(VliwBloque){inicio,fin,f->su[b],0};
    for (size_t j=inicio; j<fin; ++j) f->i[j].bloque=b;
}
/* Comprueba vistas elementales y estadísticas, incluyendo entrada vacía y utilización de los cuatro slots. */ static void basicos_y_metricas(void)
{
    Fixture f; VliwPlan *p; char *s;
    /* El vacío es un reporte válido, con tablas/listas sin filas y cero
     * en ambos cocientes. No existe un ciclo de ejecución implícito. */
    ++casos; iniciar(&f,0); p=plan(&f); VliwEstadisticas a=medir(&f,p);
    C(!a.bundles_totales && !a.instrucciones_utiles && !a.slots_disponibles);
    C(a.utilizacion_porcentual==0 && a.operaciones_por_bundle==0);
    s=mostrar(&f,p,VLIW_VIS_JSON); C(strstr(s,"\"bundles\":[]")); vliw_visual_liberar(&s);
    s=mostrar(&f,p,VLIW_VIS_CSV); C(strstr(s,"bundle,pc,bloque,alu,lsu,bru,cripto,operaciones"));
    C(strchr(s,'\n')==s+strlen(s)-1); vliw_visual_liberar(&s); vliw_liberar(&p);
    /* Una operación ocupa un slot de cuatro: utilización estática del 25%. */ ++casos; iniciar(&f,1); p=plan(&f); a=medir(&f,p);
    C(a.instrucciones_utiles==1 && a.bundles_totales==1 && a.bytes_imem==16 && a.bloques==1);
    C(a.slots_disponibles==4 && a.slots_utilizados==1 && a.slots_vacios==3 && a.utilizacion_porcentual==25.0);
    C(a.operaciones_por_bundle==1.0 && a.operaciones_por_slot[0]==1);
    s=mostrar(&f,p,VLIW_VIS_CSV);
    C(strstr(s,"0,0x0000,0,\"sumai x0,x0,0\",\"NOP\",\"NOP\",\"NOP\",1,1,0,0,OPERACIONES,0,,,,0,0,0,0\r\n"));
    vliw_visual_liberar(&s); vliw_visual_liberar(&s); vliw_visual_liberar(NULL); vliw_liberar(&p);
    /* ALU y LSU independientes ocupan dos slots; no se fuerza una coemisión
     * con barreras CRIPTO/BRU solo para representar las cuatro columnas. */
    ++casos; iniciar(&f,2); f.i[1].slot=VLIW_SLOT_LSU; f.i[1].efectos=1; f.i[1].texto="cargai x0,x0,0";
    p=plan(&f); a=medir(&f,p); C(a.bundles_totales==1 && a.slots_utilizados==2 && a.slots_vacios==2);
    C(a.utilizacion_porcentual==50 && a.operaciones_por_bundle==2 && a.operaciones_por_slot[1]==1);
    s=mostrar(&f,p,VLIW_VIS_TABLA); C(strstr(s,"I0: sumai x0,x0,0") && strstr(s,"I1: cargai x0,x0,0"));
    C(strstr(s,"BRU") && strstr(s,"CRIPTO") && strstr(s,"no es ciclo de ejecución"));
    vliw_visual_liberar(&s); vliw_liberar(&p);
    /* RAW exige bundles0 y3. Los huecos intermedios se llaman VACIO:
     * el reporte no intenta explicar la decisión interna del selector. */
    ++casos; iniciar(&f,2); f.i[0].escribe=R(5); f.i[1].lee=R(5);
    f.a[0]=(VliwDependencia){0,1,VLIW_DEP_RAW,3}; f.e.cantidad_dependencias=1;
    p=plan(&f); a=medir(&f,p); C(a.bundles_totales==4 && a.bundles_vacios==2 && a.bundles_proteccion==0);
    C(a.slots_disponibles==16 && a.slots_vacios==14 && a.utilizacion_porcentual==12.5 && a.operaciones_por_bundle==0.5);
    C(a.bytes_imem==64 && a.dependencias_por_tipo[VLIW_DEP_RAW]==1);
    s=mostrar(&f,p,VLIW_VIS_TABLA); C(strstr(s,"RAW I0 -> I1: productor=0 consumidor=3 distancia=3 mínimo=3 SATISFECHA"));
    C(strstr(s,"VACIO") && !strstr(s,"por dependencia")); vliw_visual_liberar(&s); vliw_liberar(&p);
    /* Las seis categorías cuentan exclusivamente aristas suministradas.
     * WAR y WAW usan máscaras compatibles; las otras aristas son explícitas. */
    ++casos; iniciar(&f,3); f.i[0].lee=R(6); f.i[0].escribe=R(5);
    f.i[1].lee=R(5); f.i[1].escribe=R(6); f.i[2].escribe=R(6);
    f.a[0]=(VliwDependencia){0,1,VLIW_DEP_RAW,3}; f.a[1]=(VliwDependencia){0,1,VLIW_DEP_WAR,1};
    f.a[2]=(VliwDependencia){1,2,VLIW_DEP_WAW,1};
    for (unsigned t=3; t<6; ++t) f.a[t]=(VliwDependencia){0,2,(VliwTipoDependencia)t,1};
    f.e.cantidad_dependencias=6; p=plan(&f); a=medir(&f,p);
    for (unsigned t=0; t<6; ++t) C(a.dependencias_por_tipo[t]==1);
    s=mostrar(&f,p,VLIW_VIS_JSON); C(strstr(s,"\"efectiva\":3") && strstr(s,"\"estado\":\"SATISFECHA\""));
    vliw_visual_liberar(&s); vliw_liberar(&p);
}
/* Comprueba bloques, destinos y textos presentes o ausentes en las vistas exportadas. */ static void bloques_y_textos(void)
{
    Fixture f; VliwPlan *p; char *s;
    /* Dos guardas en B1 y también B0 por su backedge. El salto conserva
     * texto viejo: la anotación expresa el destino lógico sin reescribirlo. */
    ++casos; iniciar(&f,3); f.e.cantidad_bloques=2; bloque(&f,0,0,1); bloque(&f,1,1,3);
    f.su[0][0]=1; f.b[0].cantidad_sucesores=1; f.su[1][0]=0; f.b[1].cantidad_sucesores=1;
    f.i[2].slot=VLIW_SLOT_BRU; f.i[2].control=VLIW_CONTROL_SALTO; f.i[2].texto="sye x0,-999";
    f.dest[0]=(VliwDestino){2,VLIW_DEST_DIRECTO,0,VLIW_VACIO}; f.destinos.cantidad=1;
    p=plan(&f); VliwEstadisticas a=medir(&f,p); C(a.bundles_proteccion==4 && p->bundle_por_instruccion[0]==2);
    s=mostrar(&f,p,VLIW_VIS_JSON); C(strstr(s,"\"etiqueta\":\"B0\",\"inicio_bundle\":0"));
    C(strstr(s,"\"proteccion_bundles\":2") && strstr(s,"\"tipo\":\"DIRECTO\",\"tomado\":0,\"caida\":null"));
    C(strstr(s,"sye x0,-999")); vliw_visual_liberar(&s);
    ++casos; f.dest[0].tipo=VLIW_DEST_NO_RESUELTO; s=mostrar(&f,p,VLIW_VIS_TABLA);
    C(strstr(s,"NO_RESUELTO") && strstr(s,"PROTECCION") && strstr(s,"CONTROL")); vliw_visual_liberar(&s);
    f.dest[0].tomado=7; rechazar(&f,p,VLIW_VIS_DESTINO); f.dest[0].tomado=0;
    f.dest[1]=f.dest[0]; f.destinos.cantidad=2; rechazar(&f,p,VLIW_VIS_DESTINO); f.destinos.cantidad=1;
    f.dest[0].instruccion=0; rechazar(&f,p,VLIW_VIS_DESTINO); f.dest[0].instruccion=2;
    f.dest[0].tipo=(VliwDestinoTipo)99; rechazar(&f,p,VLIW_VIS_DESTINO);
    vliw_liberar(&p);
    /* Todos los slots se representan en su posición real. La barrera
     * criptográfica genera vacíos neutrales, nunca guardas inventadas. */
    ++casos; iniciar(&f,4); f.i[1].slot=VLIW_SLOT_LSU; f.i[1].efectos=1;
    f.i[2].slot=VLIW_SLOT_CRIPTO; f.i[2].efectos=16; f.i[2].texto="fsl x4,x2,0,0";
    f.i[3].slot=VLIW_SLOT_BRU; f.i[3].control=VLIW_CONTROL_SALTO; f.i[3].texto="sye x0,0";
    p=plan(&f); a=medir(&f,p); for (unsigned slot=0; slot<4; ++slot) C(a.operaciones_por_slot[slot]==1);
    C(!a.bundles_proteccion); s=mostrar(&f,p,VLIW_VIS_CSV); C(strstr(s,"\"fsl x4,x2,0,0\""));
    vliw_visual_liberar(&s); vliw_liberar(&p);
    /* NULL y cadena vacía no son NOP: conservan su índice útil, un indicador
     * de ausencia y un marcador explícito en la vista humana. */
    ++casos; iniciar(&f,2); f.i[0].texto=NULL; f.i[1].texto=""; p=plan(&f); a=medir(&f,p);
    C(a.textos_ausentes==2 && a.instrucciones_utiles==2);
    s=mostrar(&f,p,VLIW_VIS_TABLA); C(strstr(s,"I0: <SIN TEXTO>") && strstr(s,"I1: <SIN TEXTO>")); vliw_visual_liberar(&s);
    s=mostrar(&f,p,VLIW_VIS_JSON); C(strstr(s,"\"texto\":null,\"texto_ausente\":true") && strstr(s,"\"texto\":\"\",\"texto_ausente\":true"));
    vliw_visual_liberar(&s); vliw_liberar(&p);
    /* Escapado sin interpretar ISA: JSON protege controles y CSV duplica
     * comillas. La tabla no permite que ESC ni una nueva línea la alteren. */
    ++casos; iniciar(&f,1); f.i[0].texto="coma,\"barra\\\n\r\t\033|ñ"; p=plan(&f);
    s=mostrar(&f,p,VLIW_VIS_JSON); C(strstr(s,"\\u000a") && strstr(s,"\\u001b") && strstr(s,"ñ")); vliw_visual_liberar(&s);
    s=mostrar(&f,p,VLIW_VIS_CSV); C(strstr(s,"\"coma,\"\"barra\\\n\r\t\033|ñ\"")); vliw_visual_liberar(&s);
    s=mostrar(&f,p,VLIW_VIS_TABLA); C(strstr(s,"\\x1B") && !strchr(s,'\033')); vliw_visual_liberar(&s);
    /* Solo la tabla trunca; JSON debe conservar los 2048 caracteres. */ ++casos; char largo[2049]; memset(largo,'X',2048); largo[2048]=0; f.i[0].texto=largo;
    VliwVisualOpciones o={12,false,false}; VliwVisualDiagnostico d; s=NULL;
    C(vliw_visualizar(&f.e,p,NULL,VLIW_VIS_TABLA,&o,&s,&d)==VLIW_VIS_OK);
    C(strstr(s,"I0: XXXXX...") && !strstr(s,"Dependencias externas") && !strstr(s,"Estadísticas")); vliw_visual_liberar(&s);
    s=mostrar(&f,p,VLIW_VIS_JSON); C(strstr(s,largo)); vliw_visual_liberar(&s);
    o.ancho_slot=0; C(vliw_visualizar(&f.e,p,NULL,VLIW_VIS_TABLA,&o,&s,&d)==VLIW_VIS_OK); C(strstr(s,"...")); vliw_visual_liberar(&s);
    /* Las secuencias UTF-8 incompletas/sobreextendidas/surrogates se rechazan
     * sin cambiar la salida. Estadísticas no necesita interpretar ese texto. */
    ++casos; const char *malos[]={"\xff","\xc2","\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xf0\x80\x80\x80"};
    for (size_t j=0; j<sizeof malos/sizeof *malos; ++j) { f.i[0].texto=malos[j]; rechazar(&f,p,VLIW_VIS_TEXTO); }
    C(vliw_estadisticas(&f.e,p,&a,NULL)==VLIW_VIS_OK); vliw_liberar(&p);
}
/* Prueba argumentos inválidos y verifica que las salidas y buffers mantengan su contrato de propiedad. */ /* Probamos contratos inválidos conservando las entradas: un rechazo no puede entregar texto parcial ni apropiarse del plan prestado. */ static void errores_y_propiedad(void)
{
    ++casos; Fixture f; iniciar(&f,2); VliwPlan *p=plan(&f); char *s=NULL; VliwVisualDiagnostico d;
    C(vliw_visualizar(NULL,p,NULL,VLIW_VIS_TABLA,NULL,&s,&d)==VLIW_VIS_ARGUMENTO && !s);
    C(vliw_visualizar(&f.e,NULL,NULL,VLIW_VIS_TABLA,NULL,&s,NULL)==VLIW_VIS_ARGUMENTO);
    C(vliw_visualizar(&f.e,p,NULL,VLIW_VIS_TABLA,NULL,NULL,NULL)==VLIW_VIS_ARGUMENTO);
    C(vliw_visualizar(&f.e,p,NULL,(VliwVisualFormato)-1,NULL,&s,NULL)==VLIW_VIS_ARGUMENTO);
    VliwVisualOpciones o={7,true,true}; C(vliw_visualizar(&f.e,p,NULL,VLIW_VIS_JSON,&o,&s,NULL)==VLIW_VIS_ARGUMENTO);
    o.ancho_slot=161; C(vliw_visualizar(&f.e,p,NULL,VLIW_VIS_TABLA,&o,&s,NULL)==VLIW_VIS_ARGUMENTO);
    s=mostrar(&f,p,VLIW_VIS_JSON); char *anterior=s;
    C(vliw_visualizar(&f.e,p,NULL,VLIW_VIS_JSON,NULL,&s,NULL)==VLIW_VIS_ARGUMENTO && s==anterior); vliw_visual_liberar(&s);
    C(vliw_estadisticas(&f.e,p,NULL,NULL)==VLIW_VIS_ARGUMENTO);
    /* Una salida rechazada conserva intacto el resumen marcado con 0xa5. */ ++casos; VliwEstadisticas a,copia; memset(&a,0xa5,sizeof a); memcpy(&copia,&a,sizeof a);
    p->bundle_por_instruccion[0]=100; rechazar(&f,p,VLIW_VIS_PLAN);
    C(vliw_estadisticas(&f.e,p,&a,&d)==VLIW_VIS_PLAN && !memcmp(&a,&copia,sizeof a)); p->bundle_por_instruccion[0]=0;
    p->bundles[0].instrucciones[0]=99; rechazar(&f,p,VLIW_VIS_PLAN); p->bundles[0].instrucciones[0]=0;
    p->bundles[0].instrucciones[1]=0; rechazar(&f,p,VLIW_VIS_PLAN); p->bundles[0].instrucciones[1]=VLIW_VACIO;
    p->bundles[0].operaciones=2; rechazar(&f,p,VLIW_VIS_PLAN); p->bundles[0].operaciones=1;
    p->cantidad_vacios=1; rechazar(&f,p,VLIW_VIS_PLAN); p->cantidad_vacios=0;
    p->inicio_por_bloque[0]=1; rechazar(&f,p,VLIW_VIS_PLAN); p->inicio_por_bloque[0]=0;
    p->bundles[0].bloque=8; rechazar(&f,p,VLIW_VIS_PLAN); p->bundles[0].bloque=0;
    p->cantidad_bundles=1024; rechazar(&f,p,VLIW_VIS_PLAN); p->cantidad_bundles=2;
    f.e.cantidad_instrucciones=5000; rechazar(&f,p,VLIW_VIS_PLAN); f.e.cantidad_instrucciones=2;
    f.e.instrucciones=NULL; rechazar(&f,p,VLIW_VIS_PLAN); f.e.instrucciones=f.i;
    f.e.cantidad_dependencias=SIZE_MAX; rechazar(&f,p,VLIW_VIS_PLAN); f.e.cantidad_dependencias=0;
    /* Instantáneas de estructura y arreglos detectan cambios, no solo una
     * salida diferente. Las cadenas prestadas permanecen intactas. */
    ++casos; Fixture original=f; VliwPlan plan_original=*p;
    VliwBundle bu[2]; size_t mapa[2],inicio=p->inicio_por_bloque[0];
    memcpy(bu,p->bundles,sizeof bu); memcpy(mapa,p->bundle_por_instruccion,sizeof mapa);
    for (unsigned formato=0; formato<3; ++formato) {
        s=mostrar(&f,p,(VliwVisualFormato)formato);
        for (unsigned k=0; k<12; ++k) { char *otro=mostrar(&f,p,(VliwVisualFormato)formato); C(!strcmp(s,otro)); vliw_visual_liberar(&otro); }
        vliw_visual_liberar(&s);
    }
    C(!memcmp(&f,&original,sizeof f) && !memcmp(p,&plan_original,sizeof *p));
    C(!memcmp(bu,p->bundles,sizeof bu) && !memcmp(mapa,p->bundle_por_instruccion,sizeof mapa) && inicio==p->inicio_por_bloque[0]);
    /* La cadena publicada sobrevive a la liberación del plan y del texto
     * original. No expone referencias dentro de los datos prestados. */
    ++casos; char *literal=malloc(20); C(literal!=NULL); strcpy(literal,"texto propio"); f.i[0].texto=literal;
    s=mostrar(&f,p,VLIW_VIS_JSON); free(literal); vliw_liberar(&p); C(strstr(s,"texto propio")); vliw_visual_liberar(&s);
}
/* Cambia el locale de prueba y comprueba que el formato exportado conserve su representación numérica estable. */ static void localizacion(void)
{
    /* El cliente puede usar coma decimal. El JSON del visualizador debe
     * mantener punto sin cambiar el locale global del programa que lo llama. */
    if (!setlocale(LC_NUMERIC,"es_CR.utf8")) {
        fputs("Locale con coma decimal: NO VERIFICADO (no instalado)\n",stderr);
        return;
    }
    ++casos; Fixture f; iniciar(&f,1); VliwPlan *p=plan(&f);
    char *s=mostrar(&f,p,VLIW_VIS_JSON);
    C(strstr(s,"\"utilizacion_porcentual\":25.000000"));
    C(!strcmp(localeconv()->decimal_point,","));
    vliw_visual_liberar(&s); vliw_liberar(&p); C(setlocale(LC_NUMERIC,"C")!=NULL);
}
/* Visualiza el máximo de bundles admitido y comprueba tamaños y estadísticas sin desbordamientos. */ static void capacidad(void)
{
    ++casos; VliwInstruccion *i=calloc(1023,sizeof *i); C(i!=NULL);
    for (size_t j=0; j<1023; ++j) i[j]=(VliwInstruccion){.id=j,.slot=VLIW_SLOT_ALU,.texto="sumai x0,x0,0"};
    VliwBloque b={0,1023,NULL,0}; VliwEntrada e={i,1023,NULL,0,&b,1,true}; VliwPlan *p=NULL; VliwDiagnostico sd;
    C(vliw_planificar(&e,&p,&sd)==VLIW_OK); VliwEstadisticas a;
    C(vliw_estadisticas(&e,p,&a,NULL)==VLIW_VIS_OK && a.bytes_imem==16368 && a.slots_disponibles==4092);
    for (unsigned f=0; f<3; ++f) { char *s=NULL; C(vliw_visualizar(&e,p,NULL,(VliwVisualFormato)f,NULL,&s,NULL)==VLIW_VIS_OK); C(strstr(s,f==1?"1022,0x3fe0":"1022")); vliw_visual_liberar(&s); }
    p->cantidad_bundles=1024; char *s=NULL; C(vliw_visualizar(&e,p,NULL,VLIW_VIS_JSON,NULL,&s,NULL)==VLIW_VIS_PLAN && !s);
    p->cantidad_bundles=1023; vliw_liberar(&p); free(i);
}

#ifdef VLIW_VIS_FALLOS
/* Rutina real malloc: destino de las llamadas no inyectadas. */ void *__real_malloc(size_t); /* Rutina real calloc: destino de las llamadas no inyectadas. */ void *__real_calloc(size_t,size_t); /* Rutina real realloc: destino de las llamadas no inyectadas. */ void *__real_realloc(void *,size_t);
/* Rutina real vsnprintf: destino de las llamadas no inyectadas. */ int __real_vsnprintf(char *,size_t,const char *,va_list);
static size_t reservas, fallar, formato_llamadas, formato_fallar; static bool activo;
/* Envuelve malloc: inyecta fallos y delega el resto. */ void *__wrap_malloc(size_t n) { if (activo && ++reservas==fallar) return NULL; return __real_malloc(n); }
/* Envuelve calloc: inyecta fallos y delega el resto. */ void *__wrap_calloc(size_t n,size_t s) { if (activo && ++reservas==fallar) return NULL; return __real_calloc(n,s); }
/* Envuelve realloc: inyecta fallos y delega el resto. */ void *__wrap_realloc(void *p,size_t n) { if (activo && ++reservas==fallar) return NULL; return __real_realloc(p,n); }
/* Envuelve vsnprintf: inyecta fallos y delega el resto. */ int __wrap_vsnprintf(char *s,size_t n,const char *f,va_list a)
{
    if (activo && ++formato_llamadas==formato_fallar) return -1;
    return __real_vsnprintf(s,n,f,a);
}
#ifdef __GLIBC__
/* Ubuntu fortifica vsnprintf como __vsnprintf_chk. Se interceptan ambas
 * entradas sin desactivar FORTIFY ni cambiar la biblioteca de producción. */
int __real___vsnprintf_chk(char *,size_t,int,size_t,const char *,va_list);
/* Envuelve __vsnprintf_chk: inyecta fallos y delega el resto. */ int __wrap___vsnprintf_chk(char *s,size_t n,int flag,size_t capacidad,const char *f,va_list a)
{
    if (activo && ++formato_llamadas==formato_fallar) return -1;
    return __real___vsnprintf_chk(s,n,flag,capacidad,f,a);
}
#endif
/* Inyecta errores en reservas, archivos o generación y comprueba que no se publique una salida parcial ni se pierdan recursos. */ static void fallos(void)
{
    Fixture f; iniciar(&f,4); VliwPlan *p=plan(&f); size_t total=0;
    /* Primero se cuentan las reservas reales de cada formato. Después se
     * falla cada posición, incluidos el verificador S7 y el buffer creciente. */
    /* Cada formato debe rechazar reservas fallidas sin entregar texto parcial. */ for (unsigned formato=0; formato<3; ++formato) {
        char *s=NULL; reservas=0; activo=true;
        C(vliw_visualizar(&f.e,p,NULL,(VliwVisualFormato)formato,NULL,&s,NULL)==VLIW_VIS_OK);
        activo=false; size_t cantidad=reservas; vliw_visual_liberar(&s);
        for (size_t k=1; k<=cantidad; ++k) {
            reservas=0; fallar=k; activo=true; VliwVisualDiagnostico d;
            VliwVisualEstado estado=vliw_visualizar(&f.e,p,NULL,(VliwVisualFormato)formato,NULL,&s,&d);
            activo=false; fallar=0;
            C(estado==VLIW_VIS_MEMORIA && !s && d.estado==VLIW_VIS_MEMORIA); ++total;
        }
        for (size_t k=1; k<=2; ++k) {
            formato_llamadas=0; formato_fallar=k; activo=true;
            VliwVisualEstado estado=vliw_visualizar(&f.e,p,NULL,(VliwVisualFormato)formato,NULL,&s,NULL);
            if (estado!=VLIW_VIS_GENERACION) fprintf(stderr,"formato=%u inyección=%zu estado=%d llamadas=%zu\n",formato,k,estado,formato_llamadas);
            C(estado==VLIW_VIS_GENERACION && !s);
            activo=false; formato_fallar=0;
        }
    }
    VliwEstadisticas a,copia; memset(&a,0xa5,sizeof a); memcpy(&copia,&a,sizeof a);
    reservas=0; activo=true; C(vliw_estadisticas(&f.e,p,&a,NULL)==VLIW_VIS_OK); activo=false;
    size_t cantidad=reservas;
    for (size_t k=1; k<=cantidad; ++k) {
        memcpy(&a,&copia,sizeof a); reservas=0; fallar=k; activo=true;
        C(vliw_estadisticas(&f.e,p,&a,NULL)==VLIW_VIS_MEMORIA); activo=false; fallar=0;
        C(!memcmp(&a,&copia,sizeof a)); ++total;
    }
    vliw_liberar(&p); printf("S8-C fallos: %zu reservas inyectadas, 6 errores de generación PASS\n",total);
}
#endif

/* El protocolo de pruebas transporta texto en hexadecimal para incluir
 * saltos de línea/comillas sin crear un parser de ensamblador ni de BAL. */
static int hex(int c) { if (c>='0' && c<='9') return c-'0'; if (c>='a' && c<='f') return c-'a'+10; return -1; }
/* Lee o escribe el protocolo del fixture para que Python pueda ejecutar la implementación C real. */ static int fixture(int argc,char **argv)
{
    /* Leemos el fixture propio; formato y ancho provienen de esta invocación. */ C(argc==3 || argc==4); FILE *f=fopen(argv[2],"rb"); C(f!=NULL);
    size_t n,m,k,nt; C(fscanf(f,"%zu%zu%zu%zu",&n,&m,&k,&nt)==4); C(n<=4092 && m<=10000 && k<=n && nt<=n);
    VliwInstruccion *in=calloc(n+1,sizeof *in); VliwDependencia *ar=calloc(m+1,sizeof *ar);
    VliwBloque *bl=calloc(k+1,sizeof *bl); VliwDestino *ds=calloc(nt+1,sizeof *ds);
    C(in && ar && bl && ds);
    for (size_t j=0; j<n; ++j) {
        unsigned slot,control; unsigned long long lee,escribe;
        C(fscanf(f,"%zu%u%llx%llx%u%u%zu",&in[j].id,&slot,&lee,&escribe,&in[j].efectos,&control,&in[j].bloque)==7);
        in[j].slot=(VliwSlot)slot; in[j].control=(VliwControl)control; in[j].lee=lee; in[j].escribe=escribe;
    }
    for (size_t j=0; j<m; ++j) { unsigned tipo; C(fscanf(f,"%zu%zu%u%zu",&ar[j].antes,&ar[j].despues,&tipo,&ar[j].distancia)==4); ar[j].tipo=(VliwTipoDependencia)tipo; }
    for (size_t b=0; b<k; ++b) {
        C(fscanf(f,"%zu%zu%zu",&bl[b].inicio,&bl[b].fin,&bl[b].cantidad_sucesores)==3); C(bl[b].cantidad_sucesores<=k);
        size_t *su=calloc(bl[b].cantidad_sucesores+1,sizeof *su); C(su!=NULL); bl[b].sucesores=su;
        for (size_t j=0; j<bl[b].cantidad_sucesores; ++j) C(fscanf(f,"%zu",&su[j])==1);
    }
    /* Las anotaciones se cargan aparte del texto, sin resolver operandos. */ for (size_t j=0; j<nt; ++j) { unsigned tipo; C(fscanf(f,"%zu%u%zu%zu",&ds[j].instruccion,&tipo,&ds[j].tomado,&ds[j].caida)==4); ds[j].tipo=(VliwDestinoTipo)tipo; }
    int c; do { c=fgetc(f); } while (c!=EOF && c!='\n');
    char linea[32770];
    for (size_t j=0; j<n; ++j) {
        C(fgets(linea,sizeof linea,f)!=NULL); size_t longitud=strlen(linea); C(longitud && linea[longitud-1]=='\n'); linea[--longitud]=0;
        if (!strcmp(linea,"-")) continue;
        C(longitud%2==0); char *t=calloc(longitud/2+1,1); C(t!=NULL); in[j].texto=t;
        for (size_t z=0; z<longitud; z+=2) { int a=hex(linea[z]),b=hex(linea[z+1]); C(a>=0 && b>=0 && (a || b)); t[z/2]=(char)(16*a+b); }
    }
    C(fclose(f)==0); VliwEntrada e={in,n,ar,m,bl,k,true}; VliwDestinos destinos={ds,nt};
    VliwPlan *p=NULL; VliwDiagnostico sd; C(vliw_planificar(&e,&p,&sd)==VLIW_OK);
    VliwVisualOpciones opciones={48,true,true}; char *salida=NULL;
    if (argc==4 && strcmp(argv[1],"--contraste")) opciones.ancho_slot=(size_t)strtoul(argv[3],NULL,10);
#ifdef VLIW_VIS_EMISOR
    char *antes=NULL;
    if (!strcmp(argv[1],"--contraste")) {
        VliwEmisorDiagnostico ed; C(argc==4);
        VliwEmisorEstado es=vliw_emitir(&e,p,&destinos,&antes,&ed);
        if (es) fprintf(stderr,"S8-A: %s\n",ed.mensaje);
        C(es==VLIW_EM_OK);
    }
#endif
    VliwVisualFormato formato=VLIW_VIS_JSON;
    if (!strcmp(argv[1],"--tabla")) formato=VLIW_VIS_TABLA;
    else if (!strcmp(argv[1],"--csv")) formato=VLIW_VIS_CSV;
    else C(!strcmp(argv[1],"--json") || !strcmp(argv[1],"--estadisticas") || !strcmp(argv[1],"--contraste"));
    VliwVisualDiagnostico d;
    if (strcmp(argv[1],"--estadisticas")) C(vliw_visualizar(&e,p,&destinos,formato,&opciones,&salida,&d)==VLIW_VIS_OK);
    if (!strcmp(argv[1],"--estadisticas")) {
        VliwEstadisticas a; C(vliw_estadisticas(&e,p,&a,NULL)==VLIW_VIS_OK);
        printf("{\"instrucciones_utiles\":%zu,\"bundles_totales\":%zu,\"bundles_vacios\":%zu,\"bundles_con_operaciones\":%zu,\"bundles_proteccion\":%zu,\"slots_disponibles\":%zu,\"slots_utilizados\":%zu,\"slots_vacios\":%zu,\"bytes_imem\":%zu,\"bloques\":%zu,\"textos_ausentes\":%zu,\"utilizacion_porcentual\":%.17g,\"operaciones_por_bundle\":%.17g}\n",a.instrucciones_utiles,a.bundles_totales,a.bundles_vacios,a.bundles_con_operaciones,a.bundles_proteccion,a.slots_disponibles,a.slots_utilizados,a.slots_vacios,a.bytes_imem,a.bloques,a.textos_ausentes,a.utilizacion_porcentual,a.operaciones_por_bundle);
    } else C(fputs(salida,stdout)>=0);
#ifdef VLIW_VIS_EMISOR
    if (antes) {
        /* Mostrar los tres formatos no puede cambiar lo que emite S8-A. */
        for (unsigned z=0; z<3; ++z) { char *vista=NULL,*despues=NULL;
            C(vliw_visualizar(&e,p,&destinos,(VliwVisualFormato)z,NULL,&vista,NULL)==VLIW_VIS_OK);
            C(vliw_emitir(&e,p,&destinos,&despues,NULL)==VLIW_EM_OK && !strcmp(antes,despues));
            vliw_visual_liberar(&vista); vliw_emisor_liberar(&despues);
        }
        f=fopen(argv[3],"wb"); C(f!=NULL); C(fputs(antes,f)>=0 && fclose(f)==0); vliw_emisor_liberar(&antes);
    }
#endif
    vliw_visual_liberar(&salida); vliw_liberar(&p);
    /* Liberamos textos, sucesores y arreglos después de generar la vista. */ for (size_t j=0; j<n; ++j) free((void *)in[j].texto);
    for (size_t b=0; b<k; ++b) free((void *)bl[b].sucesores);
    free(in); free(ar); free(bl); free(ds); return fflush(stdout)!=0;
}
/* Selecciona el escenario autoverificable o el modo de sonda solicitado; sus códigos y salidas son comprobados por la suite Python. */ int main(int argc,char **argv)
{
#ifdef VLIW_VIS_FALLOS
    if (argc==2 && !strcmp(argv[1],"--fallos")) { fallos(); return 0; }
#endif
    if (argc>1) return fixture(argc,argv);
    basicos_y_metricas(); bloques_y_textos(); errores_y_propiedad(); localizacion(); capacidad();
    printf("S8-C: %zu escenarios, %zu comprobaciones PASS\n",casos,comprobaciones); return 0;
}
