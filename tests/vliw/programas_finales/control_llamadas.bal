%% Llamadas anidadas: (2+3)+(4+5)=14, con backend tradicional.
create_funk #declare_int# sumar(a * declare_int,b * declare_int) {
 give a gauss b;
}
%% La copia muerta se elimina, las llamadas y el observable 14 permanecen.
create_funk #declare_infinite_void# main() {
 resultados * declare_int[1] : [0];
 muerto * declare_int : 91;
 valor * declare_int : sumar(2,3);
 valor : sumar(valor,sumar(4,5));
 resultados[0] : valor;
}
