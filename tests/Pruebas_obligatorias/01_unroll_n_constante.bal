%% ============================================================================
%% LOOP UNROLLING 1: N CONSTANTE
%% ----------------------------------------------------------------------------
%% Cuatro ciclos cuyo numero de vueltas esta escrito en el codigo. El
%% unrolling calcula N al compilar y los desenrolla TOTAL: N copias del
%% cuerpo, sin comparacion ni saltos. Cada uno prueba una trampa distinta:
%%   - suma:     N = 8, el caso base.
%%   - saltos:   step 3 y un limite que NO es multiplo del paso
%%               (k = 1, 4, 7, 10: son 4 vueltas, no 11 / 3).
%%   - doble:    cada vuelta depende de la anterior (d = 2d + m): las copias
%%               deben quedar en orden y cada una con SU valor de m.
%%   - anidado:  ciclo dentro de ciclo (3 x 4). El interno se desenrolla
%%               primero y despues el externo (12 copias en total).
%%
%% Resultados esperados (resultados[k]):
%%   [0] suma    = 28    (0 + 1 + ... + 7)
%%   [1] saltos  = 22    (1 + 4 + 7 + 10)
%%   [2] doble   = 121   (1 -> 2 -> 5 -> 12 -> 27 -> 58 -> 121)
%%   [3] anidado = -6    (suma de a - b, con a en 0..2 y b en 0..3)
%% ============================================================================
create_funk #declare_infinite_void# main() {
    resultados * declare_int[4] : [0, 0, 0, 0];

    suma * declare_int : 0;
    cycle i let 0 until 8 {
        suma : suma gauss i;
    } endgame;

    saltos * declare_int : 0;
    cycle k let 1 until 12 step 3 {
        saltos : saltos gauss k;
    } endgame;

    d * declare_int : 1;
    cycle m let 0 until 6 {
        d : d gauss d gauss m;
    } endgame;

    anidado * declare_int : 0;
    cycle a let 0 until 3 {
        cycle b let 0 until 4 {
            anidado : anidado gauss a neumann b;
        } endgame;
    } endgame;

    resultados[0] : suma;
    resultados[1] : saltos;
    resultados[2] : d;
    resultados[3] : anidado;
}
