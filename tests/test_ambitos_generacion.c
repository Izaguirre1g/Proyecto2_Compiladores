#include "sintactico.h"
#include "semantic.h"
#include "memory.h"
#include "generacion_adaptador.h"
#include "modelo_control.h"

static void generar(const char *fuente, char *texto, size_t capacidad)
{
    Ast ast = {0}; SimTabla tabla = {0}; MemProyectoMapa mapa;
    assert(construir_ast(fuente, strlen(fuente), "ambitos.bal", stderr, &ast) == 0);
    assert(sim_inicializar(&tabla) == SIM_OK);
    assert(analizar_semantica_modo(&ast, &tabla, "ambitos.bal", stderr,
                                 SEM_MODULO_IMPORTADO) == 0);
    assert(mem_asignar_proyecto(&tabla, 0x4000, 65536, 32768, &mapa) == MEM_PROYECTO_OK);
    assert(mapa.funciones == 1);
    const AstNodo *funcion = ast.raiz->primer_hijo;
    while (funcion && funcion->tipo != AST_FUNCION) funcion = funcion->siguiente;
    FILE *f = tmpfile(); assert(f);
    assert(gd_generar_funcion(funcion, &tabla, mapa.marco_maximo_bytes,
                             NULL, NULL, f, stderr) == 0);
    rewind(f);
    size_t n = fread(texto, 1, capacidad - 1, f);
    assert(!ferror(f) && feof(f)); texto[n] = 0;
    fclose(f);
    /* Un simbolo de bloque con ubicacion invalida debe fallar sin publicar. */
    SimSimbolo *local = NULL;
    for (SimSimbolo *s = tabla.primero; s; s = s->siguiente_orden)
        if (s->ambito->nivel > 1) { local = s; break; }
    assert(local);
    local->desplazamiento = (long)mapa.marco_maximo_bytes;
    f = tmpfile(); assert(f); fputs("previo\n", f);
    assert(gd_generar_funcion(funcion, &tabla, mapa.marco_maximo_bytes,
                             NULL, NULL, f, NULL) == 1);
    rewind(f); char previo[32]; assert(fgets(previo, sizeof(previo), f));
    assert(!strcmp(previo, "previo\n") && fgetc(f) == EOF);
    fclose(f); sim_liberar(&tabla); ast_liberar(&ast);
}

