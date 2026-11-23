/* Reproduce las salidas externas que la política histórica no conserva.
 * Usa el productor y el simulador del IDE reales; imprime mediciones por nivel.
 * Los fixtures pertenecen al repositorio; las salidas de prueba son regenerables. */
#include "ide_optimizacion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fallos;
#define COMPROBAR(x) do {if(!(x)){fprintf(stderr,"FALLO %d: %s\n",__LINE__,#x);++fallos;}}while(0)
/* Carga un fixture real y contrasta objetos requeridos por su contrato externo.
 * La salida se libera con el liberador público incluso al faltar un objeto. */
static void caso(const char *ruta, const char *const *nombres, const char *const *esperados, size_t cantidad)
{
    FILE *f=fopen(ruta,"rb"); char texto[8192];
    if (!f) { ++fallos; return; }
    size_t n=fread(texto,1,sizeof texto-1,f); fclose(f);texto[n]=0;
    IdeOptimizacion r;
    IdeObservable obs[8];
    for(size_t i=0;i<cantidad;++i)obs[i]=(IdeObservable){nombres[i],0,0};
    if (ide_optimizacion_analizar_con_observables(texto,n,ruta,obs,cantidad,&r)) { ++fallos;return; }
    const IdeOptNivel *niveles[]={&r.o0,&r.o1,&r.o2};
    for (size_t k=0;k<3;++k) {
        const IdeOptNivel *v=niveles[k];
        printf("%s O%zu instr=%zu bundles=%zu bytes=%zu ciclos=%llu ipc=%.9f marco=%zu variables=",ruta,k,v->instrucciones,v->bundles,v->bundles*16,(unsigned long long)v->simulacion.sim.ciclos,v->simulacion.sim.ipc,v->marco_main);
        for (size_t j=0;j<v->cantidad_variables;++j)printf("%s=%s;",v->variables[j].nombre,v->valores?v->valores[j]:"?");
        puts("");
    }
    for (size_t i=0;i<cantidad;++i) {
        int existe=0;
        for(size_t j=0;j<r.o1.cantidad_variables;++j)if(!strcmp(nombres[i],r.o1.variables[j].nombre)) {
            existe=1;COMPROBAR(r.o1.valores && !strcmp(r.o1.valores[j],esperados[i]));
        }
        if(!existe){fprintf(stderr,"FALLO esperado: O1 perdió %s\n",nombres[i]);++fallos;}
    }
    COMPROBAR(r.mismo_o1==1 && r.mismo_o2==1 && r.mismo_resultado==1);
    /* Exporta el ensamblador real de la API para contrastarlo byte a byte
     * con CLI; los artefactos quedan exclusivamente en este worktree. */
    const char *base=strrchr(ruta,'/');char salida[256];
    snprintf(salida,sizeof salida,"evidencias/observabilidad/%s.ide-o1.asm",base?base+1:ruta);
    FILE *asmf=fopen(salida,"wb");COMPROBAR(asmf!=NULL);
    if(asmf){COMPROBAR(fputs(r.o1.ensamblador,asmf)>=0);fclose(asmf);}
    ide_optimizacion_liberar(&r);
}
/* Un resultado interno eliminado no hace incorrecto el programa. Si no
 * existe un contrato más pequeño, el modo histórico devuelve NO VERIFICABLE.
 * También registra el ahorro histórico sin exigir que coincida con O2. */
static void historico(void)
{
    const char *ruta="examples/dce/dce_ensamblador.bal";
    FILE *f=fopen(ruta,"rb");char texto[8192];COMPROBAR(f!=NULL);if(!f)return;
    size_t n=fread(texto,1,sizeof texto-1,f);fclose(f);texto[n]=0;
    IdeOptimizacion r;COMPROBAR(ide_optimizacion_analizar(texto,n,ruta,&r)==0);
    COMPROBAR(r.o0.bundles==528 && r.o1.bundles==435);
    COMPROBAR(r.o0.simulacion.sim.ciclos==6194 && r.o1.simulacion.sim.ciclos==2177);
    COMPROBAR(r.mismo_o1==-1);
    IdeObservable o={"resultados",0,0};
    COMPROBAR(ide_optimizacion_comparar(&r.o0,&r.o1,&o,1)==1);
    printf("historico: %zu/%zu bundles; %llu/%llu ciclos\n",r.o0.bundles,r.o1.bundles,
        (unsigned long long)r.o0.simulacion.sim.ciclos,(unsigned long long)r.o1.simulacion.sim.ciclos);
    ide_optimizacion_liberar(&r);
}
/* Dos r de main y otro r de una función. Solo el declarador (8,9) es salida.
 * La eliminación del primer homónimo no cambia la identidad del segundo. */
