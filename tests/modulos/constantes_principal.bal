%% Constantes importadas: d.DOBLE = 14, d.esquina() = 4 + 14 = 18,
%% d.MATRIZ[1][0] = 3 (arreglo constante de otro archivo).
bring constantes_datos aka d;
create_funk #declare_infinite_void# main() {
    resultados * declare_int[3] : [0, 0, 0];
    a * declare_int : d.DOBLE;
    b * declare_int : d.esquina();
    c * declare_int : d.MATRIZ[1][0];
    resultados[0] : a;
    resultados[1] : b;
    resultados[2] : c;
}
