%% ============================================================================
%% REORDENAMIENTO 2: FUNCIONES, RECURSION Y LLAMADAS COMO BARRERAS
%% ----------------------------------------------------------------------------
%% Las llamadas son barreras: el reordenamiento no puede mover nada a traves
%% de una llamada (los argumentos se pasan en orden y la funcion puede
%% leer o escribir memoria). Entre una llamada y otra si puede reordenar.
%%   - fact(): recursion DENTRO de un whether; "n" se usa DESPUES de la
%%     llamada recursiva, asi que tiene que sobrevivirla (cada nivel tiene su
%%     propio marco).
%%   - fib(): DOS llamadas recursivas seguidas dentro de un whether; la
%%     segunda no puede adelantarse a la primera ni mezclarse con la suma.
%%   - absoluto(): el valor que se devuelve depende de una rama.
%%   (Cada funcion tiene un solo give al final: un give dentro de un whether
%%   deja un salto inalcanzable en la IR que -O1 no puede quitar del codigo
%%   BAL, y entonces -O1 no compila el programa.)
%%   - combinar(): recibe el resultado de OTRA llamada (fib(6)) como argumento.
%%   - en main, entre las llamadas hay calculos independientes (w, z) que el
%%     reordenamiento puede adelantar, y otros que dependen del resultado de
%%     la llamada anterior y no pueden subir.
%%
%% Resultados esperados (resultados[k]):
%%   [0] fact(7)  = 5040
%%   [1] fib(10)  = 55
%%   [2] absoluto(-5) = 5
%%   [3] combinar(3, fib(6), 4) = 3 + 8 - 4 = 7   (una llamada como argumento)
%%   [4] z - w = 3   (w = 12 y z = 15 se calculan ENTRE llamadas)
%% ============================================================================
create_funk #declare_int# fact(n * declare_int) {
    r * declare_int : 1;
    whether (n > 1) {
        sub * declare_int : fact(n neumann 1);
        r : n pitagoras sub;
    }
    give r;
}

create_funk #declare_int# fib(n * declare_int) {
    r * declare_int : n;
    whether (n >= 2) {
        a * declare_int : fib(n neumann 1);
        b * declare_int : fib(n neumann 2);
        r : a gauss b;
    }
    give r;
}

create_funk #declare_int# absoluto(n * declare_int) {
    r * declare_int : n;
    whether (n < 0) {
        r : neumann n;
    }
    give r;
}

create_funk #declare_int# combinar(a * declare_int, b * declare_int, c * declare_int) {
    give a gauss b neumann c;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[5] : [0, 0, 0, 0, 0];
    u * declare_int : 3;
    v * declare_int : 4;

    f7 * declare_int : fact(7);
    w * declare_int : u gauss u gauss u gauss u;
    z * declare_int : w gauss u;
    f10 * declare_int : fib(10);
    c1 * declare_int : absoluto(neumann 5);
    k * declare_int : combinar(u, fib(6), v);

    resultados[0] : f7;
    resultados[1] : f10;
    resultados[2] : c1;
    resultados[3] : k;
    resultados[4] : z neumann w;
}
