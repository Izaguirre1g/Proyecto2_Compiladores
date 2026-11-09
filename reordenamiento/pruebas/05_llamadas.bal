%% Reordenamiento 5 (funciones): las llamadas tienen efectos y son
%% barreras. Los argumentos y las llamadas conservan su orden; solo se
%% reordena lo que esta entre una llamada y otra.
%% Resultado esperado: r1 = 5, r2 = 9, total = 14.
create_funk #declare_int# sumar(a * declare_int, b * declare_int) {
    s * declare_int : a gauss b;
    give s;
}

create_funk #declare_infinite_void# main() {
    x * declare_int : 1 gauss 1;
    y * declare_int : x pitagoras 3;
    r1 * declare_int : sumar(x, 3);
    w * declare_int : y gauss 1;
    z * declare_int : 2 pitagoras 2;
    r2 * declare_int : sumar(w, 2);
    total * declare_int : r1 gauss r2;
}
