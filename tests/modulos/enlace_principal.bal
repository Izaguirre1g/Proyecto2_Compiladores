%% Programa con dos modulos: enlace_util y operaciones (que a su vez
%% importa matematicas). Resultados esperados en el simulador:
%%   resultados[0] = u.usar(3)      = (3 + 100) + (3 * 2) = 109
%%   resultados[1] = u.local_tapa() = 100 + 7             = 107
%%   resultados[2] = u.base                                = 100
%%   resultados[3] = op.duplicar(5) = 5 + 5               = 10
bring enlace_util aka u;
bring operaciones aka op;

create_funk #declare_infinite_void# main() {
    resultados * declare_int[4] : [0, 0, 0, 0];
    a * declare_int : u.usar(3);
    b * declare_int : u.local_tapa();
    c * declare_int : u.base;
    d * declare_int : op.duplicar(5);
    resultados[0] : a;
    resultados[1] : b;
    resultados[2] : c;
    resultados[3] : d;
}
