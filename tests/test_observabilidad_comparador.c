/* Oráculos independientes de comparación: igualdad de objetos completos,
 * diferencia, ausencia, información insuficiente y homónimos inequívocos.
 * OBS_BASELINE_SOURCE permite ejecutar los mismos contraejemplos contra el
 * comparador histórico sin alterar ni recompilar archivos del repo original. */
#include "ide_optimizacion.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef OBS_BASELINE_SOURCE
#include OBS_BASELINE_SOURCE
#endif
static int fallos;
#define VER(x) do {if(!(x)){fprintf(stderr,"FALLO %d: %s\n",__LINE__,#x);++fallos;}}while(0)
/* Los arreglos de prueba pertenecen a la pila: no se pasan al liberador de
 * resultados productivos. El modo histórico ignora el contrato y los bytes. */
static int comparar(const IdeOptNivel *a,const IdeOptNivel *b,const IdeObservable *o,size_t n)
{
#ifdef OBS_BASELINE_SOURCE
    (void)o;(void)n;return comparar_resultados(a,b);
#else
    return ide_optimizacion_comparar(a,b,o,n);
#endif
}
int main(void)
{
    uint32_t x[18]={0},y[18]={0};unsigned char *dx[]={(unsigned char *)x},*dy[]={(unsigned char *)y};
    size_t largo[]={sizeof x};char *resumen[]={"[0, 0, ...]"};
    IdeVarMain v={.nombre="salida",.linea_declaracion=4,.columna_declaracion=5,.elementos=18,.bytes_elemento=4};
    IdeOptNivel a={.compilado=1,.variables=&v,.cantidad_variables=1,.datos=dx,.largos_datos=largo,.valores=resumen};
    a.simulacion.estado=MET_LISTO;IdeOptNivel b=a;b.datos=dy;
    IdeObservable o={"salida",4,5};
    /* Los bytes coinciden aunque los resúmenes gráficos no sean un oráculo. */
    VER(comparar(&a,&b,&o,1)==1);
    /* La diferencia posterior al elemento 16 debe detectarse, con el mismo
     * texto truncado que antes escondía esta divergencia. */
    y[17]=9;VER(comparar(&a,&b,&o,1)==0);y[17]=0;
    /* Falta una salida obligatoria: NO VERIFICABLE, no igualdad vacía. */
    b.cantidad_variables=0;b.valores=NULL;VER(comparar(&a,&b,&o,1)==-1);
    b=a;VER(comparar(&a,&b,NULL,0)==-1);
    b.datos=NULL;VER(comparar(&a,&b,&o,1)==-1);
    /* Una variable interna ausente es legítima si no forma parte del contrato. */
    IdeVarMain dos[2]={v,v};strcpy(dos[1].nombre,"interna");
    unsigned char *datos2[]={dx[0],dx[0]};size_t largos2[]={sizeof x,sizeof x};
    a.variables=dos;a.cantidad_variables=2;a.datos=datos2;a.largos_datos=largos2;
    b.variables=&v;b.cantidad_variables=1;b.datos=dy;VER(comparar(&a,&b,&o,1)==1);
    /* El homónimo de otra declaración no satisface el selector obligatorio. */
    strcpy(dos[1].nombre,"salida");dos[1].linea_declaracion=9;
    o.linea=9;VER(comparar(&a,&b,&o,1)==-1);
    o.linea=o.columna=0;VER(comparar(&a,&b,&o,1)==-1);
    /* Una diferencia probada prevalece incluso si falta otro resultado. */
    a.variables=&v;a.cantidad_variables=1;a.datos=dx;a.largos_datos=largo;
    o=(IdeObservable){"salida",4,5};IdeObservable req[2]={{"ausente",0,0},o};
    y[17]=9;VER(comparar(&a,&b,req,2)==0);
    printf("comparador: %s\n",fallos?"FAIL":"PASS");return fallos?1:0;
}
