%% Modulo de prueba: f retorna 44. Los x de cada bloque son distintos.
create_funk #declare_int# f() {
    x * declare_int : 1;
    r * declare_int : 0;
    i * declare_int : 0;
    whether (declare_true) {
        r : r gauss x;
        x * declare_int : 9;
        r : r gauss x;
        whale (i < 2) {
            temporal * declare_int : i gauss 1;
            r : r gauss temporal;
            i : temporal;
        } stop;
    }
    also {
        x * declare_int : 20;
        r : r gauss x;
    }
    r : r gauss x;
    whether (declare_true) {
        x * declare_int : 30;
        r : r gauss x;
    }
    give r;
}
