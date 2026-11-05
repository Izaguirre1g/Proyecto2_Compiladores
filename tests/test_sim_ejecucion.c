/* Comprueba la coordinación pública frente a S1–S6 sin instrumentación.
 * Los fallos de reserva se inyectan solo desde este ejecutable de pruebas. */
#include "sim_ejecucion.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static size_t checks, comparaciones_flancos;
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL M2 %d: %s\n",__LINE__,#x); exit(1); } } while (0)
/* --wrap del enlazador activa estas reservas interceptadas solo en el test.
 * Un índice distinto de cero selecciona la llamada que devolverá NULL; las
 * demás llegan al asignador real. No se cambia el código de producción. */
#ifdef SIM_EJECUCION_FALLOS
static size_t fallar_calloc, fallar_realloc, llamadas_calloc, llamadas_realloc;
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
/* Permite fallar por separado la sesión y la imagen para ejercitar la
 * limpieza parcial. Las comprobaciones exigen que no se publique una sesión. */
void *__wrap_calloc(size_t n, size_t z)
{
    ++llamadas_calloc;
    if (fallar_calloc && llamadas_calloc == fallar_calloc) return NULL;
    return __real_calloc(n,z);
}
/* Al rechazar realloc no se libera p: M1 debe conservar lo ya reservado y
 * permitir un reintento sin perder historial ni duplicar el evento pendiente. */
void *__wrap_realloc(void *p, size_t n)
{
    ++llamadas_realloc;
    if (fallar_realloc && llamadas_realloc == fallar_realloc) return NULL;
    return __real_realloc(p,n);
}
#endif

/* Además de pedir un snapshot coherente a M1, se comprueba que todo bundle
 * admitido esté retirado, anulado o pendiente. El IPC debe usar instrucciones
 * útiles retiradas y ciclos reales, con cero definido para una época vacía. */
static SimMetricasResumen resultado(SimEjecucion *s)
{
    SimMetricasResumen r;
    C(sim_ejecucion_resultados(s,&r)==SIM_METRICAS_OK);
    C(r.admitidos==r.completados+r.anulados+r.pendientes);
    C(r.ipc==(r.ciclos ? (double)r.instrucciones_utiles/(double)r.ciclos : 0));
    return r;
}

/* PC, ciclos, estado y bundles detectan cambios de temporización o retiros;
 * GPR y DMEM detectan efectos arquitectónicos distintos. La ocupación pendiente
 * y AUTH/INIT/traps también deben coincidir con S6 directo. La API opaca no
 * permite inspeccionar la bóveda privada como parte de esta comparación. */
static void iguales(SimEjecucion *s, const SimPipeline *ref)
{
    SimMetricasResumen r=resultado(s); SimEjecucionEstado e;
    C(sim_ejecucion_estado(s,&e)==0);
    C(r.pc==ref->pc && r.ciclos==ref->ciclos && r.estado==ref->estado && e.pipeline==ref->estado);
    C(r.admitidos==ref->bundles_admitidos && r.completados==ref->bundles_completados && r.anulados==ref->bundles_anulados);
    C(!memcmp(r.registros,ref->registros,sizeof r.registros));
    uint8_t memoria[65536]; C(sim_ejecucion_memoria(s,memoria,sizeof memoria)==0);
    /* Una huella igual no prueba igualdad byte a byte: se coteja la DMEM
     * completa para detectar también escrituras fuera de las zonas esperadas. */
    C(!memcmp(memoria,ref->memoria.bytes,sizeof memoria));
    C(r.estado_crypto==ref->estado_crypto && r.trap_count32==ref->trap_count && r.trap_pc==ref->trap_pc);
    C(r.pendientes_por_etapa[0]==(unsigned)ref->if_id.valido);
    C(r.pendientes_por_etapa[1]==(unsigned)ref->id_ex.valido);
    C(r.pendientes_por_etapa[2]==(unsigned)ref->ex_wb.valido);
}

static SimEjecucion *crear(const char *ruta, bool balb, bool fisica)
{
    SimEjecucion *s=NULL;
    C(sim_ejecucion_crear(ruta,balb?SIM_EJECUCION_BALB:SIM_EJECUCION_MEM,
        fisica?SIM_BUSQUEDA_FISICA:SIM_BUSQUEDA_FINITA,&s,stderr)==0);
    C(s!=NULL); return s;
}

/* Casos de ciclo de vida: validación transaccional, cero ciclos, pausa,
 * reanudación, terminación estable, snapshots propios y sesiones aisladas. */
