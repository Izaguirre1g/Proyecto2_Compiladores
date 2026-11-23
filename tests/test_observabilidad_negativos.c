/* Regresiones de contratos O1: inválidos, duplicados, modo histórico,
 * arreglos completos y ámbitos hermanos. Usa productores y simulador reales. */
#include "ide_optimizacion.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned errores,comprobaciones;
#define CHECK(x) do {++comprobaciones;if(!(x)){fprintf(stderr,"FAIL L%d %s\n",__LINE__,#x);++errores;}}while(0)
static const char *programa=
"create_funk #declare_int# otra() {\n"
"    privado * declare_int : 7;\n    r * declare_int : 99;\n    give 0;\n}\n"
"create_funk #declare_infinite_void# main() {\n"
"    muerto * declare_int : 4;\n"
"    salida * declare_int : 9;\n}\n";
static void dce_negativos(void){
    IdeDce d={0};IdeObservable o={"ausente",0,0};
    CHECK(ide_dce_analizar_con_observables(programa,strlen(programa),"tests/h19.bal",&o,1,&d)==0);
    CHECK(d.estado==IDE_DCE_SIN_IR && d.motivo);ide_dce_liberar(&d);
    o.nombre="privado";
    CHECK(ide_dce_analizar_con_observables(programa,strlen(programa),"tests/h19.bal",&o,1,&d)==0);
    CHECK(d.estado==IDE_DCE_SIN_IR);ide_dce_liberar(&d);
    IdeObservable obs[2]={{"salida",0,0},{"salida",0,0}};IdeOptimizacion r={0};
    CHECK(ide_optimizacion_analizar_con_observables(programa,strlen(programa),"tests/h19.bal",obs,2,&r)==0);
    CHECK(r.mismo_resultado==1 && r.o1.compilado);
    CHECK(r.o1_codigo && !strstr(r.o1_codigo,"muerto *") && strstr(r.o1_codigo,"salida *"));
    CHECK(r.o1.cantidad_variables==1 && r.o1.valores && !strcmp(r.o1.valores[0],"9"));
    ide_optimizacion_liberar(&r);
    IdeDce a={0},b={0};
    CHECK(ide_dce_analizar(programa,strlen(programa),"tests/h19.bal",&a)==0);
    CHECK(ide_dce_analizar_con_observables(programa,strlen(programa),"tests/h19.bal",NULL,0,&b)==0);
    CHECK(a.estado==b.estado && a.codigo_nuevo && b.codigo_nuevo && !strcmp(a.codigo_nuevo,b.codigo_nuevo));
    CHECK(a.resultado.pasadas==b.resultado.pasadas && a.resultado.pasadas>0 && a.vivas && a.vivas[0]);
    ide_dce_liberar(&a);ide_dce_liberar(&b);
    CHECK(ide_dce_analizar_con_observables(NULL,0,NULL,NULL,0,&d)==-1);
    CHECK(ide_dce_analizar_con_observables(programa,strlen(programa),NULL,NULL,1,&d)==-1);
    CHECK(ide_dce_analizar_con_observables(programa,strlen(programa),NULL,obs,65,&d)==-1);
    o=(IdeObservable){"salida",8,0};
    CHECK(ide_dce_analizar_con_observables(programa,strlen(programa),NULL,&o,1,&d)==-1);
    CHECK(ide_dce_analizar_con_observables("",0,"tests/h19.bal",NULL,0,&d)==0);
    CHECK(d.estado!=IDE_DCE_LISTO);ide_dce_liberar(&d);
}
/* El contrato solo expone locales de main: un global no se convierte en
 * raíz por coincidencia textual. Un local de otra función tampoco satisface
 * la petición (cubierto arriba). No se altera la semántica global de BAL. */
