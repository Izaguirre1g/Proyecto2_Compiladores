/* Valida propiedad, publicación y recursos del adaptador de niveles.
 * Recibe un fuente de fixture y usa el DCE/lexer reales. Solo las reservas
 * propias de optimizacion_cli.c se interceptan: no se atribuye a esta prueba
 * inyección exhaustiva sobre los algoritmos compartidos del frontend/IDE. */
#define _POSIX_C_SOURCE 200809L
#include "../../tools/vliw/optimizacion_cli.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
static size_t reservas,fallo;
/* Las implementaciones compartidas se enlazan sin renombrar sus reservas.
 * El fallo se limita a los tres puntos propios del adaptador nuevo. */
void *jf_malloc(size_t n){if (++reservas==fallo) return NULL;return malloc(n);}
char *jf_strdup(const char *s){if (++reservas==fallo) return NULL;size_t n=strlen(s)+1;char *p=malloc(n);if(p)memcpy(p,s,n);return p;}
/* Lee el snapshot para probar que permanece independiente del fuente original.
 * El buffer pertenece al llamador; no se compara padding de estructuras. */
static char *leer(const char *p)
{
 FILE *f=fopen(p,"rb");assert(f);assert(fseek(f,0,SEEK_END)==0);long n=ftell(f);assert(n>=0);rewind(f);char *s=calloc((size_t)n+1,1);assert(s);assert(fread(s,1,(size_t)n,f)==(size_t)n);assert(fclose(f)==0);return s;
}
/* Ejecuta un contrato por proceso: éxito con snapshot propio, rechazos sin
 * publicación o fallo de cada reserva seguido de recuperación. El driver es
 * dueño de sus buffers y comprueba que la entrada prestada permanece intacta. */
int main(int argc,char **argv)
{
 assert(argc==3);VliwOptFuente out={0};char *antes=leer(argv[2]);
 /* El snapshot debe cambiar por DCE y desaparecer junto con su directorio,
  * sin retirar el fuente ni dejar rutas utilizables después de liberar. */
 if (!strcmp(argv[1],"positivas")) {
  reservas=0;assert(vliw_opt_fuente_preparar(argv[2],&out,stderr)==0);assert(out.ruta && out.directorio && out.cambio_bal && out.eliminadas_ir>0);
  char *nuevo=leer(out.ruta);assert(strcmp(antes,nuevo));free(nuevo);
  char *ruta=strdup(out.ruta),*dir=strdup(out.directorio);assert(ruta && dir);
  vliw_opt_fuente_liberar(&out);assert(!out.ruta && !out.directorio);assert(access(ruta,F_OK)!=0 && access(dir,F_OK)!=0);free(ruta);free(dir);vliw_opt_fuente_liberar(&out);
 } else if (!strcmp(argv[1],"negativas")) {
  /* Un resultado ocupado no se sobrescribe; los argumentos inválidos fallan
   * antes de tomar propiedad de recursos del llamador. */
  assert(vliw_opt_fuente_preparar(NULL,&out,NULL)!=0);assert(vliw_opt_fuente_preparar(argv[2],NULL,NULL)!=0);
  out.eliminadas_ir=1;assert(vliw_opt_fuente_preparar(argv[2],&out,NULL)!=0 && out.eliminadas_ir==1);memset(&out,0,sizeof out);
  assert(vliw_opt_fuente_preparar("/no/existe/fuente.bal",&out,NULL)!=0 && !out.ruta);
  vliw_opt_fuente_liberar(NULL);
 } else {
  assert(!strcmp(argv[1],"recursos"));
  /* Fallan lectura, adquisición del directorio o nombre de snapshot. Cada
   * rechazo debe dejar salida vacía y permitir otra construcción independiente. */
  for (fallo=1;fallo<=3;++fallo) {
   reservas=0;assert(vliw_opt_fuente_preparar(argv[2],&out,NULL)!=0 && !out.ruta && !out.directorio);
   size_t f=fallo;fallo=0;assert(vliw_opt_fuente_preparar(argv[2],&out,NULL)==0);vliw_opt_fuente_liberar(&out);fallo=f;
  }
 }
 char *despues=leer(argv[2]);assert(!strcmp(antes,despues));free(antes);free(despues);puts("PASS propiedad, inmutabilidad y publicación del adaptador");return 0;
}
