%% Prueba DCE 1: asignaciones muertas mezcladas con llamadas con efectos.
%% registrar() ESCRIBE en el arreglo que recibe: aunque nadie use lo que
%% devuelve, quitar la llamada cambiaria el contenido de "datos".
create_funk #declare_int# registrar(v * declare_int[], valor * declare_int) {
    v[0] : valor;
    give valor;
}

%% Las variables locales de una funcion desaparecen al hacer give: si nadie
%% las lee antes, son codigo muerto.
create_funk #declare_int# procesar(v * declare_int[], a * declare_int) {
    b * declare_int : a pitagoras 2;     %% muerta: nadie lee b
    c * declare_int : registrar(v, a);   %% c muerta, pero la llamada se queda
    registrar(v, 7);                     %% resultado ignorado: la llamada se queda
    d * declare_int : a gauss 1;         %% muerta: nadie lee d
    give a;
}

%% En main, el resultado son las variables que el programa usa. Un valor
%% que se reemplaza antes de leerse sigue siendo codigo muerto.
create_funk #declare_infinite_void# main() {
    datos * declare_int[3] : [0, 0, 0];
    total * declare_int : 0;             %% muerta: se reemplaza antes de leerse
    total : procesar(datos, 4);          %% viva: se lee en la linea siguiente
    datos[1] : total;                    %% guarda el resultado en el arreglo
}