static void global_no_expuesto(void)
{
    const char *t="declare_const global * declare_int : 7;\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    salida * declare_int : 9;\n}\n";
    /* Primero acreditamos que el fixture global es un programa BAL válido:
     * su rechazo posterior corresponde al contrato de raíces, no al parser. */
    IdeEnsamblador a={0};
    CHECK(ide_ensamblador_generar(t,strlen(t),"tests/obs_global.bal",&a)==0);
    CHECK(a.estado==IDE_ASM_LISTO);
    ide_ensamblador_liberar(&a);
    IdeObservable o={"global",0,0};IdeDce d={0};
    CHECK(ide_dce_analizar_con_observables(t,strlen(t),"tests/obs_global.bal",&o,1,&d)==0);
    CHECK(d.estado==IDE_DCE_SIN_IR && d.motivo);
    ide_dce_liberar(&d);
}

static void comparador_grande(void){
    uint32_t *x=calloc(4096,sizeof *x),*y=calloc(4096,sizeof *y);CHECK(x && y);if(!x||!y){free(x);free(y);return;}
    unsigned char *dx[]={(unsigned char *)x},*dy[]={(unsigned char *)y};size_t lx[]={16384},ly[]={16384};
    IdeVarMain vx={.nombre="objeto",.linea_declaracion=2,.columna_declaracion=5,.elementos=4096,.bytes_elemento=4},vy=vx;
    IdeOptNivel a={.compilado=1,.variables=&vx,.cantidad_variables=1,.datos=dx,.largos_datos=lx},b=a;b.variables=&vy;b.datos=dy;b.largos_datos=ly;
    a.simulacion.estado=b.simulacion.estado=MET_LISTO;IdeObservable o={"objeto",2,5};
    CHECK(ide_optimizacion_comparar(&a,&b,&o,1)==1);
    y[4000]=3;CHECK(ide_optimizacion_comparar(&a,&b,&o,1)==0);y[4000]=0;
    vy.elementos=2048;ly[0]=8192;CHECK(ide_optimizacion_comparar(&a,&b,&o,1)==0);
    vy=vx;ly[0]=16384;vy.elementos=0;ly[0]=0;CHECK(ide_optimizacion_comparar(&a,&b,&o,1)==-1);
    vy=vx;ly[0]=16384;vy.elementos=SIZE_MAX;CHECK(ide_optimizacion_comparar(&a,&b,&o,1)==-1);
    vy=vx;CHECK(ide_optimizacion_comparar(NULL,&b,&o,1)==-1);CHECK(ide_optimizacion_comparar(&a,&b,NULL,0)==-1);
    for(unsigned k=0;k<64;++k){size_t j=16+k*17;y[j]=k+1;CHECK(ide_optimizacion_comparar(&a,&b,&o,1)==0);y[j]=0;CHECK(ide_optimizacion_comparar(&a,&b,&o,1)==1);}
    free(x);free(y);
}

static void bloques_hermanos(void){
    const char *t="create_funk #declare_infinite_void# main() {\n"
        "    whether (declare_true) {\n        r * declare_int : 1;\n    }\n"
        "    whether (declare_true) {\n        r * declare_int : 2;\n    }\n}\n";
    IdeObservable o={"r",6,9};IdeOptimizacion r={0};
    CHECK(ide_optimizacion_analizar_con_observables(t,strlen(t),"tests/h19_hermanos.bal",&o,1,&r)==0);
    CHECK(r.mismo_resultado==1 && r.o1.cantidad_variables==1);
    CHECK(r.o1.valores && !strcmp(r.o1.valores[0],"2"));
    CHECK(r.o1.variables && r.o1.variables[0].linea_declaracion==6);
    ide_optimizacion_liberar(&r);
    IdeDce d={0};o.linea=o.columna=0;
    CHECK(ide_dce_analizar_con_observables(t,strlen(t),"tests/h19_hermanos.bal",&o,1,&d)==0);
    CHECK(d.estado==IDE_DCE_SIN_IR);ide_dce_liberar(&d);
}
int main(void){dce_negativos();global_no_expuesto();comparador_grande();bloques_hermanos();printf("H19 negativos: %u comprobaciones; %u errores\n",comprobaciones,errores);return errores?1:0;}
