%% Loop unrolling 3: N en una variable grande (n = 1002).
%% Supera el maximo del desenrollado total: se desenrolla PARCIAL con
%% factor k; las 1002 mod k vueltas restantes quedan en linea recta.
%% Resultado esperado: suma = 2 * 1002 = 2004.
create_funk #declare_infinite_void# main() {
    n * declare_int : 1002;
    suma * declare_int : 0;
    cycle i let 0 until n {
        suma : suma gauss 2;
    } endgame;
}
