%% Reordenamiento 3 (WAR + WAW + RAW): la misma variable "t" se reutiliza
%% en tres calculos seguidos. Solo pueden adelantarse las instrucciones
%% que no tocan "t".
%% Resultado esperado: t = 8, u = 3, v = 7, w = 9, z = 14.
create_funk #declare_infinite_void# main() {
    m * declare_int : 2;
    n * declare_int : 3;
    t * declare_int : m gauss 1;
    u * declare_int : t;
    t : m pitagoras n;
    v * declare_int : t gauss 1;
    t : t gauss m;
    w * declare_int : n pitagoras n;
    z * declare_int : v gauss v;
}
