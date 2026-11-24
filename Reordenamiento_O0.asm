# __arranque:
sumai x3,x0,0 | nop | nop | nop
nop
nop
cizqi x3,x3,8 | nop | nop | nop
nop
nop
ori x3,x3,0 | nop | nop | nop
nop
nop
cizqi x3,x3,8 | nop | nop | nop
nop
nop
ori x3,x3,64 | nop | nop | nop
nop
nop
cizqi x3,x3,8 | nop | nop | nop
nop
nop
ori x3,x3,0 | nop | nop | nop
nop
nop
sumai x2,x0,0 | nop | nop | nop
nop
nop
cizqi x2,x2,8 | nop | nop | nop
nop
nop
ori x2,x2,1 | nop | nop | nop
nop
nop
cizqi x2,x2,8 | nop | nop | nop
nop
nop
ori x2,x2,0 | nop | nop | nop
nop
nop
cizqi x2,x2,8 | nop | nop | nop
nop
nop
ori x2,x2,0 | nop | nop | nop
nop
nop
sumai x13,x0,0 | nop | nop | nop
nop
nop
cizqi x13,x13,8 | nop | nop | nop
nop
nop
ori x13,x13,0 | nop | nop | nop
nop
nop
cizqi x13,x13,8 | nop | nop | nop
nop
nop
ori x13,x13,22 | nop | nop | nop
nop
nop
cizqi x13,x13,8 | nop | nop | nop
nop
nop
ori x13,x13,224 | nop | nop | nop
nop
nop
nop | nop | syer x1,x13,0 | nop
nop
nop
# __arranque_fin:
nop | nop | sye x0,0 | nop  # -> __arranque_fin
nop
nop
# __fn_analizar_sensor_reordenado:
restai x2,x2,40 | nop | nop | nop
nop
nop
nop | guardap x2,x1,0 | nop | nop
nop
nop
nop | guardap x2,x8,4 | nop | nop
nop
nop
nop | guardap x2,x9,8 | nop | nop
nop
nop
sumai x4,x0,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,12 | nop | nop
nop
nop
sumai x4,x0,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,16 | nop | nop
nop
nop
sumai x4,x0,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,20 | nop | nop
nop
nop
sumai x4,x0,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,24 | nop | nop
nop
nop
sumai x4,x0,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,28 | nop | nop
nop
nop
sumai x4,x0,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,32 | nop | nop
nop
nop
sumai x4,x0,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,36 | nop | nop
nop
nop
# __gf_analizar_sensor_reordenado_while_cond_0:
nop | cargai x4,x2,12 | nop | nop
nop
nop
nop | cargai x5,x2,8 | nop | nop
nop
nop
mrq x4,x4,x5 | nop | nop | nop
nop
nop
nop | nop | igualno x4,x0,96 | nop  # -> __rs_cerca_0
nop
nop
nop | nop | sye x0,3456 | nop  # -> __gf_analizar_sensor_reordenado_while_fin_0
nop
nop
# __rs_cerca_0:
nop | cargai x4,x2,12 | nop | nop
nop
nop
cizqi x4,x4,2 | nop | nop | nop
nop
nop
nop | cargai x5,x2,4 | nop | nop
nop
nop
suma x4,x4,x5 | nop | nop | nop
nop
nop
nop | cargai x4,x4,0 | nop | nop
nop
nop
nop | guardap x2,x4,20 | nop | nop
nop
nop
nop | cargai x4,x2,12 | nop | nop
nop
nop
sumai x5,x0,100 | nop | nop | nop
nop
nop
sumai x12,x4,0 | nop | nop | nop
nop
nop
sumai x13,x5,0 | nop | nop | nop
nop
nop
sumai x14,x0,0 | nop | nop | nop
nop
nop
sumai x5,x0,32 | nop | nop | nop
nop
nop
# __gb_mul_485_paso:
andi x15,x13,1 | nop | nop | nop
nop
nop
resta x15,x0,x15 | nop | nop | nop
nop
nop
and x15,x12,x15 | nop | nop | nop
nop
nop
suma x14,x14,x15 | nop | nop | nop
nop
nop
cizqi x12,x12,1 | nop | nop | nop
nop
nop
cderi x13,x13,1 | nop | nop | nop
nop
nop
restai x5,x5,1 | nop | nop | nop
nop
nop
nop | nop | igualno x5,x0,-336 | nop  # -> __gb_mul_485_paso
nop
nop
sumai x4,x14,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,24 | nop | nop
nop
nop
nop | cargai x4,x2,20 | nop | nop
nop
nop
sumai x5,x0,2 | nop | nop | nop
nop
nop
sumai x12,x4,0 | nop | nop | nop
nop
nop
sumai x13,x5,0 | nop | nop | nop
nop
nop
sumai x14,x0,0 | nop | nop | nop
nop
nop
sumai x5,x0,32 | nop | nop | nop
nop
nop
# __gb_mul_486_paso:
andi x15,x13,1 | nop | nop | nop
nop
nop
resta x15,x0,x15 | nop | nop | nop
nop
nop
and x15,x12,x15 | nop | nop | nop
nop
nop
suma x14,x14,x15 | nop | nop | nop
nop
nop
cizqi x12,x12,1 | nop | nop | nop
nop
nop
cderi x13,x13,1 | nop | nop | nop
nop
nop
restai x5,x5,1 | nop | nop | nop
nop
nop
nop | nop | igualno x5,x0,-336 | nop  # -> __gb_mul_486_paso
nop
nop
sumai x4,x14,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,28 | nop | nop
nop
nop
nop | cargai x4,x2,28 | nop | nop
nop
nop
sumai x5,x0,10 | nop | nop | nop
nop
nop
suma x4,x4,x5 | nop | nop | nop
nop
nop
nop | guardap x2,x4,32 | nop | nop
nop
nop
nop | cargai x4,x2,32 | nop | nop
nop
nop
sumai x5,x0,3 | nop | nop | nop
nop
nop
sumai x12,x4,0 | nop | nop | nop
nop
nop
sumai x13,x5,0 | nop | nop | nop
nop
nop
sumai x14,x0,0 | nop | nop | nop
nop
nop
sumai x5,x0,32 | nop | nop | nop
nop
nop
# __gb_mul_487_paso:
andi x15,x13,1 | nop | nop | nop
nop
nop
resta x15,x0,x15 | nop | nop | nop
nop
nop
and x15,x12,x15 | nop | nop | nop
nop
nop
suma x14,x14,x15 | nop | nop | nop
nop
nop
cizqi x12,x12,1 | nop | nop | nop
nop
nop
cderi x13,x13,1 | nop | nop | nop
nop
nop
restai x5,x5,1 | nop | nop | nop
nop
nop
nop | nop | igualno x5,x0,-336 | nop  # -> __gb_mul_487_paso
nop
nop
sumai x4,x14,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,36 | nop | nop
nop
nop
nop | cargai x4,x2,20 | nop | nop
nop
nop
sumai x5,x0,50 | nop | nop | nop
nop
nop
myq x4,x4,x5 | nop | nop | nop
nop
nop
nop | nop | igualsi x4,x0,240 | nop  # -> __gf_analizar_sensor_reordenado_else_1
nop
nop
nop | cargai x4,x2,16 | nop | nop
nop
nop
nop | cargai x5,x2,36 | nop | nop
nop
nop
suma x4,x4,x5 | nop | nop | nop
nop
nop
nop | guardap x2,x4,16 | nop | nop
nop
nop
# __gf_analizar_sensor_reordenado_else_1:
nop | cargai x4,x2,12 | nop | nop
nop
nop
sumai x5,x0,1 | nop | nop | nop
nop
nop
suma x4,x4,x5 | nop | nop | nop
nop
nop
nop | guardap x2,x4,12 | nop | nop
nop
nop
nop | nop | sye x0,-3600 | nop  # -> __gf_analizar_sensor_reordenado_while_cond_0
nop
nop
# __gf_analizar_sensor_reordenado_while_fin_0:
nop | cargai x4,x2,16 | nop | nop
nop
nop
sumai x8,x4,0 | nop | nop | nop
nop
nop
nop | cargai x1,x2,0 | nop | nop
nop
nop
sumai x2,x2,40 | nop | nop | nop
nop
nop
nop | nop | syer x0,x1,0 | nop
nop
nop
# __fn_main:
restai x2,x2,28 | nop | nop | nop
nop
nop
nop | guardap x2,x1,0 | nop | nop
nop
nop
sumai x4,x0,30 | nop | nop | nop
nop
nop
nop | guardap x2,x4,4 | nop | nop
nop
nop
sumai x4,x0,60 | nop | nop | nop
nop
nop
nop | guardap x2,x4,8 | nop | nop
nop
nop
sumai x4,x0,55 | nop | nop | nop
nop
nop
nop | guardap x2,x4,12 | nop | nop
nop
nop
sumai x4,x0,10 | nop | nop | nop
nop
nop
nop | guardap x2,x4,16 | nop | nop
nop
nop
sumai x4,x0,80 | nop | nop | nop
nop
nop
nop | guardap x2,x4,20 | nop | nop
nop
nop
sumai x4,x2,4 | nop | nop | nop
nop
nop
restai x2,x2,4 | nop | nop | nop
nop
nop
nop | guardap x2,x4,0 | nop | nop
nop
nop
sumai x4,x0,5 | nop | nop | nop
nop
nop
restai x2,x2,4 | nop | nop | nop
nop
nop
nop | guardap x2,x4,0 | nop | nop
nop
nop
nop | cargai x4,x2,4 | nop | nop
nop
nop
nop | cargai x5,x2,0 | nop | nop
nop
nop
sumai x13,x0,0 | nop | nop | nop
nop
nop
cizqi x13,x13,8 | nop | nop | nop
nop
nop
ori x13,x13,0 | nop | nop | nop
nop
nop
cizqi x13,x13,8 | nop | nop | nop
nop
nop
ori x13,x13,4 | nop | nop | nop
nop
nop
cizqi x13,x13,8 | nop | nop | nop
nop
nop
ori x13,x13,80 | nop | nop | nop
nop
nop
restai x2,x2,8 | nop | nop | nop
nop
nop
nop | guardap x2,x4,0 | nop | nop
nop
nop
nop | guardap x2,x5,4 | nop | nop
nop
nop
nop | cargai x8,x2,0 | nop | nop
nop
nop
nop | cargai x9,x2,4 | nop | nop
nop
nop
nop | nop | syer x1,x13,0 | nop
nop
nop
sumai x2,x2,8 | nop | nop | nop
nop
nop
sumai x2,x2,8 | nop | nop | nop
nop
nop
sumai x4,x8,0 | nop | nop | nop
nop
nop
nop | guardap x2,x4,24 | nop | nop
nop
nop
nop | cargai x1,x2,0 | nop | nop
nop
nop
sumai x2,x2,28 | nop | nop | nop
nop
nop
nop | nop | syer x0,x1,0 | nop
nop
nop
