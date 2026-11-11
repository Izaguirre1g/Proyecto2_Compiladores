%% Prueba DCE 4: el ensamblador y el binario salen del codigo SIN el codigo
%% muerto. Solo usa parametros int (el backend todavia no acepta arreglos
%% como parametros), asi que se puede generar su ensamblador antes y despues.
create_funk #declare_int# cuadrado(x * declare_int) {
    doble * declare_int : x gauss x;      %% muerta: nadie lee doble
    give x pitagoras x;
}

create_funk #declare_int# suma_hasta(n * declare_int) {
    total * declare_int : 0;
    extra * declare_int : 0;              %% muerta: nadie lee extra
    aux * declare_int : cuadrado(n);      %% aux muerta, pero la llamada se queda
    i * declare_int : 1;
    whale (i <= n) {
        total : total gauss i;
        extra : i pitagoras 2;            %% muerta: nadie lee extra
        i : i gauss 1;
    } stop;
    give total;
}

%% En main, el resultado son las variables que el programa usa: r y s se
%% guardan en el arreglo resultados; suma nunca se lee: es codigo muerto.
create_funk #declare_infinite_void# main() {
    resultados * declare_int[2] : [0, 0];
    r * declare_int : 0;                  %% muerta: se reemplaza antes de leerse
    r : cuadrado(4);
    s * declare_int : suma_hasta(5);
    suma * declare_int : r gauss s;       %% muerta: nadie lee suma
    resultados[0] : r;
    resultados[1] : s;
}
