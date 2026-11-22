%% ============================================================================
%% LOOP UNROLLING 2: N VARIABLE PEQUENA
%% ----------------------------------------------------------------------------
%% El limite del ciclo esta en una VARIABLE:
%%   - En main, n = 5 se asigna justo antes del ciclo: el unrolling puede
%%     saber su valor y desenrolla TOTAL (5 copias).
%%   - En sumar_desde(), n es un PARAMETRO: su valor no se conoce al
%%     compilar. Se desenrolla PARCIAL con factor k = 4 y se deja el ciclo
%%     original como RESIDUO para las vueltas que no completan un grupo de 4.
%% Las llamadas recorren los bordes del desenrollado parcial:
%%   0 vueltas (no entra ni al grupo ni al residuo), 1 y 3 vueltas (solo el
%%   residuo), 4 vueltas (un grupo exacto, sin residuo) y 9 vueltas
%%   (dos grupos + 1 de residuo), con un inicio negativo.
%%
%% Resultados esperados (resultados[k]):
%%   [0] en main, x = 2x + 1 cinco veces desde 0 = 31
%%   [1] sumar_desde(10, 0) = 0
%%   [2] sumar_desde(10, 1) = 10
%%   [3] sumar_desde(10, 3) = 10 + 11 + 12 = 33
%%   [4] sumar_desde(10, 4) = 10 + 11 + 12 + 13 = 46
%%   [5] sumar_desde(-3, 9) = -3 - 2 - 1 + 0 + 1 + 2 + 3 + 4 + 5 = 9
%% ============================================================================
create_funk #declare_int# sumar_desde(a * declare_int, n * declare_int) {
    s * declare_int : 0;
    cycle k let 0 until n {
        s : s gauss a gauss k;
    } endgame;
    give s;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[6] : [0, 0, 0, 0, 0, 0];
    x * declare_int : 0;
    n * declare_int : 5;
    cycle i let 0 until n {
        x : x gauss x gauss 1;
    } endgame;

    resultados[0] : x;
    resultados[1] : sumar_desde(10, 0);
    resultados[2] : sumar_desde(10, 1);
    resultados[3] : sumar_desde(10, 3);
    resultados[4] : sumar_desde(10, 4);
    resultados[5] : sumar_desde(neumann 3, 9);
}
