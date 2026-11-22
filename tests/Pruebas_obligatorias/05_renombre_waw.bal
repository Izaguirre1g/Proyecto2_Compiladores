%% ============================================================================
%% RENOMBRAMIENTO 2: DEPENDENCIAS WAW (dos escrituras al mismo nombre)
%% ----------------------------------------------------------------------------
%% "t" se escribe seis veces en el mismo bloque. Entre dos escrituras
%% seguidas hay una dependencia WAW: sin renombrar, la segunda no puede
%% adelantarse a la primera, aunque calcula algo independiente. Ademas,
%% cada valor de "t" se lee antes de la escritura siguiente (WAR).
%% El renombramiento separa los seis valores (t1..t6) y el orden solo lo
%% imponen las dependencias verdaderas (RAW).
%% Trampas:
%%   - el valor FINAL de t debe ser el de la ultima escritura (t = 2^8),
%%     aunque las escrituras se reordenen;
%%   - "t : t gauss 1" lee y escribe t en la misma instruccion;
%%   - "u" tambien se reescribe con un valor que depende de su anterior.
%%
%% Resultados esperados (resultados[k]):
%%   [0] u1 = 24   (t = 7 + 5 = 12; u1 = 12 * 2)
%%   [1] u2 = 35   (t = 7 * 5 = 35)
%%   [2] u3 = 36   (t = 35 + 1)
%%   [3] u  = 18   (u = 36 / 2, guardado ANTES de reescribir u)
%%   [4] v  = 4    (t = 7 - 5 = 2; v = 2 * 2)
%%   [5] u  = 22   (u = u + v)
%%   [6] t  = 256  (t = 2^8, la ultima escritura)
%%   [7] total = 377 (u1 + u2 + u3 + u + v + t = 24+35+36+22+4+256)
%% ============================================================================
create_funk #declare_infinite_void# main() {
    resultados * declare_int[8] : [0, 0, 0, 0, 0, 0, 0, 0];
    p * declare_int : 7;
    q * declare_int : 5;

    t * declare_int : p gauss q;
    u1 * declare_int : t pitagoras 2;
    t : p pitagoras q;
    u2 * declare_int : t;
    t : t gauss 1;
    u3 * declare_int : t;
    u * declare_int : t euclides 2;
    resultados[3] : u;
    t : p neumann q;
    v * declare_int : t pitagoras t;
    u : u gauss v;
    t : 2 descartes 8;

    resultados[0] : u1;
    resultados[1] : u2;
    resultados[2] : u3;
    resultados[4] : v;
    resultados[5] : u;
    resultados[6] : t;
    resultados[7] : u1 gauss u2 gauss u3 gauss u gauss v gauss t;
}