static void homonimos(void)
{
    const char *texto="create_funk #declare_int# f() {\n"
        "    r * declare_int : 99;\n    give 0;\n}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    r * declare_int : 1;\n    whether (declare_true) {\n"
        "        r * declare_int : 2;\n    }\n}\n";
    IdeObservable o={"r",8,9};IdeOptimizacion r;
    COMPROBAR(ide_optimizacion_analizar_con_observables(texto,strlen(texto),"tests/obs_editor.bal",&o,1,&r)==0);
    COMPROBAR(r.o1.compilado && r.mismo_o1==1 && r.mismo_o2==1);
    COMPROBAR(r.o1.cantidad_variables==1);
    if(r.o1.cantidad_variables) {
        COMPROBAR(r.o1.variables[0].linea_declaracion==8);
        COMPROBAR(r.o1.valores && !strcmp(r.o1.valores[0],"2"));
    }
    COMPROBAR(r.o1_codigo && !strstr(r.o1_codigo,": 99;") && !strstr(r.o1_codigo,": 1;"));
    ide_optimizacion_liberar(&r);
    /* Un nombre sin coordenadas es ambiguo y debe rechazarse expresamente. */
    IdeDce d; o.linea=o.columna=0;
    COMPROBAR(ide_dce_analizar_con_observables(texto,strlen(texto),"tests/obs_editor.bal",&o,1,&d)==0);
    COMPROBAR(d.estado==IDE_DCE_SIN_IR && d.motivo && strstr(d.motivo,"ambiguo"));
    ide_dce_liberar(&d);
    o.nombre="ausente";
    COMPROBAR(ide_dce_analizar_con_observables(texto,strlen(texto),"tests/obs_editor.bal",&o,1,&d)==0);
    COMPROBAR(d.estado==IDE_DCE_SIN_IR);ide_dce_liberar(&d);
}
/* Un arreglo real de 18 enteros fuerza la lectura completa del simulador.
 * Alterar el último byte del objeto publicado debe cambiar el veredicto,
 * aunque la cadena visual siga truncada exactamente igual. */
static void arreglo_completo(void)
{
    const char *t="create_funk #declare_infinite_void# main() {\n"
        "    salida * declare_int[18] : [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17];\n}\n";
    IdeObservable o={"salida",0,0};IdeOptimizacion r;
    COMPROBAR(ide_optimizacion_analizar_con_observables(t,strlen(t),"tests/obs_arreglo.bal",&o,1,&r)==0);
    COMPROBAR(r.mismo_resultado==1 && r.o1.cantidad_variables==1);
    if(r.o1.datos && r.o1.largos_datos && r.o1.largos_datos[0]==72) {
        COMPROBAR(r.o1.datos[0][68]==17);
        r.o1.datos[0][68]^=1;
        COMPROBAR(ide_optimizacion_comparar(&r.o0,&r.o1,&o,1)==0);
    } else COMPROBAR(0);
    ide_optimizacion_liberar(&r);
}
int main(void)
{
    const char *m[]={"fila_encontrada","columna_encontrada"},*mv[]={"0","1"};
    const char *p[]={"resultado","vacio","combinado","total"},*pv[]={"24","0","13","37"};
    caso("examples/ejercicio2_busqueda_matriz.bal",m,mv,2);
    caso("examples/ejercicio3_puntajes.bal",p,pv,4);
    historico();homonimos();arreglo_completo();
    /* registrar escribe mediante un parámetro: conservar llamadas con
     * retorno descartado es necesario para obtener [7, 4, 0]. */
    const char *e[]={"datos"},*ev[]={"[7, 4, 0]"};
    caso("examples/dce/dce_llamadas.bal",e,ev,1);
    printf("observabilidad: %s\n",fallos?"FAIL":"PASS");return fallos?1:0;
}