static void unitarias(const char *nop, const char *store, const char *trap)
{
    SimEjecucion *s=NULL; SimMetricasResumen r; SimEjecucionEstado e;
    C(sim_ejecucion_crear(NULL,0,0,&s,NULL)==SIM_EJECUCION_ARGUMENTO && !s);
    C(sim_ejecucion_crear("",0,0,&s,NULL)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_crear(nop,9,0,&s,NULL)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_crear(nop,0,9,&s,NULL)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_crear(nop,0,0,NULL,NULL)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_crear("/no/existe/m2",0,0,&s,NULL)==SIM_EJECUCION_CARGA && !s);
    C(sim_ejecucion_avanzar(NULL)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_ejecutar(NULL,0)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_reiniciar(NULL,false)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_reintentar(NULL)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_estado(NULL,&e)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_resultados(NULL,&r)==SIM_METRICAS_ARGUMENTO);
    C(sim_ejecucion_trap(NULL,0,NULL)==SIM_METRICAS_ARGUMENTO);
    C(sim_ejecucion_exportar(NULL,0,NULL)==SIM_METRICAS_ARGUMENTO);
    /* Una salida ocupada conserva su sesión. Las consultas inválidas tampoco
     * deben convertir esa instancia válida en una sesión parcialmente creada. */
    s=crear(nop,false,false); SimEjecucion *anterior=s;
    C(sim_ejecucion_crear(nop,0,0,&s,NULL)==SIM_EJECUCION_ARGUMENTO && s==anterior);
    C(sim_ejecucion_estado(s,NULL)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_resultados(s,NULL)==SIM_METRICAS_ARGUMENTO);
    C(sim_ejecucion_trap(s,0,NULL)==SIM_METRICAS_ARGUMENTO);
    C(sim_ejecucion_exportar(s,0,NULL)==SIM_METRICAS_ARGUMENTO);
    uint8_t mem[65536]; C(sim_ejecucion_memoria(NULL,mem,sizeof mem)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_memoria(s,NULL,sizeof mem)==SIM_EJECUCION_ARGUMENTO);
    C(sim_ejecucion_memoria(s,mem,65535)==SIM_EJECUCION_ARGUMENTO);
    /* Cero publica LIMITE sin reloj. Dos intentos dejan el NOP aún pendiente;
     * reanudar con un presupuesto enorme termina al cuarto flanco, sin relleno. */
    C(sim_ejecucion_ejecutar(s,0)==0); r=resultado(s);
    C(r.ciclos==0 && r.estado==SIM_PIPE_LIMITE && r.ipc==0);
    C(sim_ejecucion_ejecutar(s,2)==0); r=resultado(s);
    C(r.ciclos==2 && r.completados==0 && r.estado==SIM_PIPE_LIMITE);
    SimEjecucion *b=crear(nop,false,false); SimMetricasResumen guardado=resultado(b);
    C(sim_ejecucion_ejecutar(s,UINT64_MAX)==0); r=resultado(s);
    C(r.ciclos==4 && r.estado==SIM_PIPE_TERMINADO && r.bundles_nop==1 && r.instrucciones_utiles==0);
    C(resultado(b).ciclos==guardado.ciclos && resultado(b).epoca==guardado.epoca);
    /* Una vez drenado, avanzar o reintentar no debe inventar otro ciclo ni
     * retirar de nuevo el NOP. B debe seguir en su estado inicial independiente. */
    C(sim_ejecucion_avanzar(s)==0 && sim_ejecucion_ejecutar(s,0)==0 && resultado(s).ciclos==4);
    C(sim_ejecucion_reintentar(s)==0 && resultado(s).ciclos==4);
    /* Exportación conserva formato, propiedad y publicación transaccional:
     * rechazar un destino ya ocupado no lo reemplaza. Tampoco revela la bóveda. */
    char *j=NULL,*t=NULL; C(sim_ejecucion_exportar(s,99,&j)==SIM_METRICAS_ARGUMENTO && !j);
    C(sim_ejecucion_exportar(s,SIM_METRICAS_JSON,&j)==0);
    char *previo=j; C(sim_ejecucion_exportar(s,0,&j)==SIM_METRICAS_ARGUMENTO && j==previo);
    C(sim_ejecucion_exportar(s,SIM_METRICAS_TEXTO,&t)==0 && strstr(t,"Métricas dinámicas"));
    C(!strstr(j,"password") && !strstr(j,"keys") && !strstr(j,"boveda"));
    sim_metricas_liberar_reporte(&j); sim_metricas_liberar_reporte(&t);
    /* El reset reemplaza la época de A, pero no su snapshot anterior por valor
     * ni la época de B. La comparación de M1 usa esos snapshots independientes. */
    C(sim_ejecucion_reiniciar(s,false)==0 && resultado(s).epoca==2 && resultado(s).ciclos==0);
    C(r.ciclos==4 && r.epoca==1 && resultado(b).epoca==1);
    SimMetricasComparacion cmp; SimMetricasResumen nuevo=resultado(s);
    C(sim_metricas_comparar(&r,&nuevo,&cmp)==0 && cmp.ciclos.negativa && cmp.ciclos.magnitud==4);
    sim_ejecucion_liberar(&s); sim_ejecucion_liberar(&s); sim_ejecucion_liberar(NULL);
    sim_ejecucion_liberar(&b);
    /* Dos flancos dejan el store en EX: el reset S6 publica un cero sobre el
     * valor 9 inicial de DMEM. La época nueva empieza sin ciclos ni accesos LSU.
     * Restaurar después debe recuperar el 9; S6 directo y toda DMEM actúan
     * como referencia para comprobar también el orden reset/restauración. */
    SimImagen *imagen=NULL; C(sim_cargar_balb(store,&imagen,stderr)==0);
    SimPipeline ref; C(sim_pipeline_iniciar(&ref,imagen)==0);
    s=crear(store,true,false); C(sim_ejecucion_ejecutar(s,2)==0);
    C(sim_pipeline_ejecutar(&ref,2)==SIM_PIPE_LIMITE); iguales(s,&ref);
    C(sim_ejecucion_memoria(s,mem,sizeof mem)==0 && mem[4]==9);
    C(sim_ejecucion_reiniciar(s,false)==0);
    C(sim_pipeline_reiniciar(&ref)==SIM_PIPE_ACTIVO); iguales(s,&ref);
    C(sim_ejecucion_memoria(s,mem,sizeof mem)==0 && mem[4]==0);
    r=resultado(s); C(r.epoca==2 && r.ciclos==0 && r.lsu_escrituras_ex==0);
    C(sim_ejecucion_reiniciar(s,true)==0);
    C(sim_pipeline_reiniciar(&ref)==SIM_PIPE_ACTIVO);
    C(sim_pipeline_restaurar_memoria(&ref)==SIM_PIPE_ACTIVO); iguales(s,&ref);
    C(sim_ejecucion_memoria(s,mem,sizeof mem)==0 && mem[4]==9);
    C(resultado(s).epoca==3 && resultado(s).ciclos==0);
    sim_liberar_imagen(&imagen);
    sim_ejecucion_liberar(&s);
    /* A ejecuta un store y B un NOP. Avanzar y restaurar A no debe cambiar
     * memoria, reloj o época de B; B puede avanzar entre ambas operaciones. */
    s=crear(store,true,false); b=crear(nop,false,false);
    C(sim_ejecucion_ejecutar(s,4)==0);
    C(sim_ejecucion_memoria(b,mem,sizeof mem)==0 && mem[4]==0);
    C(resultado(b).ciclos==0 && resultado(b).epoca==1);
    C(sim_ejecucion_ejecutar(b,1)==0);
    C(sim_ejecucion_reiniciar(s,true)==0);
    C(resultado(b).ciclos==1 && resultado(b).epoca==1);
    C(sim_ejecucion_memoria(s,mem,sizeof mem)==0 && mem[4]==9);
    sim_ejecucion_liberar(&s); sim_ejecucion_liberar(&b);
    /* El fixture vuelve a provocar traps en el vector: en 20 ciclos físicos
     * se esperan seis entradas, con la primera en el ciclo 3. Reset reemplaza
     * ese historial y conserva FISICA, que sigue más allá del código cargado. */
    s=crear(trap,false,true); C(sim_ejecucion_ejecutar(s,20)==0); r=resultado(s);
    C(r.traps==6 && r.ciclos==20 && r.estado==SIM_PIPE_LIMITE);
    SimMetricaTrap h; C(sim_ejecucion_trap(s,0,&h)==0 && h.ciclo==3);
    C(sim_ejecucion_trap(s,6,&h)==SIM_METRICAS_ARGUMENTO);
    C(sim_ejecucion_reiniciar(s,true)==0 && resultado(s).epoca==2);
    C(sim_ejecucion_trap(s,0,&h)==SIM_METRICAS_ARGUMENTO);
    C(sim_ejecucion_ejecutar(s,100)==0 && resultado(s).ciclos==100 && resultado(s).estado==SIM_PIPE_LIMITE);
    sim_ejecucion_liberar(&s);
#ifdef SIM_EJECUCION_FALLOS
    /* Fallar cada calloc de creación obliga a limpiar lo adquirido y dejar
     * s en NULL. La repetición y los sanitizadores comprueban estas rutas sin
     * permitir que una sesión incompleta llegue al cliente. */
    for (size_t k=1;k<=2;++k) {
        llamadas_calloc=0; fallar_calloc=k;
        C(sim_ejecucion_crear(nop,0,0,&s,NULL)==SIM_EJECUCION_MEMORIA && !s);
        fallar_calloc=0;
    }
    /* El trap 1 reserva historial y el 5 lo amplía de cuatro a ocho entradas.
     * En ambos casos S6 ya aceptó el flanco cuando falla realloc. Avance, reset
     * y consultas coherentes deben bloquearse hasta reobservar ese mismo trap;
     * una falla fatal no sería recuperable mediante este mecanismo MEMORIA. */
    for (unsigned objetivo=1;objetivo<=5;objetivo+=4) {
        s=crear(trap,false,true); unsigned ciclo=3*objetivo;
        C(sim_ejecucion_ejecutar(s,ciclo-1)==0);
        llamadas_realloc=0; fallar_realloc=1;
        C(sim_ejecucion_avanzar(s)==SIM_EJECUCION_PENDIENTE);
        C(sim_ejecucion_estado(s,&e)==0 && e.pendiente && !e.fatal && e.metricas==SIM_METRICAS_MEMORIA);
        C(sim_ejecucion_ejecutar(s,10)==SIM_EJECUCION_PENDIENTE);
        C(sim_ejecucion_avanzar(s)==SIM_EJECUCION_PENDIENTE);
        C(sim_ejecucion_reiniciar(s,true)==SIM_EJECUCION_PENDIENTE);
        C(sim_ejecucion_resultados(s,&r)==SIM_METRICAS_SECUENCIA);
        C(sim_ejecucion_exportar(s,1,&j)==SIM_METRICAS_SECUENCIA && !j);
        C(sim_ejecucion_trap(s,0,&h)==SIM_METRICAS_SECUENCIA);
        /* Una segunda falta de memoria conserva el bloqueo. Al retirarla,
         * el reloj sigue en el ciclo del trap y el historial aumenta una vez;
         * solo la llamada posterior de ejecutar permite otro flanco. */
        llamadas_realloc=0; C(sim_ejecucion_reintentar(s)==SIM_EJECUCION_PENDIENTE);
        fallar_realloc=0; C(sim_ejecucion_reintentar(s)==0);
        r=resultado(s); C(r.ciclos==ciclo && r.traps==objetivo);
        C(sim_ejecucion_reintentar(s)==0 && resultado(s).traps==objetivo);
        C(sim_ejecucion_ejecutar(s,1)==0 && resultado(s).ciclos==ciclo+1);
        sim_ejecucion_liberar(&s);
    }
    /* Una reserva fallida al exportar no deja un flanco pendiente: se exige
     * salida NULL, se vuelve a pedir el reporte y después la CPU puede avanzar.
     * No debe confundirse este error de reporte con el del historial de traps. */
    s=crear(nop,false,false); llamadas_realloc=0; fallar_realloc=1;
    C(sim_ejecucion_exportar(s,1,&j)==SIM_METRICAS_MEMORIA && !j);
    fallar_realloc=0; C(sim_ejecucion_exportar(s,1,&j)==0);
    sim_metricas_liberar_reporte(&j); C(sim_ejecucion_avanzar(s)==0);
    sim_ejecucion_liberar(&s);
#endif
    printf("M2: %zu comprobaciones PASS\n",checks);
}

