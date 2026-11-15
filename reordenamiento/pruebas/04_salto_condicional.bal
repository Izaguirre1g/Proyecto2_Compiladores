%% Reordenamiento 4 (control): bloques separados por un whether.
%% Nada cruza el salto condicional ni sale de su bloque: solo se reordena
%% DENTRO de cada bloque, y la condicion queda antes del ifFalse.
%% Resultado esperado: x = 10, y = 7, mayor = 1, z = 17.
create_funk #declare_infinite_void# main() {
    x * declare_int : 4 gauss 6;
    y * declare_int : 2 gauss 5;
    k * declare_int : x pitagoras 2;
    mayor * declare_int : 0;
    whether (k > y) {
        mayor : 1;
        y : y gauss 0;
    }
    z * declare_int : x gauss y;
}
