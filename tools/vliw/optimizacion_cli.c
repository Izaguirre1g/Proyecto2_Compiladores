/* Conecta -O1 con ide_dce_analizar y el backend tradicional. No baja IR a ISA.
 * Recibe un archivo BAL, valida su alcance y produce una copia privada para el
 * compilador existente. El límite de 2 MiB evita reservas/recorridos ilimitados.
 * Los temporales del IDE quedan aislados: nunca se usa su nombre fijo junto al
 * fuente del usuario. Con `bring`, los módulos importados (y los que ellos
 * importan) se copian al mismo directorio privado: así la copia de trabajo
 * los encuentra igual que el fuente original. El DCE solo reescribe el texto
 * del programa principal; los módulos se compilan tal como están. */
#define _POSIX_C_SOURCE 200809L
#include "optimizacion_cli.h"
#include "ide_dce.h"
#include "lexer.h"
#include "semantic_modules.h"
#include "memory.h"
#include "enlace_funciones.h"
#include "codificacion_binaria.h"
#include "cli.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Publica un diagnóstico con la ruta original; no consume memoria prestada. */
static int rechazo(FILE *d, const char *ruta, const char *causa, int codigo)
{
    if (d) fprintf(d,"%s: -O1: %s\n",ruta?ruta:"(sin ruta)",causa);
    return codigo;
}
/* Los nombres pertenecen exclusivamente al directorio mkdtemp (modo 0700):
 * la copia de trabajo, el temporal del IDE y los módulos copiados. Se borran
 * todos los archivos del directorio y después el directorio. */
void vliw_opt_fuente_liberar(VliwOptFuente *f)
{
    if (!f) return;
    if (f->ruta) { remove(f->ruta); free(f->ruta); }
    if (f->directorio) {
        DIR *d=opendir(f->directorio);
        if (d) {
            struct dirent *e;
            while ((e=readdir(d))!=NULL) {
                if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
                char nombre[512];
                int n=snprintf(nombre,sizeof nombre,"%s/%s",f->directorio,e->d_name);
                if (n>0 && (size_t)n<sizeof nombre) remove(nombre);
            }
            closedir(d);
        }
        rmdir(f->directorio); free(f->directorio);
    }
    memset(f,0,sizeof *f);
}
/* Lee un archivo completo (hasta `limite` bytes). 1 bien, 0 si no se pudo. */
static int leer_todo(const char *ruta, size_t limite, char **texto, size_t *largo)
{
    FILE *f=fopen(ruta,"rb"); if (!f) return 0;
    char *b=malloc(limite+1); if (!b) { fclose(f); return 0; }
    size_t n=fread(b,1,limite+1,f); int io=ferror(f); if (fclose(f)) io=1;
    if (io || n>limite) { free(b); return 0; }
    b[n]=0; *texto=b; *largo=n; return 1;
}
/* Copia a `destino` cada módulo que `texto` importa (`bring nombre`) desde
 * `origen`, y recursivamente los que importan esos módulos. Un módulo que no
 * existe no se copia: la semántica lo informa después con su mensaje normal.
 * 0 bien, 1 si un nombre choca con la copia de trabajo, 2 por E/S o memoria. */