/* Harness privado de fixtures: run coteja cada flanco M2 con S6 directo y
 * su reporte con M1 sobre la referencia. chunk usa presupuestos sucesivos;
 * Python compara luego sus salidas completas con run. No es la herramienta
 * pública: solo acepta el protocolo controlado de estas pruebas. */
static int archivo(int argc,char **argv)
{
    C(argc==9); bool balb=!strcmp(argv[2],"balb"), fisica=!strcmp(argv[4],"fisica");
    SimEjecucion *s=crear(argv[3],balb,fisica);
    SimImagen *i=NULL; C((balb?sim_cargar_balb:sim_cargar_mem)(argv[3],&i,stderr)==0);
    SimPipeline ref; C(sim_pipeline_iniciar(&ref,i)==0);
    C(sim_pipeline_configurar_busqueda(&ref,fisica?SIM_BUSQUEDA_FISICA:SIM_BUSQUEDA_FINITA)==0);
    SimMetricas m={0}; C(sim_metricas_iniciar(&m,&ref)==0);
    uint64_t presupuesto=strtoull(argv[5],NULL,10), fragmento=strtoull(argv[6],NULL,10);
    const char *lista=argv[6];
    SimEjecucionCodigo codigo=SIM_EJECUCION_OK;
    if (!strcmp(argv[1],"run")) {
        iguales(s,&ref);
        for (uint64_t n=0;n<presupuesto;++n) {
            /* El reloj previo decide si M1 vio un flanco o solo un intento
             * rechazado. Se compara antes de que cualquiera avance otra vez. */
            uint64_t antes=ref.ciclos; SimPipelineEstado estado=sim_avanzar_ciclo(&ref);
            codigo=sim_ejecucion_avanzar(s);
            C(codigo==(estado>SIM_PIPE_LIMITE?SIM_EJECUCION_PIPELINE:SIM_EJECUCION_OK));
            C(sim_metricas_observar(&m,&ref)==(antes==ref.ciclos?SIM_METRICAS_SIN_CICLO:SIM_METRICAS_OK));
            iguales(s,&ref);
            if (antes!=ref.ciclos) ++comparaciones_flancos;
            if (estado==SIM_PIPE_TERMINADO || estado>SIM_PIPE_LIMITE) break;
        }
        /* Pausar ambos no ejecuta instrucciones. Un error previo debe seguir
         * siendo error; la terminación también conserva su último snapshot. */
        C(sim_ejecucion_ejecutar(s,0)==codigo);
        (void)sim_pipeline_ejecutar(&ref,0); iguales(s,&ref);
        /* Rechazos terminales/administrativos conservan ciclos y estado. */
        if (ref.estado==SIM_PIPE_TERMINADO || ref.estado>SIM_PIPE_LIMITE) {
            C(sim_ejecucion_avanzar(s)==codigo); iguales(s,&ref);
        }
    } else {
        C(fragmento>0);
        for (uint64_t n=0;n<presupuesto;) {
            /* La lista prueba 10+20+30+40; un único número se reutiliza para
             * 100×1. El último tramo se recorta al presupuesto restante. */
            char *fin; fragmento=strtoull(lista,&fin,10); C(fragmento>0);
            if (*fin==',') lista=fin+1;
            uint64_t cantidad=presupuesto-n<fragmento?presupuesto-n:fragmento;
            codigo=sim_ejecucion_ejecutar(s,cantidad); n+=cantidad;
            if (codigo!=0) break;
        }
        if (!presupuesto) C(sim_ejecucion_ejecutar(s,0)==0);
    }
    char *out=NULL,*directo=NULL;
    SimMetricasFormato f=!strcmp(argv[7],"json")?SIM_METRICAS_JSON:SIM_METRICAS_TEXTO;
    C(sim_ejecucion_exportar(s,f,&out)==0);
    if (!strcmp(argv[1],"run")) {
        /* Igualdad textual exige que M2 reutilice M1 sin añadir campos ni
         * alterar formato, historial o estado administrativo del reporte. */
        C(sim_metricas_exportar(&m,f,&directo)==0 && !strcmp(out,directo));
        sim_metricas_liberar_reporte(&directo);
    }
    C(fputs(out,stdout)>=0); sim_metricas_liberar_reporte(&out);
    uint8_t mem[65536]; C(sim_ejecucion_memoria(s,mem,sizeof mem)==0);
    FILE *fmem=fopen(argv[8],"wb"); C(fmem && fwrite(mem,1,sizeof mem,fmem)==sizeof mem && fclose(fmem)==0);
    sim_metricas_liberar(&m); sim_liberar_imagen(&i); sim_ejecucion_liberar(&s);
    fprintf(stderr,"M2 diferencial: %zu flancos PASS\n",comparaciones_flancos);
    return fflush(stdout)!=0;
}
int main(int argc,char **argv)
{
    if (argc==5 && !strcmp(argv[1],"unit")) { unitarias(argv[2],argv[3],argv[4]); return 0; }
    return archivo(argc,argv);
}
