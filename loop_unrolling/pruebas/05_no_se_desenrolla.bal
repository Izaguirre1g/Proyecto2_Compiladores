%% Loop unrolling 5: ciclos que NO deben desenrollarse.
%% - el cuerpo modifica el iterador (whale);
%% - el cuerpo modifica el limite;
%% - el limite es una expresion calculada en la cabecera (n gauss 1).
create_funk #declare_infinite_void# main() {
    i * declare_int : 0;
    n * declare_int : 10;
    whale (i < n) {
        i : i gauss 2;
        i : i gauss 1;
    } stop;
    j * declare_int : 0;
    whale (j < n) {
        n : n neumann 1;
        j : j gauss 1;
    } stop;
    k * declare_int : 0;
    cycle m let 0 until n gauss 1 {
        k : k gauss m;
    } endgame;
}
