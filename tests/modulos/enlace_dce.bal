%% -O1 (DCE) con modulos: se borra "tirar" del principal; el modulo se
%% compila tal como esta. Tambien asigna una llamada a un elemento de arreglo.
%% Resultados esperados: resultados[0] = 8, resultados[1] = 20.
bring enlace_muerto aka m;

create_funk #declare_infinite_void# main() {
    resultados * declare_int[2] : [0, 0];
    tirar * declare_int : 99;
    a * declare_int : m.doble(4);
    resultados[0] : a;
    resultados[1] : m.doble(10);
}
