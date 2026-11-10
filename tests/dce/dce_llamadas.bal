%% Prueba DCE 1: asignaciones muertas mezcladas con llamadas con efectos.
%% registrar() ESCRIBE en el arreglo que recibe: aunque nadie use lo que
%% devuelve, quitar la llamada cambiaria el contenido de "datos".
create_funk #declare_int# registrar(v * declare_int[], valor * declare_int) {
    v[0] : valor;
    give valor;
}

create_funk #declare_infinite_void# main() {
    datos * declare_int[3] : [0, 0, 0];
    a * declare_int : 4;
    b * declare_int : a pitagoras 2;        %% muerta: nadie lee b
    c * declare_int : registrar(datos, a);  %% c muerta, pero la llamada se queda
    registrar(datos, 7);                    %% resultado ignorado: la llamada se queda
    d * declare_int : a gauss 1;            %% muerta: nadie lee d
}