int main(void)
{
    char texto[16384], fuente[4096];
    /* Fixture de una sola función, sin main: generar() prueba módulos
     * importados. El ejemplo público es ahora un programa ejecutable (58e330d)
     * y no puede importarse según la semántica BAL. Conservamos el oráculo 44. */
    FILE *ejemplo = fopen("tests/modulos/ambitos_generacion.bal", "r");
    assert(ejemplo);
    size_t leidos = fread(fuente, 1, sizeof(fuente) - 1, ejemplo);
    assert(!ferror(ejemplo) && feof(ejemplo)); fuente[leidos] = 0;
    fclose(ejemplo);
    generar(fuente, texto, sizeof(texto));
    assert(ejecutar_control(texto) == 44);
    /* Todo en una linea: el orden se obtiene del AST, no de numeros de linea.
     * El primer uso de x en la rama ve x externo; despues de declararlo ve x=9.
     * El temporal del ciclo se inicializa nuevamente en cada vuelta. */
    generar("create_funk #declare_int# f() {"
            "x * declare_int : 1; r * declare_int : 0; i * declare_int : 0;"
            "whether (declare_true) { r : r gauss x;"
            "x * declare_int : 9; r : r gauss x;"
            "whale (i < 2) { temporal * declare_int : i gauss 1;"
            "r : r gauss temporal; i : temporal; } stop;"
            "} also { x * declare_int : 20; r : r gauss x; }"
            "r : r gauss x;"
            "whether (declare_true) { x * declare_int : 30; r : r gauss x; }"
            "give r; }", texto, sizeof(texto));
    assert(ejecutar_control(texto) == 44);

    /* Las ramas hermanas tienen nombres iguales y offsets diferentes. */
    generar("create_funk #declare_int# f() { r * declare_int : 0;"
            "whether (declare_false) { x * declare_int : 10; r : x; }"
            "also { x * declare_int : 20; r : x; } give r; }", texto, sizeof(texto));
    assert(ejecutar_control(texto) == 20);

    /* Bloques con llaves independientes, anidados y hermanos. */
    generar("create_funk #declare_int# f() { x * declare_int : 2;"
            "r * declare_int : 0; { x * declare_int : 7;"
            "{ x * declare_int : 8; r : x; } r : r gauss x; }"
            "{ x * declare_int : 9; r : r gauss x; } give r gauss x; }",
            texto, sizeof(texto));
    assert(ejecutar_control(texto) == 26);

    /* Parametro x (sp+4), r (sp+8), sombra x (sp+12). */
    generar("create_funk #declare_int# f(x * declare_int) {"
            "r * declare_int : x; whether (declare_true) {"
            "x * declare_int : 9; r : x; } give x; }", texto, sizeof(texto));
    assert(strstr(texto, "cargai x4, x2, 12\nguardap x2, x4, 8\n"));
    assert(strstr(texto, "cargai x4, x2, 4\nsumai x8, x4, 0\n"));

    /* Global x usa gp; la sombra es local y luego se vuelve a gp. */
    generar("declare_const x * declare_int : 3;"
            "create_funk #declare_int# f() { r * declare_int : x;"
            "whether (declare_true) { x * declare_int : 9; r : x; }"
            "give x; }", texto, sizeof(texto));
    assert(strstr(texto, "cargai x4, x2, 8\nguardap x2, x4, 4\n"));
    assert(strstr(texto, "cargai x4, x3, 0\nsumai x8, x4, 0\n"));

    /* cycle: limite exclusivo, paso implicito 1: 0+1+2+3+4. */
    generar("create_funk #declare_int# f() { r * declare_int : 0;"
            "cycle i let 0 until 5 { r : r gauss i; } endgame; give r; }",
            texto, sizeof(texto));
    assert(strstr(texto, "__gf_f_cycle_cond_0:\n"));
    assert(strstr(texto, "sye x0, __gf_f_cycle_cond_0\n"));
    assert(strstr(texto, "__gf_f_cycle_fin_0:\n"));
    assert(ejecutar_control(texto) == 10);

    /* Paso constante 2 (0, 2, 4) y descendente con neumann 1 (5..1). */
    generar("create_funk #declare_int# f() { r * declare_int : 0;"
            "cycle i let 0 until 5 step 2 { r : r gauss i; } endgame;"
            "cycle i let 5 until 0 step neumann 1 { r : r gauss i; } endgame;"
            "give r; }", texto, sizeof(texto));
    assert(ejecutar_control(texto) == 6 + 15);

    /* Paso variable: el sentido se decide en ejecucion (6, 4, 2 y 0, 3, 6). */
    generar("create_funk #declare_int# f() { r * declare_int : 0;"
            "p * declare_int : neumann 2;"
            "cycle i let 6 until 0 step p { r : r gauss i; } endgame;"
            "p : 3;"
            "cycle i let 0 until 7 step p { r : r gauss i; } endgame;"
            "give r; }", texto, sizeof(texto));
    assert(ejecutar_control(texto) == 12 + 9);

    /* Cero vueltas, ciclos anidados con whether y limite de variable externa. */
    generar("create_funk #declare_int# f() { r * declare_int : 0;"
            "n * declare_int : 3;"
            "cycle i let 3 until 3 { r : 100; } endgame;"
            "cycle i let 0 until n { cycle j let 0 until n {"
            "whether (i < j) { r : r gauss 1; } } endgame; } endgame;"
            "give r; }", texto, sizeof(texto));
    assert(ejecutar_control(texto) == 3);

    /* Un nombre local no se hace visible fuera de su bloque. */
    const char *invalido = "create_funk #declare_int# f() {"
        "whether (declare_true) { oculto * declare_int : 1; } give oculto; }";
    Ast ast = {0}; SimTabla tabla = {0};
    assert(construir_ast(invalido, strlen(invalido), "invalido.bal", stderr, &ast) == 0);
    assert(sim_inicializar(&tabla) == SIM_OK);
    assert(analizar_semantica_modo(&ast, &tabla, "invalido.bal", NULL,
                                 SEM_MODULO_IMPORTADO) == 1);
    sim_liberar(&tabla); ast_liberar(&ast);
    puts("OK: ambitos anidados, orden de declaracion, sombras, parametros y globales");
    return 0;
}
