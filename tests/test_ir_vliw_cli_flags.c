/* Ejercita el parser interno de opciones sin requerir un parser .bal.
 * Secciones no referenciadas de cli.c se descartan en el enlace de esta
 * prueba aislada; no se invoca el backend ni se suplantan sus funciones. */
#include "../src/Linea_Comandos/cli.c"
#include <assert.h>

int main(void)
{
    Compilacion c = {0};
    char *valido[] = {"balc", "--vliw-ir=4", "-s", "-p", "x.bal"};
    assert(parse_compilacion(5, valido, &c) == 0);
    assert(c.vliw_ir && c.vliw_regs == 4 && c.ensamblador && c.paquetes);
    assert(!strcmp(c.entrada, "x.bal"));

    c = (Compilacion){0};
    char *default_regs[] = {"balc", "--vliw-ir", "x.bal"};
    assert(parse_compilacion(3, default_regs, &c) == 0);
    assert(c.vliw_ir && c.vliw_regs == 3);

    c = (Compilacion){0};
    char *mal_regs[] = {"balc", "--vliw-ir=2", "x.bal"};
    assert(parse_compilacion(3, mal_regs, &c) < 0);

    c = (Compilacion){0};
    char *sin_numero[] = {"balc", "--vliw-ir=", "x.bal"};
    assert(parse_compilacion(3, sin_numero, &c) < 0);

    c = (Compilacion){0};
    char *con_o[] = {"balc", "--vliw-ir", "-o", "salida.bin", "x.bal"};
    assert(parse_compilacion(5, con_o, &c) < 0);

    c = (Compilacion){0};
    char *con_ir[] = {"balc", "--vliw-ir", "--ir", "x.bal"};
    assert(parse_compilacion(4, con_ir, &c) < 0);

    c = (Compilacion){0};
    char *con_dce[] = {"balc", "--vliw-ir", "--dce", "x.bal"};
    assert(parse_compilacion(4, con_dce, &c) < 0);

    puts("OK: opciones --vliw-ir y rechazos de banderas incompatibles");
    return 0;
}
