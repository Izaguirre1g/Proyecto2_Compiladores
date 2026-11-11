%% Reordenamiento 2 (WAW): "r" se escribe dos veces en el mismo bloque.
%% Las dos escrituras deben conservar su orden (WAW): el valor final de r
%% tiene que ser el de la segunda. Las cuentas con "p" son independientes.
%% Resultado esperado: r = 12, q = 25, s = 15.
create_funk #declare_infinite_void# main() {
    p * declare_int : 5;
    r * declare_int : p gauss 1;
    r : p gauss 7;
    q * declare_int : p pitagoras p;
    s * declare_int : r gauss 3;
}
