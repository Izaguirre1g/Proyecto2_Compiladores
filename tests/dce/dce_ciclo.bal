%% Prueba DCE 2: codigo muerto dentro de un ciclo, con una llamada con
%% efectos en el cuerpo. "extra" esta muerta; al quitarla, "base" tambien
%% queda muerta, pero eso se ve recien en la SEGUNDA pasada (diapositiva 28:
%% eliminar codigo muerto puede generar nuevo codigo muerto).
create_funk #declare_int# acumular(v * declare_int[], i * declare_int) {
    v[i] : v[i] gauss 1;
    give v[i];
}

create_funk #declare_infinite_void# main() {
    conteo * declare_int[4] : [0, 0, 0, 0];
    base * declare_int : 10;
    extra * declare_int : 0;
    i * declare_int : 0;
    whale (i < 4) {
        extra : base gauss i;   %% muerta: nadie lee extra
        acumular(conteo, i);    %% escribe en conteo: se queda
        i : i gauss 1;          %% viva: la lee la condicion del ciclo
    } stop;
}
