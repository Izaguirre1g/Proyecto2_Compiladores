%% Reordenamiento 6 (ciclo + funciones): se usa con --unroll --reorder.
%% Primero se desenrolla el ciclo; luego el bloque grande que queda se
%% reordena. El whether del cuerpo separa bloques (control) y la funcion
%% tiene su propio bloque.
%% Resultado esperado: a = 6, b = 30 (2*(0+1+2+3+4) + 10).
create_funk #declare_int# doble(v * declare_int) {
    give v pitagoras 2;
}

create_funk #declare_infinite_void# main() {
    a * declare_int : 0;
    b * declare_int : 0;
    cycle i let 0 until 5 {
        a : a gauss 1;
        b : b gauss i;
        b : b gauss i;
        whether (i == 4) {
            b : b gauss 10;
        }
    } endgame;
    a : a gauss 1;
    c * declare_int : doble(a);
}
