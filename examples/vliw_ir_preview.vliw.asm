__vliw_b0:
restai x2,x2,4 | nop | nop | nop
sumai x28,x0,1 | nop | nop | nop
nop | nop | nop | nop
nop | guardap x2,x1,0 | nop | nop
nop | nop | igualsi x28,x0,__vliw_b2 | nop
__vliw_b1:
nop | nop | nop | nop
nop | nop | nop | nop
nop | nop | sye x0,__vliw_b2 | nop
__vliw_b2:
nop | nop | nop | nop
nop | nop | nop | nop
nop | cargai x1,x2,0 | nop | nop
sumai x2,x2,4 | nop | nop | nop
