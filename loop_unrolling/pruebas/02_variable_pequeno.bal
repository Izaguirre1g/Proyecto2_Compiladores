%% Loop unrolling 2: N en una variable pequena (n = 3).
%% El valor de n se conoce al entrar al ciclo, asi que se desenrolla TOTAL.
%% El mismo ciclo dentro de una funcion con n como parametro (valor
%% desconocido) se desenrolla PARCIAL con un ciclo residuo.
%% Resultado esperado: producto = 1*2*3 = 6 y r = 0+1+2 = 3.
create_funk #declare_int# sumar_hasta(n * declare_int) {
    suma * declare_int : 0;
    cycle i let 0 until n {
        suma : suma gauss i;
    } endgame;
    give suma;
}

create_funk #declare_infinite_void# main() {
    n * declare_int : 3;
    producto * declare_int : 1;
    cycle i let 0 until n {
        producto : producto pitagoras (i gauss 1);
    } endgame;
    r * declare_int : sumar_hasta(3);
}
