/* Verifica M1 contra ciclos reales y un segundo simulador sin recolector.
 * Los fixtures se codifican mediante CB; no implementan otra ISA o ejecución. */
#include "sim_metricas.h"
#include "codificacion_binaria.h"
#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static size_t casos,checks;
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL M1 %d: %s\n",__LINE__,#x); exit(1); } } while (0)
/* Prepara la imagen del escenario. */ static SimImagen *imagen(const char *asm_)
{
    CbPrograma p={0}; C(cb_ensamblar(asm_,&p,stderr)==0);
    SimImagen *i=calloc(1,sizeof *i); C(i!=NULL); i->cantidad_bundles=p.cantidad;
    for (size_t b=0;b<p.cantidad;++b) memcpy(i->imem[b],p.bundles[b].slots,sizeof i->imem[b]);
    cb_liberar(&p); return i;
}
/* Inicializa arreglos y estado del fixture. */ static void iniciar(SimPipeline *s,SimMetricas *m,const SimImagen *i)
{
    C(sim_pipeline_iniciar(s,i)==SIM_PIPE_ACTIVO); C(sim_metricas_iniciar(m,s)==SIM_METRICAS_OK);
}
/* Exige igualdad de los campos públicos comprobados. */ static void iguales(const SimPipeline *a,const SimPipeline *b)
{
    C(a->pc==b->pc && a->ciclos==b->ciclos && a->estado==b->estado);
    C(a->bundles_admitidos==b->bundles_admitidos && a->bundles_completados==b->bundles_completados && a->bundles_anulados==b->bundles_anulados);
    C(!memcmp(a->registros,b->registros,sizeof a->registros)); C(!memcmp(a->memoria.bytes,b->memoria.bytes,65536));
    C(a->estado_crypto==b->estado_crypto && a->trap_count==b->trap_count && a->trap_pc==b->trap_pc);
    /* Solo el test inspecciona igualdad de almacenamiento privado. Ningún
     * getter ni reporte M1 permite extraer estos datos. */
    C(a->boveda.password==b->boveda.password && a->boveda.pwd_set==b->boveda.pwd_set);
    C(!memcmp(a->boveda.keys,b->boveda.keys,sizeof a->boveda.keys));
}
/* Avanza un flanco del simulador y observa M1 en ese mismo horizonte para no perder ni duplicar eventos. */ static void paso(SimPipeline *s,SimMetricas *m,SimPipeline *sin)
{
    uint64_t previo=s->ciclos; SimPipelineEstado e=sim_avanzar_ciclo(s);
    if (sin) { C(sim_avanzar_ciclo(sin)==e); iguales(s,sin); }
    C(sim_metricas_observar(m,s)==(s->ciclos==previo ? SIM_METRICAS_SIN_CICLO : SIM_METRICAS_OK));
    if (sin) iguales(s,sin);
}
/* Consulta métricas válidas antes de contrastar sus campos. */ static SimMetricasResumen resultado(SimMetricas *m)
{
    SimMetricasResumen r; C(sim_metricas_resultados(m,&r)==SIM_METRICAS_OK); return r;
}
/* Avanza hasta retirar los bundles pendientes de la búsqueda finita; comprueba los límites fijados por el fixture. */ static void drenar(SimPipeline *s,SimMetricas *m,SimPipeline *otro)
{
    for (unsigned n=0;n<200 && (s->estado==SIM_PIPE_ACTIVO || s->estado==SIM_PIPE_LIMITE);++n) paso(s,m,otro);
    C(s->estado==SIM_PIPE_TERMINADO);
}
/* Comprueba los contadores básicos, NOP y ejecución secuencial frente a los valores explícitos del banco. */ static void basicos(void)
{
/* Preparamos una operación útil aislada: antes de avanzar, ciclos, IPC y ocupación deben ser cero. */     ++casos; SimImagen *i=imagen("sumai x5,x0,7 | nop | nop | nop\n");
    SimPipeline s,t; SimMetricas m={0}; iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==SIM_PIPE_ACTIVO);
    SimMetricasResumen r=resultado(&m); C(r.ciclos==0 && r.ipc==0 && r.bundles_por_ciclo==0);
    C(sim_metricas_observar(&m,&s)==SIM_METRICAS_SIN_CICLO);
    C(sim_pipeline_ejecutar(&s,0)==SIM_PIPE_LIMITE && sim_pipeline_ejecutar(&t,0)==SIM_PIPE_LIMITE);
    C(sim_metricas_observar(&m,&s)==SIM_METRICAS_SIN_CICLO); C(resultado(&m).estado==SIM_PIPE_LIMITE);
