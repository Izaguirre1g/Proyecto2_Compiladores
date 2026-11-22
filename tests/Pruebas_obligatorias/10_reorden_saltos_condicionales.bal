%% ============================================================================
%% REORDENAMIENTO 1: SALTOS CONDICIONALES (seguridad del control)
%% ----------------------------------------------------------------------------
%% Clasifica 8 valores con una cadena whether / alif / alif / also dentro de
%% un ciclo. Cada rama tiene varias instrucciones, unas encadenadas (RAW) y
%% otras independientes: el reordenamiento las intercala DENTRO de su
%% bloque basico para esconder la latencia, pero por seguridad:
%%   - nada puede subir por encima de la condicion ni del ifFalse;
%%   - nada de una rama puede pasar a otra rama ni salir despues del salto;
%%   - minimo y maximo se reescriben en ramas distintas y su valor depende
%%     del orden en que se recorren los datos.
%% Tambien hay condiciones compuestas con && y ||, y una negacion ~( ).
%%
%% Datos: [12, -7, 0, 33, 5, -20, 7, -1]
%% Resultados esperados (resultados[k]):
%%   [0] negativos = 3          [1] ceros = 1
%%   [2] chicos = 2 (1..9)      [3] grandes = 2 (>= 10)
%%   [4] puntos = 120   (negativo: 1 - 2x; chico: 3x; grande: x - 10)
%%   [5] minimo = -20           [6] maximo = 33
%%   [7] especiales = 4 (entre 10 y 50, o exactamente -1, o -7: 12, 33, -7, -1)
%% ============================================================================
create_funk #declare_infinite_void# main() {
    resultados * declare_int[8] : [0, 0, 0, 0, 0, 0, 0, 0];
    datos * declare_int[8] : [12, neumann 7, 0, 33, 5, neumann 20, 7, neumann 1];
    negativos * declare_int : 0;
    ceros * declare_int : 0;
    chicos * declare_int : 0;
    grandes * declare_int : 0;
    puntos * declare_int : 0;
    minimo * declare_int : datos[0];
    maximo * declare_int : datos[0];
    especiales * declare_int : 0;
    x * declare_int : 0;
    aporte * declare_int : 0;

    cycle i let 0 until 8 {
        x : datos[i];
        whether (x < 0) {
            negativos : negativos gauss 1;
            aporte : 1 neumann x neumann x;
            puntos : puntos gauss aporte;
        }
        alif (x == 0) {
            ceros : ceros gauss 1;
        }
        alif (x < 10) {
            chicos : chicos gauss 1;
            aporte : x gauss x gauss x;
            puntos : puntos gauss aporte;
        }
        also {
            grandes : grandes gauss 1;
            aporte : x neumann 10;
            puntos : puntos gauss aporte;
        }
        whether (x < minimo) {
            minimo : x;
        }
        whether (~(x <= maximo)) {
            maximo : x;
        }
        whether ((x > 10 && x < 50) || x == neumann 1 || x == neumann 7) {
            especiales : especiales gauss 1;
        }
    } endgame;

    resultados[0] : negativos;
    resultados[1] : ceros;
    resultados[2] : chicos;
    resultados[3] : grandes;
    resultados[4] : puntos;
    resultados[5] : minimo;
    resultados[6] : maximo;
    resultados[7] : especiales;
}
