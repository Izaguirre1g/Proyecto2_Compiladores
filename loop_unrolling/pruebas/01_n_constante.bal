%% Loop unrolling 1: N constante (5 vueltas). Se espera desenrollado TOTAL.
%% Resultado esperado: suma = 0+1+2+3+4 = 10.
create_funk #declare_infinite_void# main() {
    suma * declare_int : 0;
    cycle i let 0 until 5 {
        suma : suma gauss i;
    } endgame;
}
