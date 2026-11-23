#include "cli.h"
#include "semantic_modules.h"

#include <stdio.h>
#include <string.h>

/* Punto de entrada: el CLI existente conserva todas sus opciones. */
int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--semantico") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Uso: %s --semantico archivo_principal.bal\n", argv[0]);
            return 2;
        }
        return sem_analizar_proyecto(argv[2], stderr);
    }
    /* Modo interno: argv[0] pasa a ser "--frontend", que el CLI ignora. */
    if (argc >= 2 && strcmp(argv[1], "--frontend") == 0)
        return cli_frontend(argc - 1, argv + 1);
    return cli_run(argc, argv);
}

