%% Asignar el resultado de una llamada a un elemento de arreglo o matriz.
%% Resultados esperados: 9, 17, 25, 36.
create_funk #declare_int# cuadrado(x * declare_int) {
    give x pitagoras x;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[4] : [0, 0, 0, 0];
    m * declare_int[2][2] : [[0, 0], [0, 0]];
    i * declare_int : 2;
    resultados[0] : cuadrado(3);
    resultados[1] : cuadrado(4) gauss 1;
    resultados[i] : cuadrado(i gauss 3);
    m[1][0] : cuadrado(6);
    resultados[3] : m[1][0];
}
