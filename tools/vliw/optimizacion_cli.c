/* Conecta -O1 con ide_dce_analizar y el backend tradicional. No baja IR a ISA.
 * Recibe un archivo BAL, valida su alcance y produce una copia privada para el
 * compilador existente. El límite de 2 MiB evita reservas/recorridos ilimitados.
 * Los temporales del IDE quedan aislados: nunca se usa su nombre fijo junto al
 * fuente del usuario. No se admiten módulos hasta acreditar reescritura global. */
#define _POSIX_C_SOURCE 200809L
#include "optimizacion_cli.h"
#include "ide_dce.h"
#include "lexer.h"
#include "semantic_modules.h"
#include "memory.h"
#include "enlace_funciones.h"
#include "codificacion_binaria.h"
#include "cli.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Publica un diagnóstico con la ruta original; no consume memoria prestada. */
static int rechazo(FILE *d, const char *ruta, const char *causa, int codigo)
{
    if (d) fprintf(d,"%s: -O1: %s\n",ruta?ruta:"(sin ruta)",causa);
    return codigo;
}
/* Los nombres pertenecen exclusivamente al directorio mkdtemp (modo 0700).
 * Las APIs IDE normalmente retiran su copia; el rechazo también la retira. */
void vliw_opt_fuente_liberar(VliwOptFuente *f)
{
    if (!f) return;
    if (f->ruta) { remove(f->ruta); free(f->ruta); }
    if (f->directorio) {
        char nombre[128];
        int n=snprintf(nombre,sizeof nombre,"%s/.ide_entrada.bal",f->directorio);
        if (n>0 && (size_t)n<sizeof nombre) remove(nombre);
        rmdir(f->directorio); free(f->directorio);
    }
    memset(f,0,sizeof *f);
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
int vliw_opt_fuente_preparar(const char *entrada,VliwOptFuente *salida,FILE *diag)
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
    /* El lexer real distingue bring de comentarios/cadenas. Rechazamos módulos
     * antes de trasladar el fuente: no se inventan rutas de imports ni ámbitos. */
    Lexer lex;lexer_init(&lex,texto,largo,entrada);
    for (;;) {
        Token t=lexer_next(&lex);TokenType tipo=t.type;TokenError error=t.error;token_dispose(&t);
        if (tipo==TOKEN_ERROR || error==TOKEN_OUT_OF_MEMORY || tipo==TOKEN_BRING) {
            free(texto);return rechazo(diag,entrada,tipo==TOKEN_BRING?"módulos bring pendientes de reescritura acreditada":"fuente léxico inválido o recursos insuficientes",error==TOKEN_OUT_OF_MEMORY?2:1);
        }
        if (tipo==TOKEN_EOF) break;
    }
    VliwOptFuente nuevo={0};IdeDce dce={0};int estado=2;
    nuevo.directorio=strdup("/tmp/balc-opt-XXXXXX");
    if (!nuevo.directorio) goto fin;
    if (!mkdtemp(nuevo.directorio)) {free(nuevo.directorio);nuevo.directorio=NULL;goto fin;}
    size_t n=strlen(nuevo.directorio)+sizeof "/entrada.bal";
    nuevo.ruta=malloc(n);if (!nuevo.ruta) goto fin;
    (void)snprintf(nuevo.ruta,n,"%s/entrada.bal",nuevo.directorio);
    /* El IDE trabaja sobre el snapshot privado y reconstruye AST/IR/CFG. Su
     * reescritor y sus validaciones se reutilizan sin tocar algoritmos de José. */
    if (ide_dce_analizar(texto,largo,nuevo.ruta,&dce)) goto fin;
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