static int copiar_modulos(const char *origen, const char *destino, const char *texto,
                          size_t largo, const char *ruta, unsigned profundidad, FILE *diag)
{
    if (profundidad > 64) return rechazo(diag,ruta,"demasiados niveles de bring",1);
    Lexer lex; lexer_init(&lex,texto,largo,ruta);
    int despues_de_bring=0, estado=0;
    while (!estado) {
        Token t=lexer_next(&lex);
        TokenType tipo=t.type;
        if (tipo==TOKEN_EOF || tipo==TOKEN_ERROR) { token_dispose(&t); break; }
        if (despues_de_bring && tipo==TOKEN_IDENTIFIER && t.lexeme) {
            if (!strcmp(t.lexeme,"entrada")) {
                estado=rechazo(diag,ruta,"un módulo llamado 'entrada' choca con la copia de trabajo",1);
            } else {
                char de[4096], a[4096];
                int n1=snprintf(de,sizeof de,"%s/%s.bal",origen,t.lexeme);
                int n2=snprintf(a,sizeof a,"%s/%s.bal",destino,t.lexeme);
                char *modulo=NULL; size_t largo_modulo=0;
                if (n1<0 || n2<0 || (size_t)n1>=sizeof de || (size_t)n2>=sizeof a) estado=2;
                else if (access(a,F_OK)!=0 && leer_todo(de,2u*1024u*1024u,&modulo,&largo_modulo)) {
                    FILE *f=fopen(a,"wb");
                    if (!f || fwrite(modulo,1,largo_modulo,f)!=largo_modulo) estado=2;
                    if (f && fclose(f)) estado=2;
                    if (!estado) estado=copiar_modulos(origen,destino,modulo,largo_modulo,de,profundidad+1,diag);
                    free(modulo);
                }
            }
        }
        despues_de_bring = tipo==TOKEN_BRING;
        token_dispose(&t);
    }
    return estado;
}
/* Escribe solo la copia de trabajo. Un fallo no convierte un resultado DCE
 * incompleto en una compilación de referencia ni permite fallback silencioso. */
static int guardar(const char *ruta, const char *texto, size_t largo)
{
    FILE *f=fopen(ruta,"wb"); if (!f) return 0;
    int ok=fwrite(texto,1,largo,f)==largo;
    if (fclose(f)) ok=0;
    return ok;
}
/* El IDE conserva su backend escalar histórico. Si ese verificador rechaza
 * la extensión de referencias, O1 debe acreditar de nuevo ambas variantes
 * con el backend CLI ampliado; no basta ignorar hay_ensamblador. El visitante
 * consume tabla/AST prestados y desecha todo código de validación. */
