/* Sonda de pruebas sobre top intacto: entradas antes del flanco, efectos
 * después de NBA. La inspección de bóveda es exclusiva de esta referencia. */
`timescale 1ns/1ps
module tb_sim_crypto_ref;
 logic clk=0, reset=1;
 top DUT(.clk(clk),.reset(reset),.debug_result(),.debug_rd(),.debug_write());
 logic [31:0] wb_pc=0,dir_a,dir_b;
 logic activo_a,activo_b;
 longint unsigned admitidos=0,completados=0,anulados=0;
 // Estas sondas no controlan el DUT. Una excepción anula su EX y el ID
 // admitido; IF descartado nunca incrementó admitidos. NOP sí es un bundle.
 always @(posedge clk) begin
  if(reset) begin wb_pc<=0;admitidos<=0;completados<=0;anulados<=0;end
  else begin
   wb_pc<=DUT.ex_pc;
   if(DUT.fetch_valid&&!DUT.redirect) admitidos<=admitidos+1;
   if(DUT.ex_wb_valid) completados<=completados+1;
   anulados<=anulados+(DUT.id_valid&&DUT.redirect)+DUT.crypto_fault;
  end
 end
 integer ciclos,reinicio=0,ignorado,local_c;
 logic [31:0] inicial[0:16383];
 string ruta,previo;
 integer fd,n,rot;
 logic [31:0] l,r,k;
 initial begin
  if($value$plusargs("VECTORES=%s",ruta)) begin
   fd=$fopen(ruta,"r");if(!fd)$fatal(1,"vectores inaccesibles");
   while(!$feof(fd)) begin
    n=$fscanf(fd,"%h %h %h %d\n",l,r,k,rot);
    if(n==4) $display("F %08h %08h %08h %08h %08h %08h",DUT.CRYPTO.rol32(l,rot),DUT.CRYPTO.feistel_f(r,k),r,l^DUT.CRYPTO.feistel_f(r,k),r^DUT.CRYPTO.feistel_f(l,k),l);
    else if(!$feof(fd))$fatal(1,"vector inválido");
   end
   $fclose(fd);
  end else begin
   if(!$value$plusargs("CICLOS=%d",ciclos))$fatal(1,"falta CICLOS");
   ignorado=$value$plusargs("RESET_DESPUES=%d",reinicio);
   // Se precarga la misma DMEM que usa S1 antes de ejecutar. Ninguna sonda
   // cambia el programa, las entradas de permiso ni el estado del procesador.
   #1;for(integer j=0;j<16384;j=j+1)inicial[j]=0;
   if($value$plusargs("DMEM=%s",ruta))$readmemh(ruta,inicial);
   for(integer j=0;j<16384;j=j+1)DUT.DMEM.mem[j]=inicial[j];
   #1;clk=1;#1;clk=0;#1;reset=0;#2;
   for(integer paso=0;paso<ciclos;paso=paso+1)begin
    // Reset publica las escrituras de memoria EX ya activas y después borra
    // los registros. El paso siguiente observa ese efecto sin realimentarlo.
    if(reinicio>0&&paso==reinicio)begin reset=1;#1;clk=1;#1;clk=0;#1;reset=0;#2;end
    local_c=paso+1-(reinicio>0&&paso>=reinicio?reinicio:0);
    dir_a=DUT.mem_addr;dir_b=DUT.crypto_mem_addr;
    activo_a=DUT.id_ex_valid&&DUT.ex_lsu_valid&&!DUT.crypto_fault;
    activo_b=DUT.id_ex_valid&&DUT.ex_crypto_valid&&!DUT.crypto_fault&&(DUT.ex_crypto_op==3||DUT.ex_crypto_op==4);
    // Se normalizan buses sin habilitación. Los operandos ID se omiten
    // con redirect porque ese bundle no se acepta; EX conserva su captura.
    // Las solicitudes WB describen los cinco puertos antes del flanco.
    previo=$sformatf("{\"c\":%0d,\"pc_pre\":%0d,\"estado_pre\":%0d,\"fault\":%0d,\"flush\":%0d,\"redirect\":%0d",local_c,DUT.pc,DUT.crypto_estado,DUT.crypto_fault,DUT.redirect,DUT.redirect?DUT.redirect_pc:0);
    previo={previo,$sformatf(",\"conflicto\":%0d,\"v0\":%0d,\"pc0\":%0d,\"v1\":%0d,\"pc1\":%0d,\"v2\":%0d",DUT.wb_conflict,DUT.fetch_valid,DUT.fetch_valid?DUT.pc:0,DUT.id_valid,DUT.id_valid?DUT.id_pc:0,DUT.id_ex_valid)};
    previo={previo,$sformatf(",\"pc2\":%0d,\"v3\":%0d,\"pc3\":%0d,\"kill_if\":%0d,\"kill_id\":%0d,\"id_alu_a\":%0d",DUT.id_ex_valid?DUT.ex_pc:0,DUT.ex_wb_valid,DUT.ex_wb_valid?wb_pc:0,DUT.redirect&&DUT.fetch_valid,DUT.redirect&&DUT.id_valid,(DUT.id_valid&&!DUT.redirect&&(1))?DUT.alu_rs1_data:0)};
    previo={previo,$sformatf(",\"id_alu_b\":%0d,\"id_lsu_a\":%0d,\"id_lsu_b\":%0d,\"id_bru_a\":%0d,\"id_bru_b\":%0d,\"id_crypto_a\":%0d",(DUT.id_valid&&!DUT.redirect&&(1))?(DUT.alu_use_imm?{{21{DUT.alu_imm[10]}},DUT.alu_imm}:DUT.alu_rs2_data):0,(DUT.id_valid&&!DUT.redirect&&(DUT.lsu_valid))?DUT.lsu_rs1_data:0,(DUT.id_valid&&!DUT.redirect&&(DUT.lsu_valid))?DUT.lsu_rs2_data:0,(DUT.id_valid&&!DUT.redirect&&(DUT.bru_is_branch||DUT.bru_is_jump))?DUT.bru_rs1_data:0,(DUT.id_valid&&!DUT.redirect&&(DUT.bru_is_branch||DUT.bru_is_jump))?DUT.bru_rs2_data:0,(DUT.id_valid&&!DUT.redirect&&(DUT.crypto_valid))?DUT.crypto_rs_a_data:0)};
    previo={previo,$sformatf(",\"id_crypto_b\":%0d,\"id_crypto_fault\":%0d,\"ex_crypto_a\":%0d,\"ex_crypto_b\":%0d,\"a_valid\":%0d,\"a_addr\":%0d",(DUT.id_valid&&!DUT.redirect&&(DUT.crypto_valid))?DUT.crypto_rs_b_data:0,DUT.id_valid&&!DUT.redirect&&DUT.crypto_fault_id,DUT.id_ex_valid&&DUT.ex_crypto_valid?DUT.ex_crypto_operand_a:0,DUT.id_ex_valid&&DUT.ex_crypto_valid?DUT.ex_crypto_operand_b:0,DUT.id_ex_valid&&DUT.ex_lsu_valid&&!DUT.crypto_fault,((DUT.id_ex_valid&&DUT.ex_lsu_valid&&!DUT.crypto_fault))?DUT.mem_addr:0)};
    previo={previo,$sformatf(",\"a_rd\":%0d,\"a_we\":%0d,\"a_wd\":%0d,\"b_valid\":%0d,\"b_addr\":%0d,\"b_rd\":%0d",((DUT.id_ex_valid&&DUT.ex_lsu_valid&&!DUT.crypto_fault))?DUT.mem_rdata:0,((DUT.id_ex_valid&&DUT.ex_lsu_valid&&!DUT.crypto_fault))?DUT.mem_we:0,((DUT.id_ex_valid&&DUT.ex_lsu_valid&&!DUT.crypto_fault)&&DUT.mem_we)?DUT.mem_wdata:0,DUT.id_ex_valid&&DUT.ex_crypto_valid&&!DUT.crypto_fault&&(DUT.ex_crypto_op==3||DUT.ex_crypto_op==4),((DUT.id_ex_valid&&DUT.ex_crypto_valid&&!DUT.crypto_fault&&(DUT.ex_crypto_op==3||DUT.ex_crypto_op==4)))?DUT.crypto_mem_addr:0,((DUT.id_ex_valid&&DUT.ex_crypto_valid&&!DUT.crypto_fault&&(DUT.ex_crypto_op==3||DUT.ex_crypto_op==4)))?DUT.crypto_mem_rdata:0)};
    previo={previo,$sformatf(",\"b_we\":%0d,\"b_wd\":%0d,\"w0\":%0d,\"w0_rd\":%0d,\"w0_data\":%0d,\"w1\":%0d",((DUT.id_ex_valid&&DUT.ex_crypto_valid&&!DUT.crypto_fault&&(DUT.ex_crypto_op==3||DUT.ex_crypto_op==4)))?DUT.crypto_mem_we:0,((DUT.id_ex_valid&&DUT.ex_crypto_valid&&!DUT.crypto_fault&&(DUT.ex_crypto_op==3||DUT.ex_crypto_op==4))&&DUT.crypto_mem_we)?DUT.crypto_mem_wdata:0,DUT.wb_we[0],DUT.wb_we[0]?DUT.wb_waddr[0]:0,DUT.wb_we[0]?DUT.wb_wdata[0]:0,DUT.wb_we[1])};
    previo={previo,$sformatf(",\"w1_rd\":%0d,\"w1_data\":%0d,\"w2\":%0d,\"w2_rd\":%0d,\"w2_data\":%0d,\"w3\":%0d",DUT.wb_we[1]?DUT.wb_waddr[1]:0,DUT.wb_we[1]?DUT.wb_wdata[1]:0,DUT.wb_we[2],DUT.wb_we[2]?DUT.wb_waddr[2]:0,DUT.wb_we[2]?DUT.wb_wdata[2]:0,DUT.wb_we[3])};
    previo={previo,$sformatf(",\"w3_rd\":%0d,\"w3_data\":%0d,\"w4\":%0d,\"w4_rd\":%0d,\"w4_data\":%0d",DUT.wb_we[3]?DUT.wb_waddr[3]:0,DUT.wb_we[3]?DUT.wb_wdata[3]:0,DUT.wb_we[4],DUT.wb_we[4]?DUT.wb_waddr[4]:0,DUT.wb_we[4]?DUT.wb_wdata[4]:0)};
    previo=$sformatf("%s,\"ex_alu_a\":%0d,\"ex_alu_b\":%0d,\"ex_lsu_a\":%0d,\"ex_lsu_b\":%0d,\"ex_bru_a\":%0d,\"ex_bru_b\":%0d,\"ex_alu_resultado\":%0d",previo,(DUT.id_ex_valid)?DUT.ex_operand_a:0,(DUT.id_ex_valid)?DUT.ex_operand_b:0,(DUT.id_ex_valid&&DUT.ex_lsu_valid)?DUT.ex_lsu_operand_a:0,(DUT.id_ex_valid&&DUT.ex_lsu_valid)?DUT.ex_lsu_operand_b:0,(DUT.id_ex_valid&&(DUT.ex_bru_is_branch||DUT.ex_bru_is_jump))?DUT.ex_bru_operand_a:0,(DUT.id_ex_valid&&(DUT.ex_bru_is_branch||DUT.ex_bru_is_jump))?DUT.ex_bru_operand_b:0,DUT.id_ex_valid&&!DUT.crypto_fault?DUT.alu_result:0);
    // Los selectores guardados siguen señalando las palabras del EX anterior
    // cuando, después de NBA, los buses ya pertenecen al siguiente bundle.
    #1;clk=1;#1;
    $write("%s",previo);
    $write(",\"pc\":%0d",DUT.pc);
$write(",\"estado\":%0d",DUT.crypto_estado);
$write(",\"trap_pc\":%0d",DUT.trap_pc);
$write(",\"traps\":%0d",DUT.trap_count);
$write(",\"a_post\":%0d",activo_a?DUT.DMEM.mem[dir_a[15:2]]:0);
$write(",\"b_post\":%0d",activo_b?DUT.DMEM.mem[dir_b[15:2]]:0);
$write(",\"par_l\":%0d",DUT.crypto_we_d?DUT.crypto_data_l_d:0);
$write(",\"par_r\":%0d",DUT.crypto_we_d?DUT.crypto_data_r_d:0);
$write(",\"par_rd\":%0d",DUT.crypto_we_d?DUT.crypto_rd_d:0);
$write(",\"auth_d\":%0d",DUT.CRYPTO.auth_we_d?DUT.CRYPTO.auth_d:0);
$write(",\"par\":%0d",DUT.crypto_we_d);
$write(",\"auth_we\":%0d",DUT.CRYPTO.auth_we_d);
$write(",\"init_clr\":%0d",DUT.CRYPTO.init_clr_d);
$write(",\"admitidos\":%0d",admitidos);
$write(",\"completados\":%0d",completados);
$write(",\"anulados\":%0d",anulados);
    $write(",\"regs\":[");for(integer j=0;j<32;j=j+1)$write("%s%0d",j?",":"",DUT.REGFILE.regs[j]);
    $write("],\"sonda_boveda\":[%0d,%0d",DUT.CRYPTO.VAULT.password,DUT.CRYPTO.VAULT.pwd_set);
    for(integer j=0;j<4;j=j+1)for(integer w=0;w<4;w=w+1)$write(",%0d",DUT.CRYPTO.VAULT.keys[j][w]);
    $write("]}\n");clk=0;#2;
   end
   for(integer j=0;j<16384;j=j+1)if(DUT.DMEM.mem[j]!==0)$display("{\"mem\":%0d,\"valor\":%0d}",j*4,DUT.DMEM.mem[j]);
  end
  $finish;
 end
endmodule
