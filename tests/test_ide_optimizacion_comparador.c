/* Regresión del comparador REAL, incluido como unidad privada para acceder
 * a comparar_resultados sin publicar una API nueva. Los arreglos sintéticos
 * son préstamos de la pila y no se pasan al liberador de resultados.
 * El grupo programas compila BAL y ejecuta los productores/simulador reales.
 * O1_COMPARADOR_SOURCE permite reproducir el defecto con la fuente base. */
#ifndef O1_COMPARADOR_SOURCE
#define O1_COMPARADOR_SOURCE "../src/IDE/ide_optimizacion.c"
#endif
#include O1_COMPARADOR_SOURCE

static unsigned fallos, comprobaciones;
#define COMPROBAR(x) do { ++comprobaciones; if (!(x)) { \
    fprintf(stderr,"FAIL L%d: %s\n",__LINE__,#x); ++fallos; } } while (0)

/* Igualdad visual intencional: cambiar SOLO la cola de los datos debe producir
 * DIFERENTE aunque los dos resúmenes sigan mostrando los mismos 16 elementos.
 * Cada nivel usa su marco y offset: nunca se comparan direcciones como valores. */
static void arreglos(void)
{
    unsigned char ma[MET_BYTES_MEMORIA]={0}, mb[MET_BYTES_MEMORIA]={0};
    IdeVarMain va={.nombre="salida",.desplazamiento=4,.elementos=20,.bytes_elemento=4};
    IdeVarMain vb=va; vb.desplazamiento=12;
    char *resumen[]={"[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, …]"};
    IdeOptNivel a={.compilado=1,.marco_main=128,.variables=&va,.cantidad_variables=1,
                   .valores=resumen,.memoria_final=ma};
    IdeOptNivel b=a;b.marco_main=256;b.variables=&vb;b.memoria_final=mb;
    a.simulacion.estado=b.simulacion.estado=MET_LISTO;
    size_t base=CLI_TOPE_PILA-b.marco_main+(size_t)vb.desplazamiento;
    COMPROBAR(comparar_resultados(&a,&b)==1); puts("iguales 20: comprobado");
    mb[base+16*4]=1;
    COMPROBAR(comparar_resultados(&a,&b)==0); puts("diferencia elemento 17: comprobado");
    mb[base+16*4]=0;mb[base+19*4]=2;
    COMPROBAR(comparar_resultados(&a,&b)==0); puts("diferencia ultimo: comprobado");
    mb[base+19*4]=0;vb.elementos=21;
    COMPROBAR(comparar_resultados(&a,&b)==0); puts("tamanos distintos: comprobado");
    va.elementos=vb.elementos=16;
    COMPROBAR(comparar_resultados(&a,&b)==1); puts("iguales 16: comprobado");
    vb.es_boolean=1;COMPROBAR(comparar_resultados(&a,&b)==0);vb.es_boolean=0;
    /* El contrato estricto del compañero se mantiene: ausencia -> 0;
     * no compilado/no terminado -> -1. Nunca se acepta igualdad por vacío. */
    b.cantidad_variables=0;b.valores=NULL;COMPROBAR(comparar_resultados(&a,&b)==0);
    b.cantidad_variables=1;b.valores=resumen;b.compilado=0;
    COMPROBAR(comparar_resultados(&a,&b)==-1);b.compilado=1;
    b.simulacion.estado=MET_SIN_TERMINAR;COMPROBAR(comparar_resultados(&a,&b)==-1);
    b.simulacion.estado=MET_LISTO;
    /* Un objeto fuera de marco o con multiplicación imposible no se lee. */
    vb.elementos=SIZE_MAX;COMPROBAR(comparar_resultados(&a,&b)==-1);
    vb.elementos=16;vb.desplazamiento=-1;COMPROBAR(comparar_resultados(&a,&b)==-1);
    vb.desplazamiento=250;COMPROBAR(comparar_resultados(&a,&b)==-1);
    vb.desplazamiento=12;b.memoria_final=NULL;COMPROBAR(comparar_resultados(&a,&b)==-1);
    /* Dos escalares con resúmenes idénticos: la igualdad depende de DMEM.
     * Cambiar sólo los bytes debe producir desigualdad, nunca igualdad visual. */
    b.memoria_final=mb;va.elementos=vb.elementos=1;
    COMPROBAR(comparar_resultados(&a,&b)==1);
    mb[base]=7;COMPROBAR(comparar_resultados(&a,&b)==0);

}

/* Dos declaraciones homónimas mantienen el orden semántico que ya usa el
 * productor. La segunda no puede satisfacer el valor de la primera ni su
 * ausencia puede hacer que el conjunto se considere equivalente. */
static void homonimos(void)
{
    unsigned char ma[MET_BYTES_MEMORIA]={0}, mb[MET_BYTES_MEMORIA]={0};
    IdeVarMain va[2]={{.nombre="x",.desplazamiento=4,.elementos=1,.bytes_elemento=4},
                     {.nombre="x",.desplazamiento=8,.elementos=1,.bytes_elemento=4}};
    IdeVarMain vb[2]={va[0],va[1]};vb[0].desplazamiento=12;vb[1].desplazamiento=20;
    char *resumen[]={"1","2"};
    IdeOptNivel a={.compilado=1,.marco_main=32,.variables=va,.cantidad_variables=2,
                   .valores=resumen,.memoria_final=ma};
    IdeOptNivel b=a;b.variables=vb;b.memoria_final=mb;
    a.simulacion.estado=b.simulacion.estado=MET_LISTO;
    size_t base=CLI_TOPE_PILA-32;
    ma[base+4]=mb[base+12]=1;ma[base+8]=mb[base+20]=2;
    COMPROBAR(comparar_resultados(&a,&b)==1);
    mb[base+20]=9;COMPROBAR(comparar_resultados(&a,&b)==0);
    mb[base+20]=2;b.cantidad_variables=1;COMPROBAR(comparar_resultados(&a,&b)==0);
}

/* Busca un objeto del fixture y comprueba su valor producido desde DMEM.
 * Los resultados de analizar son propios y se liberan en el llamador. */
static void comprobar_valor(const IdeOptNivel *n,const char *nombre,const char *esperado)
{
    size_t i=0;while(i<n->cantidad_variables && strcmp(n->variables[i].nombre,nombre))++i;
    COMPROBAR(i<n->cantidad_variables);
    if(i<n->cantidad_variables)COMPROBAR(n->valores && !strcmp(n->valores[i],esperado));
}

/* Compila y simula los tres niveles sin configuración manual de observables.
 * Los oráculos son los resultados matemáticos de los fixtures existentes.
 * Toda reserva productiva se libera aunque falle una comprobación de valor. */
static void programa(const char *ruta,const char *const *nombres,const char *const *valores,
                     size_t cantidad,int exigir_dce)
{
    FILE *f=fopen(ruta,"rb");COMPROBAR(f!=NULL);if(!f)return;
    char texto[65536];size_t largo=fread(texto,1,sizeof texto-1,f);
    COMPROBAR(!ferror(f) && feof(f));fclose(f);texto[largo]=0;
    IdeOptimizacion r={0};int rc=ide_optimizacion_analizar(texto,largo,ruta,&r);
    COMPROBAR(rc==0);if(rc)return;
    const IdeOptNivel *niveles[]={&r.o0,&r.o1,&r.o2};
    for(size_t k=0;k<3;++k){
        const IdeOptNivel *n=niveles[k];
        COMPROBAR(n->compilado && n->simulacion.estado==MET_LISTO);
        for(size_t i=0;i<cantidad;++i)comprobar_valor(n,nombres[i],valores[i]);
        printf("%s O%zu bundles=%zu bytes=%zu ciclos=%llu DCE_O1=%zu\n",ruta,k,
               n->bundles,n->bundles*16,(unsigned long long)n->simulacion.sim.ciclos,r.o1_eliminadas_ir);
    }
    COMPROBAR(r.mismo_o1==1 && r.mismo_o2==1 && r.mismo_resultado==1);
    COMPROBAR(r.o0.cantidad_variables==r.o1.cantidad_variables);
    if(exigir_dce)COMPROBAR(r.o1_eliminadas_ir>0 && r.o1_cambio);
    ide_optimizacion_liberar(&r);
}

/* Muerto auxiliar y una escritura sobrescrita se pueden eliminar, pero la
 * dependencia x->necesario y la llamada que incrementa datos deben permanecer.
 * No se imponen métricas históricas de otra política de observabilidad. */
static void auxiliar(void)
{
    const char *t="create_funk #declare_int# registrar(v * declare_int[]) {\n"
        "    v[0] : v[0] gauss 1;\n    give 0;\n}\n"
        "create_funk #declare_int# calcular(v * declare_int[]) {\n"
        "    muerto * declare_int : 123;\n    x * declare_int : 99;\n"
        "    x : 5;\n    necesario * declare_int : x gauss 2;\n"
        "    descartado * declare_int : registrar(v);\n    give necesario;\n}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    datos * declare_int[1] : [0];\n    resultado * declare_int : calcular(datos);\n"
        "    otra_final * declare_int : 41;\n}\n";
    IdeOptimizacion r={0};int rc=ide_optimizacion_analizar(t,strlen(t),"tests/editor_comparador.bal",&r);
    COMPROBAR(rc==0);if(rc)return;
    const IdeOptNivel *niveles[]={&r.o0,&r.o1,&r.o2};
    for(size_t k=0;k<3;++k){
        COMPROBAR(niveles[k]->compilado && niveles[k]->simulacion.estado==MET_LISTO);
        comprobar_valor(niveles[k],"datos","1");comprobar_valor(niveles[k],"resultado","7");
        comprobar_valor(niveles[k],"otra_final","41");
    }
    COMPROBAR(r.mismo_resultado==1 && r.o1_eliminadas_ir>0 && r.o1_cambio);
    COMPROBAR(r.o1_codigo && !strstr(r.o1_codigo,": 123;") && !strstr(r.o1_codigo,": 99;"));
    COMPROBAR(r.o1_codigo && strstr(r.o1_codigo,"registrar(v)"));
    ide_optimizacion_liberar(&r);
}

/* Verifica que la captura real del simulador conserva también la cola de un
 * arreglo de 20 elementos. El resumen sigue limitado a MAX_ELEMENTOS=16. */
static void captura_real(void)
{
    const char *t="create_funk #declare_infinite_void# main() {\n"
        "    salida * declare_int[20] : [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19];\n}\n";
    IdeOptimizacion r={0};int rc=ide_optimizacion_analizar(t,strlen(t),"tests/editor_arreglo.bal",&r);
    COMPROBAR(rc==0);if(rc)return;
    COMPROBAR(r.mismo_resultado==1 && r.o1.cantidad_variables==1);
    const IdeOptNivel *niveles[]={&r.o0,&r.o1,&r.o2};
    for(size_t k=0;k<3;++k){
        const IdeOptNivel *n=niveles[k];COMPROBAR(n->memoria_final && n->cantidad_variables==1);
        if(n->memoria_final && n->cantidad_variables==1){
            size_t base=CLI_TOPE_PILA-n->marco_main+(size_t)n->variables[0].desplazamiento;
            COMPROBAR(n->variables[0].elementos==20);
            COMPROBAR(n->memoria_final[base+16*4]==16 && n->memoria_final[base+19*4]==19);
            COMPROBAR(n->valores && strstr(n->valores[0],"…"));
        }
    }
    ide_optimizacion_liberar(&r);
}

int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"comparador")){arreglos();homonimos();}
    else if(!strcmp(argv[1],"programas")){
        const char *m[]={"fila_encontrada","columna_encontrada"},*mv[]={"0","1"};
        const char *p[]={"resultado","vacio","combinado","total"},*pv[]={"24","0","13","37"};
        programa("examples/ejercicio2_busqueda_matriz.bal",m,mv,2,0);
        programa("examples/ejercicio3_puntajes.bal",p,pv,4,0);
        const char *n7[]={"resultados","datos"},*v7[]={"[100, 60]","[60, 20, 30, 0, 0]"};
        programa("tests/Pruebas_obligatorias/07_dce_llamadas_con_efectos.bal",n7,v7,2,1);
        const char *n8[]={"resultados","hist"},*v8[]={"[16, 15, 9]","[1, 2, 3, 4, 15]"};
        programa("tests/Pruebas_obligatorias/08_dce_efectos_en_ciclos.bal",n8,v8,2,1);
        const char *n9[]={"resultados","res"},*v9[]={"21","[21, 7, 25, 10]"};
        programa("tests/Pruebas_obligatorias/09_dce_operaciones_que_fallan.bal",n9,v9,2,1);
        auxiliar();captura_real();
    }else return 2;
    printf("%s: %u comprobaciones, %u fallos\n",argv[1],comprobaciones,fallos);
    return fallos?1:0;
}