static int validar_referencias(void *ctx,const Ast *ast,SimTabla *tabla,FILE *diag)
{
    (void)ctx;MemProyectoMapa mapa={0};
    if(mem_asignar_proyecto_referencias(tabla,CLI_GP_BASE,CLI_FIN_GLOBALES,
                                      CLI_CAPACIDAD_MARCO,&mapa)!=MEM_PROYECTO_OK)return 1;
    FILE *f=tmpfile();if(!f)return 2;
    EfArranque arranque={CLI_GP_BASE,CLI_TOPE_PILA};
    int rc=ef_enlazar_referencias(ast,tabla,0,&arranque,NULL,f,diag,NULL);
    if(fclose(f))rc=2;
    return rc;
}
int vliw_opt_fuente_preparar_con_observables(const char *entrada,VliwOptFuente *salida,FILE *diag,
    const IdeObservable *observables,size_t cantidad)
{
    if (!entrada || !salida || salida->ruta || salida->directorio || salida->eliminadas_ir || salida->borradas_bal ||
        salida->cambiadas_bal || salida->conservadas_bal || salida->cambio_bal)
        return rechazo(diag,entrada,"salida vacía y fuente requeridos",1);
    FILE *f=fopen(entrada,"rb");
    if (!f) return rechazo(diag,entrada,"no se pudo leer el fuente",2);
    const size_t limite=2u*1024u*1024u;
    char *texto=malloc(limite+1);
    if (!texto) { fclose(f);return rechazo(diag,entrada,"memoria insuficiente",2); }
    size_t largo=fread(texto,1,limite+1,f);int io=ferror(f);if (fclose(f)) io=1;
    if (io || largo>limite || memchr(texto,0,largo)) {
        free(texto);return rechazo(diag,entrada,"E/S, fuente binario o capacidad superior a 2 MiB",io?2:1);
    }
    texto[largo]=0;
    /* El lexer real distingue bring de comentarios/cadenas; aquí solo se
     * rechaza un fuente léxicamente inválido. */
    int hay_bring=0;
    Lexer lex;lexer_init(&lex,texto,largo,entrada);
    for (;;) {
        Token t=lexer_next(&lex);TokenType tipo=t.type;TokenError error=t.error;token_dispose(&t);
        if (tipo==TOKEN_ERROR || error==TOKEN_OUT_OF_MEMORY) {
            free(texto);return rechazo(diag,entrada,"fuente léxico inválido o recursos insuficientes",error==TOKEN_OUT_OF_MEMORY?2:1);
        }
        if (tipo==TOKEN_BRING) hay_bring=1;
        if (tipo==TOKEN_EOF) break;
    }
    VliwOptFuente nuevo={0};IdeDce dce={0};int estado=2;
    nuevo.directorio=strdup("/tmp/balc-opt-XXXXXX");
    if (!nuevo.directorio) goto fin;
    if (!mkdtemp(nuevo.directorio)) {free(nuevo.directorio);nuevo.directorio=NULL;goto fin;}
    size_t n=strlen(nuevo.directorio)+sizeof "/entrada.bal";
    nuevo.ruta=malloc(n);if (!nuevo.ruta) goto fin;
    (void)snprintf(nuevo.ruta,n,"%s/entrada.bal",nuevo.directorio);
    if (hay_bring) {   /* Los módulos se buscan junto al fuente original. */
        char origen[4096];
        const char *barra=strrchr(entrada,'/');
        int m=barra ? snprintf(origen,sizeof origen,"%.*s",(int)(barra-entrada),entrada)
                    : snprintf(origen,sizeof origen,".");
        if (m<0 || (size_t)m>=sizeof origen) goto fin;
        if (barra==entrada) strcpy(origen,"/");
        int r=copiar_modulos(origen,nuevo.directorio,texto,largo,entrada,0,diag);
        if (r) { estado=r; goto fin; }
    }
    /* El IDE trabaja sobre el snapshot privado y reconstruye AST/IR/CFG. Su
     * reescritor y sus validaciones se reutilizan sin tocar algoritmos de José. */
    if (ide_dce_analizar_con_observables(texto,largo,nuevo.ruta,observables,cantidad,&dce)) goto fin;
    if (dce.estado!=IDE_DCE_LISTO) {
        estado=rechazo(diag,entrada,dce.motivo?dce.motivo:"análisis semántico/IR rechazado; no se genera código",1);goto fin;
    }
    if (dce.resultado.cantidad_eliminadas && !dce.codigo_nuevo) {
        estado=rechazo(diag,entrada,"DCE modifica IR pero no ofrece BAL reescrito acreditado",1);goto fin;
    }
    if (dce.codigo_nuevo && dce.hay_ensamblador!=1) {
        if(!guardar(nuevo.ruta,texto,largo) ||
           sem_visitar_proyecto(nuevo.ruta,diag,validar_referencias,NULL) ||
           !guardar(nuevo.ruta,dce.codigo_nuevo,strlen(dce.codigo_nuevo)) ||
           sem_visitar_proyecto(nuevo.ruta,diag,validar_referencias,NULL)) {
            estado=rechazo(diag,entrada,"ambas variantes deben admitir el backend de referencias",1);goto fin;
        }
    }
    const char *elegido=dce.codigo_nuevo?dce.codigo_nuevo:texto;
    if (!guardar(nuevo.ruta,elegido,strlen(elegido))) goto fin;
    nuevo.eliminadas_ir=dce.resultado.cantidad_eliminadas;
    nuevo.borradas_bal=dce.sentencias_borradas;nuevo.cambiadas_bal=dce.sentencias_cambiadas;
    nuevo.conservadas_bal=dce.sentencias_conservadas;nuevo.cambio_bal=strcmp(elegido,texto)!=0;
    /* Una pasada sin cambios es válida y se informa como tal. No se promete
     * eliminar toda instrucción IR si el reescritor conserva una sentencia. */
    *salida=nuevo;memset(&nuevo,0,sizeof nuevo);estado=0;
fin:
    ide_dce_liberar(&dce);free(texto);vliw_opt_fuente_liberar(&nuevo);
    if (estado==2) rechazo(diag,entrada,"fallo de recursos o E/S durante DCE",2);
    return estado;
}

/* La ruta histórica no añade resultados ni altera sus métricas. */
int vliw_opt_fuente_preparar(const char *entrada,VliwOptFuente *salida,FILE *diag)
{ return vliw_opt_fuente_preparar_con_observables(entrada,salida,diag,NULL,0); }
