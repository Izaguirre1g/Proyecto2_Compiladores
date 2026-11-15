/* Contratos B1 y fallos reales de reservas; no inspecciona sesiones privadas. */
#define _POSIX_C_SOURCE 200809L
#include "sim_comparador.h"
#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
static size_t checks,casos;
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL B1 %d: %s\n",__LINE__,#x); exit(1); } } while (0)
#ifdef SIM_COMPARADOR_FALLOS
/* Las envolturas solo pertenecen al test. La llamada elegida falla sin que
 * realloc pierda el puntero anterior; persistente fuerza todos los intentos. */
static bool activo,persistente,fallar_locale;
static unsigned tipo;
static size_t llamada,objetivo,total[3],generaciones,fallar_generacion;
/* Rutina real malloc: destino de las llamadas no inyectadas. */ void *__real_malloc(size_t);
/* Rutina real calloc: destino de las llamadas no inyectadas. */ void *__real_calloc(size_t,size_t);
/* Rutina real realloc: destino de las llamadas no inyectadas. */ void *__real_realloc(void *,size_t);
/* Cuenta la reserva objetivo y decide si corresponde inyectar el fallo configurado; las otras llamadas conservan la rutina real. */ static bool fallar(unsigned t)
{
    if (!activo) return false;
    ++total[t];
    return tipo==t && ++llamada>=objetivo && (persistente || llamada==objetivo);
}
/* Envuelve malloc: inyecta fallos y delega el resto. */ void *__wrap_malloc(size_t n) { return fallar(0)?NULL:__real_malloc(n); }
/* Envuelve calloc: inyecta fallos y delega el resto. */ void *__wrap_calloc(size_t n,size_t z) { return fallar(1)?NULL:__real_calloc(n,z); }
/* Envuelve realloc: inyecta fallos y delega el resto. */ void *__wrap_realloc(void *p,size_t n) { return fallar(2)?NULL:__real_realloc(p,n); }
/* Rutina real vsnprintf: destino de las llamadas no inyectadas. */ int __real_vsnprintf(char *,size_t,const char *,va_list);
/* Envuelve vsnprintf: inyecta fallos y delega el resto. */ int __wrap_vsnprintf(char *s,size_t n,const char *f,va_list a)
{
    if (fallar_generacion && ++generaciones==fallar_generacion) return -1;
    return __real_vsnprintf(s,n,f,a);
}
#ifdef __GLIBC__
/* Rutina real __vsnprintf_chk: destino de las llamadas no inyectadas. */ int __real___vsnprintf_chk(char *,size_t,int,size_t,const char *,va_list);
/* Envuelve __vsnprintf_chk: inyecta fallos y delega el resto. */ int __wrap___vsnprintf_chk(char *s,size_t n,int flag,size_t z,const char *f,va_list a)
{
    if (fallar_generacion && ++generaciones==fallar_generacion) return -1;
    return __real___vsnprintf_chk(s,n,flag,z,f,a);
}
#endif
/* Rutina real newlocale: destino de las llamadas no inyectadas. */ locale_t __real_newlocale(int,const char *,locale_t);
/* Envuelve newlocale: inyecta fallos y delega el resto. */ locale_t __wrap_newlocale(int m,const char *s,locale_t l)
{
    return fallar_locale?(locale_t)0:__real_newlocale(m,s,l);
}
/* Activa el punto de fallo inyectado y reinicia sus contadores para que cada caso pruebe una reserva concreta. */ static void activar(unsigned t,size_t n,bool siempre)
{
    activo=true;tipo=t;objetivo=n;llamada=0;persistente=siempre;
    memset(total,0,sizeof total);
}
/* Deshabilita la inyección para que la comprobación y la limpieza usen las rutinas reales. */ static void desactivar(void) { activo=false;persistente=false; }
#endif
/* Obtiene el resumen público y exige el estado de comparación esperado mediante las aserciones del banco. */ static SimComparadorResumen resumen(SimComparador *c)
{
    SimComparadorResumen r;C(sim_comparador_resumen(c,&r)==0);return r;
}
/* Construye casos básicos de comparación y verifica resultados, argumentos y propiedad de recursos. */ static void basicas(const char *nop,const char *trap)
{
    ++casos; SimComparadorOpciones o={{nop,0},{nop,0},SIM_BUSQUEDA_FINITA,100,2};
    SimComparador *c=NULL;SimComparadorResumen r;
    C(sim_comparador_ejecutar(NULL,&c,NULL)==SIM_COMPARADOR_ARGUMENTO && !c);
    C(sim_comparador_ejecutar(&o,NULL,NULL)==SIM_COMPARADOR_ARGUMENTO);
    SimComparadorOpciones malo=o;malo.referencia.ruta=NULL;
    C(sim_comparador_ejecutar(&malo,&c,NULL)==SIM_COMPARADOR_ARGUMENTO);
    malo=o;malo.candidato.ruta="";C(sim_comparador_ejecutar(&malo,&c,NULL)==SIM_COMPARADOR_ARGUMENTO);
    malo=o;malo.candidato.formato=99;C(sim_comparador_ejecutar(&malo,&c,NULL)==SIM_COMPARADOR_ARGUMENTO);
    malo=o;malo.modo=99;C(sim_comparador_ejecutar(&malo,&c,NULL)==SIM_COMPARADOR_ARGUMENTO);
    malo=o;malo.reintentos=17;C(sim_comparador_ejecutar(&malo,&c,NULL)==SIM_COMPARADOR_ARGUMENTO);
    C(sim_comparador_resumen(NULL,&r)==SIM_COMPARADOR_ARGUMENTO);
    C(sim_comparador_trap(NULL,0,0,NULL)==SIM_COMPARADOR_ARGUMENTO);
    C(sim_comparador_exportar(NULL,0,NULL)==SIM_COMPARADOR_ARGUMENTO);
    /* Dos NOP idénticos deben terminar: cuatro ciclos y delta de ciclos cero. */ C(sim_comparador_ejecutar(&o,&c,NULL)==0);SimComparador *guardado=c;
    C(sim_comparador_ejecutar(&o,&c,NULL)==SIM_COMPARADOR_ARGUMENTO && guardado==c);
    C(sim_comparador_resumen(c,NULL)==SIM_COMPARADOR_ARGUMENTO);
    r=resumen(c);C(r.resultado==SIM_COMPARADOR_EQUIVALENTE && r.iniciales_compatibles && r.comparacion_realizada);
    C(r.resumenes[0].ciclos==4 && r.metricas.ciclos.magnitud==0 && r.metricas.diferencia_ipc==0);
    C(sim_comparador_trap(c,0,0,NULL)==SIM_COMPARADOR_ARGUMENTO);
    char *j=NULL;C(sim_comparador_exportar(c,99,&j)==SIM_COMPARADOR_ARGUMENTO && !j);
    C(sim_comparador_exportar(c,1,NULL)==SIM_COMPARADOR_ARGUMENTO);
    C(sim_comparador_exportar(c,1,&j)==0);char *antes=j;
    C(sim_comparador_exportar(c,0,&j)==SIM_COMPARADOR_ARGUMENTO && j==antes);
    /* La exportación no debe cambiar ni la clasificación ni los snapshots.
     * Los textos son propios y sobreviven a la destrucción del comparador. */
    SimComparadorResumen r2=resumen(c);C(r.resultado==r2.resultado && r.resumenes[0].ciclos==r2.resumenes[0].ciclos && !memcmp(r.resumenes[1].registros,r2.resumenes[1].registros,sizeof r.resumenes[1].registros));
    ++casos; char *otro=NULL;C(setlocale(LC_NUMERIC,"es_CR.utf8")!=NULL);
    C(sim_comparador_exportar(c,1,&otro)==0 && !strcmp(j,otro));
    C(!strcmp(localeconv()->decimal_point,","));C(setlocale(LC_NUMERIC,"C")!=NULL);
    sim_comparador_liberar_reporte(&otro);sim_comparador_liberar(&c);
    C(strstr(j,"EQUIVALENTE") && !strstr(j,"password") && !strstr(j,"boveda.keys"));
    sim_comparador_liberar_reporte(&j);sim_comparador_liberar_reporte(&j);sim_comparador_liberar_reporte(NULL);
    sim_comparador_liberar(&c);sim_comparador_liberar(NULL);
    /* Las rutas del cliente pueden desaparecer: el resultado conserva sus
     * propios nombres, igual que sus snapshots sobreviven a las sesiones M2. */
    ++casos;char *ruta=malloc(strlen(nop)+1);C(ruta!=NULL);strcpy(ruta,nop);
    o.referencia.ruta=ruta;C(sim_comparador_ejecutar(&o,&c,NULL)==0);
    memset(ruta,'x',strlen(ruta));free(ruta);o.referencia.ruta=nop;
    C(sim_comparador_exportar(c,1,&j)==0 && strstr(j,nop));
    sim_comparador_liberar_reporte(&j);sim_comparador_liberar(&c);
    /* Sin flancos no hay comparación final; el resultado debe ser INCONCLUSO. */ ++casos;o.presupuesto=0;C(sim_comparador_ejecutar(&o,&c,NULL)==0);r=resumen(c);
    C(r.resultado==SIM_COMPARADOR_INCONCLUSO && !r.comparacion_realizada && r.metricas_disponibles);
    C(!r.metricas.porcentaje_definido && r.metricas.cambio_porcentual_ciclos==0);
    C(r.resumenes[0].ipc==0 && r.resumenes[0].estado==SIM_PIPE_LIMITE);
    sim_comparador_liberar(&c);
    ++casos;o.presupuesto=UINT64_MAX;C(sim_comparador_ejecutar(&o,&c,NULL)==0 && resumen(c).resultado==SIM_COMPARADOR_EQUIVALENTE);
    sim_comparador_liberar(&c);
    /* Fallar la carga debe publicar ERROR, nunca equivalencia vacía. */ ++casos;o.referencia.ruta="/no/existe/b1";
    C(sim_comparador_ejecutar(&o,&c,NULL)==0);r=resumen(c);
    C(r.resultado==SIM_COMPARADOR_ERROR && r.lado_error==0 && !r.comparacion_realizada);
    C(sim_comparador_exportar(c,1,&j)==0 && strstr(j,"ERROR"));
    sim_comparador_liberar_reporte(&j);sim_comparador_liberar(&c);
    /* El modo físico repite traps y agota 20 ciclos, sin demostrar fin. */ ++casos;o.referencia.ruta=trap;o.candidato.ruta=trap;o.modo=SIM_BUSQUEDA_FISICA;o.presupuesto=20;
    C(sim_comparador_ejecutar(&o,&c,NULL)==0);r=resumen(c);
    C(r.resultado==SIM_COMPARADOR_INCONCLUSO && r.resumenes[0].traps==6 && r.resumenes[1].ciclos==20);
    SimMetricaTrap t;C(sim_comparador_trap(c,0,0,&t)==0 && t.ciclo==3 && t.vector==SIM_TRAP_VECTOR);
    C(sim_comparador_trap(c,2,0,&t)==SIM_COMPARADOR_ARGUMENTO);
    C(sim_comparador_trap(c,0,6,&t)==SIM_COMPARADOR_ARGUMENTO);
    sim_comparador_liberar(&c);
#ifdef SIM_COMPARADOR_FALLOS
    /* Se recorren todas las reservas observadas en una ejecución válida.
     * Un fallo B1 no publica objeto; un fallo M2 produce ERROR, salvo una
     * observación MEMORIA que puede recuperarse con el reintento permitido. */
    ++casos;activar(3,1,false);C(sim_comparador_ejecutar(&o,&c,NULL)==0);desactivar();
    size_t cantidades[3];memcpy(cantidades,total,sizeof cantidades);sim_comparador_liberar(&c);
    for (unsigned tipo_=0;tipo_<3;++tipo_) for (size_t n=1;n<=cantidades[tipo_];++n) {
        activar(tipo_,n,false);SimComparadorCodigo e=sim_comparador_ejecutar(&o,&c,NULL);desactivar();
        C(e==SIM_COMPARADOR_MEMORIA || e==SIM_COMPARADOR_OK);
        if (e==SIM_COMPARADOR_MEMORIA) C(!c);
        else {
            r=resumen(c);C(r.resultado==SIM_COMPARADOR_ERROR || r.resultado==SIM_COMPARADOR_INCONCLUSO);
            if (r.resultado==SIM_COMPARADOR_INCONCLUSO) C(r.resumenes[0].ciclos==20 && r.resumenes[1].ciclos==20 && r.resumenes[0].traps==6);
        }
        sim_comparador_liberar(&c);sim_comparador_liberar(&c);
    }
    /* Fallo persistente: un intento original y dos reintentos, nunca otro
     * flanco. Con cero reintentos se conserva igualmente el error pendiente. */
    ++casos;
    for (unsigned maximo=0;maximo<=2;maximo+=2) {
        o.reintentos=maximo;activar(2,1,true);C(sim_comparador_ejecutar(&o,&c,NULL)==0);desactivar();
        r=resumen(c);C(r.resultado==SIM_COMPARADOR_ERROR && r.motivo==SIM_COMPARADOR_OBSERVACION);
        C(total[2]==maximo+1 && !r.resumen_disponible[0] && !r.ejecutadas[1]);
        C(r.estados[0].pendiente && r.estados[0].metricas==SIM_METRICAS_MEMORIA);
        sim_comparador_liberar(&c);
    }
    ++casos;o.reintentos=2;C(sim_comparador_ejecutar(&o,&c,NULL)==0);
    activar(3,1,false);C(sim_comparador_exportar(c,1,&j)==0);desactivar();
    size_t buffers=total[2];sim_comparador_liberar_reporte(&j);
    for (size_t n=1;n<=buffers;++n) {
        activar(2,n,false);C(sim_comparador_exportar(c,1,&j)==SIM_COMPARADOR_MEMORIA);desactivar();C(!j);
    }
    /* Ambas fases de vsnprintf pueden fallar. Tampoco se publica una salida
     * parcial si falla crear el locale numérico temporal. */
    ++casos;
    for (size_t n=1;n<=4;++n) {
        generaciones=0;fallar_generacion=n;
        C(sim_comparador_exportar(c,1,&j)==SIM_COMPARADOR_GENERACION && !j);fallar_generacion=0;
    }
    fallar_locale=true;C(sim_comparador_exportar(c,1,&j)==SIM_COMPARADOR_GENERACION && !j);fallar_locale=false;
    C(sim_comparador_exportar(c,1,&j)==0);sim_comparador_liberar_reporte(&j);sim_comparador_liberar(&c);
#endif
    printf("B1 C: %zu escenarios, %zu comprobaciones PASS\n",casos,checks);
}
/* Selecciona el escenario autoverificable o el modo de sonda solicitado; sus códigos y salidas son comprobados por la suite Python. */ int main(int argc,char **argv)
{
    C(argc==3);basicas(argv[1],argv[2]);return 0;
}
