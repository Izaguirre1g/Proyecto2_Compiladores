%% ============================================================================
%% RENOMBRAMIENTO 3: WAR Y WAW DENTRO DE CICLOS Y FUNCIONES
%% ----------------------------------------------------------------------------
%% Rotaciones de variables, el caso mas dificil para el renombramiento:
%%   - fibonacci(): "t : a + b; a : b; b : t" en cada vuelta. a y b se leen
%%     y se reescriben (WAR), y t se reescribe en cada vuelta (WAW). Con el
%%     unrolling, las copias del cuerpo quedan en un solo bloque y el
%%     renombramiento tiene que encadenar bien las versiones de a, b y t.
%%   - tribonacci(): la misma idea con tres variables rotando.
%%   - mcd(): algoritmo de Euclides con whale: "r : a mod b; a : b; b : r".
%%
%% Resultados esperados (resultados[k]):
%%   [0] fibonacci(20)  = 6765
%%   [1] fibonacci(1)   = 1
%%   [2] tribonacci(15) = 1705   (0, 0, 1, 1, 2, 4, 7, ...)
%%   [3] mcd(1071, 462) = 21
%%   [4] mcd(17, 5)     = 1
%% ============================================================================
create_funk #declare_int# fibonacci(n * declare_int) {
    a * declare_int : 0;
    b * declare_int : 1;
    t * declare_int : 0;
    cycle i let 0 until n {
        t : a gauss b;
        a : b;
        b : t;
    } endgame;
    give a;
}

create_funk #declare_int# tribonacci(n * declare_int) {
    a * declare_int : 0;
    b * declare_int : 0;
    c * declare_int : 1;
    t * declare_int : 0;
    cycle i let 0 until n {
        t : a gauss b gauss c;
        a : b;
        b : c;
        c : t;
    } endgame;
    give a;
}

create_funk #declare_int# mcd(a * declare_int, b * declare_int) {
    r * declare_int : 0;
    whale (b =/= 0) {
        r : a euler b;
        a : b;
        b : r;
    } stop;
    give a;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[5] : [0, 0, 0, 0, 0];
    resultados[0] : fibonacci(20);
    resultados[1] : fibonacci(1);
    resultados[2] : tribonacci(15);
    resultados[3] : mcd(1071, 462);
    resultados[4] : mcd(17, 5);
}