/* Al drenar esta imagen de un bundle esperamos cuatro ciclos, una instrucción útil retirada y ninguna pendiente. */     ++casos; drenar(&s,&m,&t); r=resultado(&m);
    C(r.ciclos==4 && r.admitidos==1 && r.completados==1 && r.anulados==0 && r.pendientes==0);
    C(r.instrucciones_utiles==1 && r.instrucciones_por_slot[0]==1 && r.operaciones_isa[SIM_OP_SUMAI]==1);
    C(r.ipc==0.25 && r.bundles_por_ciclo==0.25 && r.registros[5]==7 && r.escrituras_por_puerto[0]==1);
    paso(&s,&m,&t); C(resultado(&m).ciclos==4);
    /* Exportar repetidamente debe ser estable y no revelar la bóveda. */ ++casos; char *json=NULL,*texto=NULL;
    C(sim_metricas_exportar(&m,SIM_METRICAS_JSON,&json)==SIM_METRICAS_OK);
    C(strstr(json,"\"naturaleza\":\"dinamica\"") && strstr(json,"\"ipc\":0.250000000"));
    C(!strstr(json,"password") && !strstr(json,"keys") && !strstr(json,"boveda"));
    C(sim_metricas_exportar(&m,SIM_METRICAS_TEXTO,&texto)==SIM_METRICAS_OK);
    C(strstr(texto,"Métricas dinámicas") && strstr(texto,"TERMINADO"));
    for (unsigned k=0;k<12;++k) { char *otro=NULL; C(sim_metricas_exportar(&m,SIM_METRICAS_JSON,&otro)==0 && !strcmp(json,otro)); sim_metricas_liberar_reporte(&otro); }
    if (setlocale(LC_NUMERIC,"es_CR.utf8")) {
        char *otro=NULL; C(sim_metricas_exportar(&m,SIM_METRICAS_JSON,&otro)==0 && !strcmp(json,otro));
        C(!strcmp(localeconv()->decimal_point,",")); sim_metricas_liberar_reporte(&otro); C(setlocale(LC_NUMERIC,"C")!=NULL);
    }
    sim_metricas_liberar_reporte(&json); sim_metricas_liberar_reporte(&json); sim_metricas_liberar_reporte(NULL);
    sim_metricas_liberar_reporte(&texto); sim_metricas_liberar(&m); free(i);
    /* Un bundle NOP se retira, pero su IPC e instrucciones son cero. */ ++casos; i=imagen("nop\n"); iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==0); drenar(&s,&m,&t); r=resultado(&m);
    C(r.ciclos==4 && r.completados==1 && r.bundles_nop==1 && r.bundles_utiles==0 && r.instrucciones_utiles==0 && r.ipc==0);
    sim_metricas_liberar(&m); free(i);
    ++casos; i=imagen("sumai x5,x0,7 | cargai x8,x0,0 | nop | nop\nnop\nnop\nsuma x6,x5,x8 | nop | nop | nop\n");
    i->dmem[0]=11; iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==0); drenar(&s,&m,&t); r=resultado(&m);
    C(r.ciclos==7 && r.completados==4 && r.bundles_nop==2 && r.bundles_utiles==2);
    C(r.instrucciones_utiles==3 && r.instrucciones_por_slot[0]==2 && r.instrucciones_por_slot[1]==1 && r.registros[6]==18);
    C(r.lsu_lecturas_ex==1 && r.escrituras_por_puerto[1]==1 && r.ipc==3.0/7.0);
    sim_metricas_liberar(&m); free(i);
}
/* Prepara o verifica el control de flujo del fixture para contrastar saltos, bloques y sus restricciones. */ static void control(void)
{
    ++casos; SimImagen *i=imagen("nop | nop | sye x0,fin | nop\nsumai x5,x0,1 | nop | nop | nop\nfin: sumai x6,x0,9 | nop | nop | nop\n");
    SimPipeline s,t; SimMetricas m={0}; iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==0); drenar(&s,&m,&t);
    SimMetricasResumen r=resultado(&m); C(r.ciclos==7 && r.admitidos==3 && r.completados==2 && r.anulados==1);
    C(r.instrucciones_utiles==2 && r.bru_ex==1 && r.saltos_tomados==1 && r.flush_salto==1 && r.saltos_no_tomados==0);
    C(r.anulados_salto==1 && r.busquedas_descartadas==1 && r.descartes_x0_por_slot[2]==1 && !r.registros[5] && r.registros[6]==9);
    sim_metricas_liberar(&m); free(i);
    /* La condición falsa no hace flush: se retiran las tres operaciones. */ ++casos; i=imagen("nop | nop | igualno x0,x0,fin | nop\nsumai x5,x0,1 | nop | nop | nop\nfin: sumai x6,x0,9 | nop | nop | nop\n");
    iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==0); drenar(&s,&m,&t); r=resultado(&m);
    C(r.ciclos==6 && r.completados==3 && r.instrucciones_utiles==3 && r.bru_ex==1 && r.saltos_no_tomados==1 && !r.flush_salto && !r.anulados);
    sim_metricas_liberar(&m); free(i);
    /* El bucle repite retiros reales; se esperan 23 ciclos y dos saltos tomados. */ ++casos; i=imagen("sumai x5,x0,3 | nop | nop | nop\nnop\nnop\nbucle: restai x5,x5,1 | nop | nop | nop\nnop\nnop\nnop | nop | igualno x5,x0,bucle | nop\nsuma x6,x5,x0 | nop | nop | nop\n");
    iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==0); drenar(&s,&m,&t); r=resultado(&m);
    C(r.ciclos==23 && r.completados==16 && r.admitidos==18 && r.anulados==2);
    C(r.instrucciones_utiles==8 && r.instrucciones_por_slot[0]==5 && r.instrucciones_por_slot[2]==3);
    C(r.saltos_tomados==2 && r.saltos_no_tomados==1 && r.flush_salto==2 && r.busquedas_descartadas==0 && !r.registros[5]);
    sim_metricas_liberar(&m); free(i);
}
/* Comprueba reset de épocas, duplicados y observaciones fuera de secuencia sin sumar flancos inexistentes. */ static void epocas_y_errores(void)
{
    ++casos; SimImagen *i=imagen("sumai x5,x0,7 | nop | nop | nop\n"); SimPipeline s,t;
    SimMetricas m={0},n={0}; iniciar(&s,&m,i); iniciar(&t,&n,i);
    paso(&s,&m,NULL); C(resultado(&n).ciclos==0); C(sim_metricas_observar(&m,&t)==SIM_METRICAS_EPOCA);
    /* Un límite de un ciclo se observa igual que un avance directo. */
    C(sim_pipeline_ejecutar(&t,1)==SIM_PIPE_LIMITE); C(sim_metricas_observar(&n,&t)==SIM_METRICAS_OK);
    C(resultado(&n).ciclos==1); C(sim_metricas_observar(&n,&t)==SIM_METRICAS_SIN_CICLO);
    paso(&s,&m,NULL); paso(&t,&n,NULL); C(resultado(&m).ciclos==2 && resultado(&n).ciclos==2);
    SimMetricasResumen antes=resultado(&m);
    /* Reset exige época nueva; el snapshot previo conserva sus dos ciclos. */ ++casos; C(sim_pipeline_reiniciar(&s)==0); C(sim_metricas_observar(&m,&s)==SIM_METRICAS_EPOCA);
    C(sim_metricas_iniciar(&m,&s)==0 && m.acumulado.epoca==2); C(resultado(&m).ciclos==0 && antes.ciclos==2);
    drenar(&s,&m,NULL); C(resultado(&m).ciclos==4 && resultado(&m).instrucciones_utiles==1);
    sim_metricas_liberar(&m); sim_metricas_liberar(&n); free(i);
    /* Reset puede publicar un store que ya estaba en EX, sin sumar reloj.
     * La época siguiente parte de esa DMEM; no se inventa un acceso ejecutado. */
    ++casos; i=imagen("nop | guardap x0,x0,4 | nop | nop\n"); i->dmem[4]=9;
    iniciar(&s,&m,i); paso(&s,&m,NULL); paso(&s,&m,NULL); antes=resultado(&m);
    C(sim_pipeline_reiniciar(&s)==0 && s.memoria.bytes[4]==0);
    C(sim_metricas_resultados(&m,&antes)==SIM_METRICAS_EPOCA);
    C(sim_metricas_iniciar(&m,&s)==0 && resultado(&m).ciclos==0 && !resultado(&m).lsu_escrituras_ex);
    drenar(&s,&m,NULL); C(resultado(&m).lsu_escrituras_ex==1);
    sim_metricas_liberar(&m); free(i);
    /* Omitir dos flancos impide reconstruir M1: SECUENCIA sin mutar acumulados. */ ++casos; i=imagen("nop\nnop\nnop\n"); iniciar(&s,&m,i);
    C(sim_pipeline_ejecutar(&s,2)==SIM_PIPE_LIMITE); SimMetricas guardado=m;
    C(sim_metricas_observar(&m,&s)==SIM_METRICAS_SECUENCIA && !memcmp(&m,&guardado,sizeof m));
    SimMetricasResumen r; C(sim_metricas_resultados(&m,&r)==SIM_METRICAS_SECUENCIA);
    C(sim_metricas_iniciar(&m,&s)==SIM_METRICAS_EPOCA); sim_metricas_liberar(&m); free(i);
    /* ID rechaza el opcode y NO publica el ciclo intentado. ultimo_ciclo
     * continúa siendo el aceptado antes; M1 no repite eventos ni crea traps. */
    ++casos; i=imagen("nop\n"); i->imem[0][0]=1; iniciar(&s,&m,i); paso(&s,&m,NULL);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_DECODIFICACION && s.ciclos==1);
    C(sim_metricas_observar(&m,&s)==SIM_METRICAS_SIN_CICLO); r=resultado(&m);
    C(r.ciclos==1 && !r.instrucciones_utiles && !r.traps && r.estado==SIM_PIPE_DECODIFICACION);
    sim_metricas_liberar(&m); free(i);
    ++casos; i=imagen("nop\n"); iniciar(&s,&m,i); s.pc=3;
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_PC && !s.ciclos);
    /* El PC fue inyectado externamente: comenzar una nueva observación no
     * legitima esa mutación dentro de la época anterior. */
    C(sim_metricas_observar(&m,&s)==SIM_METRICAS_EPOCA); sim_metricas_liberar(&m); free(i);
}
/* Comprueba los eventos de memoria y el historial de excepciones observados por M1. */ static void memoria_y_traps(void)
{
    ++casos; SimImagen *i=imagen("sumai x5,x0,7 | nop | nop | nop\nnop\nnop\nnop | guardap x0,x5,4 | nop | nop\nnop | cargai x7,x0,4 | nop | nop\n");
    SimPipeline s,t; SimMetricas m={0}; iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==0); drenar(&s,&m,&t);
    SimMetricasResumen r=resultado(&m); C(r.ciclos==8 && r.instrucciones_utiles==3 && r.lsu_lecturas_ex==1 && r.lsu_escrituras_ex==1 && r.registros[7]==7 && s.memoria.bytes[4]==7);
    sim_metricas_liberar(&m); free(i);
    /* Las operaciones hacia x0 cuentan como instrucciones, sin escribir GPR. */ ++casos; i=imagen("sumai x0,x0,9 | cargai x0,x0,0 | nop | nop\nnop | nop | sye x0,fin | nop\nfin: nop\n");
    iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==0); drenar(&s,&m,&t); r=resultado(&m);
    C(r.instrucciones_utiles==3 && r.descartes_x0_por_slot[0]==1 && r.descartes_x0_por_slot[1]==1 && r.descartes_x0_por_slot[2]==1);
    C(!r.escrituras_por_puerto[0] && !r.escrituras_por_puerto[1] && !r.escrituras_por_puerto[2]);
    sim_metricas_liberar(&m); free(i);
    /* CB impide WAW en un bundle. Se combinan palabras CB legales solo como
     * estímulo deliberado de conflicto, sin codificarlas manualmente. */
    ++casos; i=imagen("sumai x5,x0,7 | nop | nop | nop\n");
    SimImagen *load=imagen("nop | cargai x5,x0,0 | nop | nop\n"); i->imem[0][1]=load->imem[0][1]; free(load); i->dmem[0]=11;
    iniciar(&s,&m,i); C(sim_pipeline_iniciar(&t,i)==0); drenar(&s,&m,&t); r=resultado(&m);
    C(r.instrucciones_utiles==2 && r.escrituras_por_puerto[0]==1 && r.escrituras_por_puerto[1]==1 && r.conflictos_registro==1 && r.registros[5]==11);
    sim_metricas_liberar(&m); free(i);
    ++casos; i=imagen("sumai x10,x0,42 | nop | nop | nop\nsumai x8,x0,7 | guardap x0,x0,4 | sye x0,0 | fsl x4,x2,0,0\nnop\n");
    SimImagen *handler=imagen("sumai x12,x0,77 | nop | nop | nop\n");
    memcpy(i->imem[1023],handler->imem[0],sizeof i->imem[0]); free(handler); i->cantidad_bundles=1024;
    C(sim_pipeline_iniciar(&s,i)==0 && sim_pipeline_configurar_busqueda(&s,SIM_BUSQUEDA_FISICA)==0 && sim_metricas_iniciar(&m,&s)==0);
    C(sim_pipeline_iniciar(&t,i)==0 && sim_pipeline_configurar_busqueda(&t,SIM_BUSQUEDA_FISICA)==0);
    for (unsigned k=0;k<8;++k) paso(&s,&m,&t);
    r=resultado(&m); C(r.traps==1 && r.ultimo_trap.ciclo==4 && r.ultimo_trap.pc==16 && r.ultimo_trap.vector==0x3ff0);
    C(r.anulados_excepcion==2 && !r.bru_ex && !r.saltos_tomados && !r.lsu_escrituras_ex);
    C(r.instrucciones_por_slot[0]==2 && !r.instrucciones_por_slot[1] && !r.instrucciones_por_slot[2] && !r.instrucciones_por_slot[3]);
    C(r.registros[10]==42 && r.registros[12]==77 && !r.registros[8] && !s.memoria.bytes[4]);
    SimMetricaTrap tr; C(sim_metricas_trap(&m,0,&tr)==0 && tr.pc==16); C(sim_metricas_trap(&m,1,&tr)==SIM_METRICAS_ARGUMENTO);
    sim_metricas_liberar(&m); free(i);
    /* Fuera de la extensión cargada se retiran NOP; los bits altos del PC
     * no deben indexar fuera de IMEM ni impedir un retiro por aliasing. */
    ++casos; i=imagen("sumai x5,x0,7 | nop | nop | nop\n");
    C(sim_pipeline_iniciar(&s,i)==0 && sim_pipeline_configurar_busqueda(&s,SIM_BUSQUEDA_FISICA)==0); s.pc=0x4000;
    C(sim_metricas_iniciar(&m,&s)==0); for (unsigned k=0;k<5;++k) paso(&s,&m,NULL);
    r=resultado(&m); C(r.instrucciones_utiles==1 && r.registros[5]==7 && r.completados==2 && r.bundles_nop==1);
    sim_metricas_liberar(&m); free(i);
}
/* Prueba argumentos inválidos y diferencias de métricas, incluidos signo y denominador cero. */ static void argumentos_y_comparacion(void)
{
    ++casos; SimMetricas m={0}; SimPipeline s={0}; SimMetricasResumen r={0},copia=r;
    C(sim_metricas_iniciar(NULL,&s)==SIM_METRICAS_ARGUMENTO && sim_metricas_iniciar(&m,NULL)==SIM_METRICAS_ARGUMENTO);
    C(sim_metricas_observar(&m,&s)==SIM_METRICAS_ARGUMENTO && sim_metricas_resultados(&m,&r)==SIM_METRICAS_ARGUMENTO);
    C(!memcmp(&r,&copia,sizeof r)); C(sim_metricas_resultados(NULL,NULL)==SIM_METRICAS_ARGUMENTO);
    char *out=NULL; C(sim_metricas_exportar(&m,SIM_METRICAS_JSON,&out)==SIM_METRICAS_ARGUMENTO && !out);
    SimImagen *i=imagen("nop\n"); iniciar(&s,&m,i);
    C(sim_metricas_exportar(&m,(SimMetricasFormato)-1,&out)==SIM_METRICAS_ARGUMENTO);
    C(sim_metricas_exportar(&m,SIM_METRICAS_JSON,NULL)==SIM_METRICAS_ARGUMENTO);
    C(sim_metricas_exportar(&m,SIM_METRICAS_JSON,&out)==0); char *antes=out;
    C(sim_metricas_exportar(&m,SIM_METRICAS_JSON,&out)==SIM_METRICAS_ARGUMENTO && out==antes); sim_metricas_liberar_reporte(&out);
    r=resultado(&m); SimMetricasResumen b=r,cero=r; SimMetricasComparacion delta;
    C(sim_metricas_comparar(&r,&b,&delta)==0 && !delta.porcentaje_definido && delta.cambio_porcentual_ciclos==0);
    /* Comparación de ejecuciones reales, con iguales GPR y DMEM completa.
     * El PC numérico depende del layout y se comprueba por separado. */
    SimImagen *base=imagen("sumai x5,x0,7 | nop | nop | nop\nnop\nnop\nnop | cargai x8,x0,0 | nop | nop\nnop\nnop\nsuma x6,x5,x8 | nop | nop | nop\nnop\nnop\n");
    SimImagen *opt=imagen("sumai x5,x0,7 | cargai x8,x0,0 | nop | nop\nnop\nnop\nsuma x6,x5,x8 | nop | nop | nop\n");
    base->dmem[0]=11; opt->dmem[0]=11;
    SimPipeline sa,sb; SimMetricas ma={0},mb={0}; iniciar(&sa,&ma,base); iniciar(&sb,&mb,opt);
    drenar(&sa,&ma,NULL); drenar(&sb,&mb,NULL); r=resultado(&ma); b=resultado(&mb);
    C(!memcmp(sa.registros,sb.registros,sizeof sa.registros) && !memcmp(sa.memoria.bytes,sb.memoria.bytes,65536));
    C(sa.estado_crypto==sb.estado_crypto && sa.trap_count==sb.trap_count && r.registros[6]==18);
    C(r.ciclos==12 && b.ciclos==7 && r.instrucciones_utiles==3 && b.instrucciones_utiles==3);
    C(sim_metricas_comparar(&r,&b,&delta)==0 && delta.ciclos.negativa && delta.ciclos.magnitud==5 && !delta.instrucciones.magnitud);
    C(delta.cambio_porcentual_ciclos < -41.6666 && delta.cambio_porcentual_ciclos > -41.6667);
    C(delta.diferencia_ipc>0.178571 && delta.diferencia_ipc<0.178572);
    sim_metricas_liberar(&ma); sim_metricas_liberar(&mb); free(base); free(opt);
    C(sim_metricas_comparar(&cero,&b,&delta)==0 && delta.instrucciones.magnitud==3 && !delta.instrucciones.negativa && !delta.porcentaje_definido);
    /* El delta extremo debe conservar toda la magnitud sin underflow. */ r.ciclos=UINT64_MAX; b.ciclos=0; C(sim_metricas_comparar(&r,&b,&delta)==0 && delta.ciclos.magnitud==UINT64_MAX);
    C(sim_metricas_comparar(NULL,&b,&delta)==SIM_METRICAS_ARGUMENTO); b.valido=false; C(sim_metricas_comparar(&r,&b,&delta)==SIM_METRICAS_ARGUMENTO);
    sim_metricas_liberar(&m); sim_metricas_liberar(&m); sim_metricas_liberar(NULL); free(i);
}
#ifdef SIM_METRICAS_FALLOS
/* Rutina real realloc: destino de las llamadas no inyectadas. */ void *__real_realloc(void *,size_t); /* Rutina real vsnprintf: destino de las llamadas no inyectadas. */ int __real_vsnprintf(char *,size_t,const char *,va_list);
static size_t contar,fallar,llamada,fallar_formato; static bool activo;
/* Envuelve realloc: inyecta fallos y delega el resto. */ void *__wrap_realloc(void *p,size_t n) { if (activo && ++contar==fallar) return NULL; return __real_realloc(p,n); }
/* Envuelve vsnprintf: inyecta fallos y delega el resto. */ int __wrap_vsnprintf(char *s,size_t n,const char *f,va_list a) { if (activo && ++llamada==fallar_formato) return -1; return __real_vsnprintf(s,n,f,a); }
#ifdef __GLIBC__
/* Rutina real __vsnprintf_chk: destino de las llamadas no inyectadas. */ int __real___vsnprintf_chk(char *,size_t,int,size_t,const char *,va_list);
/* Envuelve __vsnprintf_chk: inyecta fallos y delega el resto. */ int __wrap___vsnprintf_chk(char *s,size_t n,int flag,size_t cap,const char *f,va_list a) { if (activo && ++llamada==fallar_formato) return -1; return __real___vsnprintf_chk(s,n,flag,cap,f,a); }
#endif
/* Inyecta errores en reservas, archivos o generación y comprueba que no se publique una salida parcial ni se pierdan recursos. */ static void fallos(void)
{
    /* Inyectamos fallos de M1 tras crear el fixture, sin alterar el núcleo. */ SimImagen *i=imagen("nop | nop | nop | fsl x4,x2,0,0\n"); SimPipeline s; SimMetricas m={0};
    memcpy(i->imem[1023],i->imem[0],sizeof i->imem[0]); i->cantidad_bundles=1024;
    C(sim_pipeline_iniciar(&s,i)==0 && sim_pipeline_configurar_busqueda(&s,SIM_BUSQUEDA_FISICA)==0);
    C(sim_metricas_iniciar(&m,&s)==0); paso(&s,&m,NULL); paso(&s,&m,NULL); C(sim_avanzar_ciclo(&s)==0);
    SimMetricas antes=m; activo=true; contar=0; fallar=1;
    C(sim_metricas_observar(&m,&s)==SIM_METRICAS_MEMORIA); activo=false; fallar=0;
    C(!memcmp(&m,&antes,sizeof m)); C(sim_metricas_observar(&m,&s)==0 && resultado(&m).traps==1);
    size_t total=1;
    for (unsigned f=0;f<2;++f) {
        char *out=NULL; activo=true; contar=0; C(sim_metricas_exportar(&m,(SimMetricasFormato)f,&out)==0); activo=false;
        size_t n=contar; sim_metricas_liberar_reporte(&out);
        for (size_t k=1;k<=n;++k) { activo=true; contar=0; fallar=k; C(sim_metricas_exportar(&m,(SimMetricasFormato)f,&out)==SIM_METRICAS_MEMORIA && !out); activo=false; fallar=0; ++total; }
        for (size_t k=1;k<=2;++k) { activo=true; llamada=0; fallar_formato=k; C(sim_metricas_exportar(&m,(SimMetricasFormato)f,&out)==SIM_METRICAS_GENERACION && !out); activo=false; fallar_formato=0; }
    }
    while (s.trap_count<4) paso(&s,&m,NULL);
    while (s.trap_count<5) {
        C(sim_avanzar_ciclo(&s)==SIM_PIPE_ACTIVO);
        if (s.trap_count==5) {
            SimMetricas previo=m; activo=true; contar=0; fallar=1;
            C(sim_metricas_observar(&m,&s)==SIM_METRICAS_MEMORIA); activo=false; fallar=0;
            C(!memcmp(&previo,&m,sizeof m)); C(sim_metricas_observar(&m,&s)==0 && resultado(&m).traps==5); ++total;
        } else C(sim_metricas_observar(&m,&s)==0);
    }
    sim_metricas_liberar(&m); free(i); printf("M1 fallos: %zu reservas, 4 errores de generación PASS\n",total);
}
#endif

