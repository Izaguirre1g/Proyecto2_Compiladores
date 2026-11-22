%% Modulo para las pruebas de enlace (tests/test_enlace_simulador.c).
%% Tiene una constante, llamadas internas y nombres locales que tapan a
%% la constante: el enlace no debe confundirlos.
declare_const base * declare_int : 100;

create_funk #declare_int# ayudar(x * declare_int) {
    give x gauss base;
}

create_funk #declare_int# tapar(base * declare_int) {
    give base pitagoras 2;
}

create_funk #declare_int# local_tapa() {
    r * declare_int : base;
    base * declare_int : 7;
    give r gauss base;
}

create_funk #declare_int# usar(x * declare_int) {
    give ayudar(x) gauss tapar(x);
}
