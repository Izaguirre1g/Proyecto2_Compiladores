#ifndef JAVIER_CLI_H
#define JAVIER_CLI_H

/* Compilador segun la especificacion: siempre genera el binario y las
 * opciones -o, -v, -s, -t, -m, -x y -p agregan salidas. */
int cli_run(int argc, char **argv);

/* Modo interno de pruebas del frontend (balc --frontend [-v] [-t] fuente):
 * solo lexico, -v lista los tokens y -t valida sintaxis e imprime el AST.
 * No genera archivos. Lo usan test_parser y test_cli. */
int cli_frontend(int argc, char **argv);

/* Mapa de memoria del contrato: globales desde 0x4000 hasta 0x7FFF y pila
 * desde 0x10000 hacia abajo (0x8000-0xFFFF). El codigo empieza en 0.
 * Estan aqui (y no solo en cli.c) para que el IDE genere el MISMO
 * ensamblador que balc (src/IDE/ide_ensamblador.c). */
#define CLI_GP_BASE 0x4000u
#define CLI_FIN_GLOBALES 0x8000u
#define CLI_TOPE_PILA 0x10000u
/* Bytes maximos del marco de una funcion (mem_asignar_proyecto). */
#define CLI_CAPACIDAD_MARCO 1020u

#endif
