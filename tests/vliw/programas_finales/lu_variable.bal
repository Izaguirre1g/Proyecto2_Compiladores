%% N pequeño como parámetro: el cuerpo suma 0..2 y devuelve 3.
create_funk #declare_int# sumar(n * declare_int) {
 suma * declare_int : 0;
 cycle i let 0 until n {
 suma : suma gauss i;
 } endgame;
 give suma;
}
%% N variable en función: conserva llamada y resultado 3.
create_funk #declare_infinite_void# main() {
 resultados * declare_int[1] : [0];
 muerto * declare_int : 91;
 valor * declare_int : sumar(3);
 resultados[0] : valor;
}