/* Auxiliar de pruebas: carga MEM/BALB con S1, avanza mediante S3–S6 y
 * escribe DMEM completa solo en la ruta de prueba solicitada. */
static int archivo(int argc,char **argv)
{
    C(argc==8); SimImagen *i=NULL;
    C((!strcmp(argv[1],"balb")?sim_cargar_balb:sim_cargar_mem)(argv[2],&i,stderr)==SIM_CARGA_OK);
    if (strcmp(argv[3],"-")) { FILE *f=fopen(argv[3],"rb"); C(f && fread(i->dmem,1,65536,f)==65536); C(fclose(f)==0); }
    SimPipeline s,t; SimMetricas m={0}; C(sim_pipeline_iniciar(&s,i)==0 && sim_pipeline_iniciar(&t,i)==0);
    if (!strcmp(argv[5],"fisica")) C(sim_pipeline_configurar_busqueda(&s,SIM_BUSQUEDA_FISICA)==0 && sim_pipeline_configurar_busqueda(&t,SIM_BUSQUEDA_FISICA)==0);
    C(sim_metricas_iniciar(&m,&s)==0); uint64_t n=strtoull(argv[4],NULL,10);
    for (uint64_t j=0;j<n && (s.estado==SIM_PIPE_ACTIVO || s.estado==SIM_PIPE_LIMITE);++j) paso(&s,&m,&t);
    if (s.estado==SIM_PIPE_ACTIVO) { C(sim_pipeline_ejecutar(&s,0)==SIM_PIPE_LIMITE && sim_pipeline_ejecutar(&t,0)==SIM_PIPE_LIMITE); C(sim_metricas_observar(&m,&s)==SIM_METRICAS_SIN_CICLO); }
    iguales(&s,&t); char *out=NULL;
    C(sim_metricas_exportar(&m,!strcmp(argv[6],"json")?SIM_METRICAS_JSON:SIM_METRICAS_TEXTO,&out)==0);
    C(fputs(out,stdout)>=0); sim_metricas_liberar_reporte(&out);
    FILE *f=fopen(argv[7],"wb"); C(f && fwrite(s.memoria.bytes,1,65536,f)==65536 && fclose(f)==0);
    sim_metricas_liberar(&m); sim_liberar_imagen(&i); return fflush(stdout)!=0;
}
/* Selecciona el escenario autoverificable o el modo de sonda solicitado; sus códigos y salidas son comprobados por la suite Python. */ int main(int argc,char **argv)
{
#ifdef SIM_METRICAS_FALLOS
    if (argc==2 && !strcmp(argv[1],"fallos")) { fallos(); return 0; }
#endif
    if (argc>1) return archivo(argc,argv);
    basicos(); control(); epocas_y_errores(); memoria_y_traps(); argumentos_y_comparacion();
    printf("M1: %zu escenarios, %zu comprobaciones PASS\n",casos,checks); return 0;
}
