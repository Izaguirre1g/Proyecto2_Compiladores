%% Constantes y ambitos: un nombre local no debe cambiar el valor de una
%% constante que otro valor constante usa. Resultados esperados:
%%   f(100) = TABLA[0] + N(parametro) = 5 + 100 = 105
%%   Q = TABLA[P] = 8, la suma del cycle = 6, i (constante) = 50
declare_const N * declare_int : 5;
declare_const P * declare_int : 1;
declare_const TABLA * declare_int[2] : [N, 8];
declare_const Q * declare_int : TABLA[P];
declare_const i * declare_int : 50;

create_funk #declare_int# f(N * declare_int) {
    give TABLA[0] gauss N;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[4] : [0, 0, 0, 0];
    s * declare_int : 0;
    cycle i let 0 until 4 {
        s : s gauss i;
    } endgame;
    resultados[0] : f(100);
    resultados[1] : Q;
    resultados[2] : s;
    resultados[3] : i;
}
