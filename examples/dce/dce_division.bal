%% Prueba DCE 3: operaciones que pueden fallar y llamadas cuyo resultado
%% se descarta. Una division entre una variable puede ser division entre
%% cero: quitarla cambiaria el comportamiento, asi que se queda.
create_funk #declare_int# promedio(v * declare_int[], n * declare_int) {
    suma * declare_int : v[0] gauss v[1];
    doble * declare_int : suma pitagoras 2;   %% muerta: nadie lee doble
    give suma euclides n;
}

create_funk #declare_int# calcular(notas * declare_int[], divisor * declare_int) {
    riesgo * declare_int : 100 euclides divisor;  %% muerta, pero puede fallar: se queda
    seguro * declare_int : 100 euclides 4;        %% muerta y no puede fallar: se elimina
    p * declare_int : promedio(notas, 2);         %% p muerta, la llamada se queda
    give divisor;
}

create_funk #declare_infinite_void# main() {
    notas * declare_int[2] : [80, 90];
    r * declare_int : calcular(notas, 0);
}
