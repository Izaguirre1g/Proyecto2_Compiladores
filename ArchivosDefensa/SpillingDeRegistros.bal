%% ============================================================================
%% SPILLING DE REGISTROS (traduccion de SpillingDeRegistros.py)
%% ----------------------------------------------------------------------------
%% Ocho senales y 22 valores intermedios que siguen vivos hasta la suma
%% final: hay muchos mas valores vivos que registros disponibles, asi que
%% el asignador de registros tiene que guardar algunos en memoria (spill)
%% y volver a cargarlos cuando la suma final los necesita.
%%
%% Diferencias con Python:
%%   - En BAL una funcion recibe como maximo 4 argumentos (limite del
%%     backend), y pasar las 8 senales en un arreglo hace que el programa ya
%%     no quepa en la memoria de instrucciones (1024 bundles) en -O0. Por eso
%%     el cuerpo de analizar_senales_multiples va directamente en main, con
%%     s1..s8 como variables locales (los argumentos 1, 2, ..., 8).
%%     Las operaciones son exactamente las mismas, en el mismo orden.
%%   - La suma final de Python ocupa dos lineas con parentesis; en BAL va en
%%     una sola expresion.
%%
%% Resultado esperado (senales 1, 2, 3, 4, 5, 6, 7, 8):
%%   a=3 b=7 c=11 d=15 e=21 f=165 g=-2 h=-2 i=-2 j=-2 k=-4 l=-4
%%   m=15 n=42 o=77 p=120 q=57 r=197 t=8 u=14 w=18 x=20
%%   resultado = 774
%% ============================================================================
create_funk #declare_infinite_void# main() {
    s1 * declare_int : 1;
    s2 * declare_int : 2;
    s3 * declare_int : 3;
    s4 * declare_int : 4;
    s5 * declare_int : 5;
    s6 * declare_int : 6;
    s7 * declare_int : 7;
    s8 * declare_int : 8;

    a * declare_int : s1 gauss s2;
    b * declare_int : s3 gauss s4;
    c * declare_int : s5 gauss s6;
    d * declare_int : s7 gauss s8;

    e * declare_int : a pitagoras b;
    f * declare_int : c pitagoras d;

    g * declare_int : s1 neumann s3;
    h * declare_int : s2 neumann s4;
    i * declare_int : s5 neumann s7;
    j * declare_int : s6 neumann s8;

    k * declare_int : g gauss h;
    l * declare_int : i gauss j;

    m * declare_int : a pitagoras s5;
    n * declare_int : b pitagoras s6;
    o * declare_int : c pitagoras s7;
    p * declare_int : d pitagoras s8;

    q * declare_int : m gauss n;
    r * declare_int : o gauss p;

    t * declare_int : s1 pitagoras s8;
    u * declare_int : s2 pitagoras s7;
    w * declare_int : s3 pitagoras s6;
    x * declare_int : s4 pitagoras s5;

    resultado * declare_int : e gauss f gauss k gauss l gauss m gauss n gauss o gauss p gauss q gauss r gauss t gauss u gauss w gauss x gauss a gauss b gauss c gauss d gauss g gauss h gauss i gauss j;

}
