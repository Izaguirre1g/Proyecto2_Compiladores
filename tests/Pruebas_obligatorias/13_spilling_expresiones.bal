%% ============================================================================
%% SPILLING 1: EXPRESIONES CON MUCHOS VALORES VIVOS A LA VEZ
%% ----------------------------------------------------------------------------
%% -O2 trabaja con solo 3 registros temporales. Una expresion en forma de
%% arbol balanceado de 8 hojas necesita 4 resultados parciales vivos al
%% mismo tiempo: no caben en 3, asi que el spilling guarda alguno en la pila
%% (spill store) y lo vuelve a cargar cuando lo usa (spill load).
%%   - e1, e2, e3: tres arboles de 8 hojas, con sumas y restas mezcladas
%%     (el orden de las restas importa: un valor recargado en el lugar
%%     equivocado cambia el signo).
%%   - e4: otro arbol de 8 hojas hecho con e1, e2, e3 y total (todos vivos).
%%   - total: usa e1, e2 y e3 junto con las 12 variables originales. Las 12
%%     siguen vivas desde el principio hasta el final: tambien compiten por
%%     los registros con los resultados parciales.
%% Las hojas son variables (no constantes) para que nada se calcule al
%% compilar. (El backend -O0 tiene 4 temporales: estos arboles caben justo.)
%%
%% a..l = 1, 2, ..., 12
%% Resultados esperados (resultados[k]):
%%   [0] e1 = 36    ((1+2)+(3+4)) + ((5+6)+(7+8))
%%   [1] e2 = 2     ((9-10)+(11-12)) - ((1-3)+(5-7)) = -2 - (-4)
%%   [2] e3 = -6    ((2+4)-(6+8)) + ((10+12)-(9+11)) = -8 + 2
%%   [3] total = 106  (e1 - e2 + e3 + a + b + ... + l = 36 - 2 - 6 + 78)
%%   [4] e4 = -51   ((e1-a)+(e2-b)) - ((e3-c)+(total-k)) = 35 - (-9 + 95)
%% ============================================================================
create_funk #declare_infinite_void# main() {
    resultados * declare_int[5] : [0, 0, 0, 0, 0];
    a * declare_int : 1;
    b * declare_int : 2;
    c * declare_int : 3;
    d * declare_int : 4;
    e * declare_int : 5;
    f * declare_int : 6;
    g * declare_int : 7;
    h * declare_int : 8;
    i * declare_int : 9;
    j * declare_int : 10;
    k * declare_int : 11;
    l * declare_int : 12;

    e1 * declare_int : ((a gauss b) gauss (c gauss d)) gauss ((e gauss f) gauss (g gauss h));
    e2 * declare_int : ((i neumann j) gauss (k neumann l)) neumann ((a neumann c) gauss (e neumann g));
    e3 * declare_int : ((b gauss d) neumann (f gauss h)) gauss ((j gauss l) neumann (i gauss k));
    total * declare_int : e1 neumann e2 gauss e3 gauss a gauss b gauss c gauss d gauss e gauss f
                        gauss g gauss h gauss i gauss j gauss k gauss l;

    resultados[0] : e1;
    resultados[1] : e2;
    resultados[2] : e3;
    e4 * declare_int : ((e1 neumann a) gauss (e2 neumann b)) neumann ((e3 neumann c) gauss (total neumann k));

    resultados[3] : total;
    resultados[4] : e4;
}
