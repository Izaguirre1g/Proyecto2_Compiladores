%% ============================================================================
%% LOOP UNROLLING 3: N GRANDE
%% ----------------------------------------------------------------------------
%% Ciclos de cientos de vueltas. Pasan el maximo del desenrollado total
%% (16 vueltas), asi que se desenrollan PARCIAL con factor k = 4: un ciclo
%% que hace 4 vueltas por pasada y las N mod 4 que sobran en linea recta.
%%   - suma de 0..1002: N = 1003 -> 250 pasadas de 4 + 3 vueltas sueltas.
%%   - multiplos: el mismo n con step 3 (335 vueltas). Aqui el unrolling YA
%%     NO conoce n (su asignacion quedo en otro bloque basico, antes del
%%     primer ciclo): desenrolla PARCIAL x4 y deja el ciclo como residuo.
%%     El paso se multiplica en cada copia: j, j+3, j+6, j+9.
%%   - reloj: N = 500 (125 pasadas exactas de 4, sin sueltas), con un
%%     whether DENTRO del cuerpo: suma y "da la
%%     vuelta" al pasar de 1000. Cada vuelta depende de la anterior y cada
%%     copia conserva su propio salto interno; tambien cuenta las vueltas
%%     del reloj (dos variables cambian dentro de la rama).
%%
%% Resultados esperados (resultados[k]):
%%   [0] suma      = 502503  (1003 * 1002 / 2)
%%   [1] multiplos = 335     (0, 3, ..., 1002)
%%   [2] reloj     = 250     (h = h + 7 + k; si h >= 1000, h = h - 1000)
%%   [3] giros     = 128     (cuantas veces h paso de 1000)
%% ============================================================================
create_funk #declare_infinite_void# main() {
    resultados * declare_int[4] : [0, 0, 0, 0];
    n * declare_int : 1003;

    suma * declare_int : 0;
    cycle i let 0 until n {
        suma : suma gauss i;
    } endgame;

    multiplos * declare_int : 0;
    cycle j let 0 until n step 3 {
        multiplos : multiplos gauss 1;
    } endgame;

    reloj * declare_int : 0;
    giros * declare_int : 0;
    cycle k let 0 until 500 {
        reloj : reloj gauss 7 gauss k;
        whether (reloj >= 1000) {
            reloj : reloj neumann 1000;
            giros : giros gauss 1;
        }
    } endgame;

    resultados[0] : suma;
    resultados[1] : multiplos;
    resultados[2] : reloj;
    resultados[3] : giros;
}
