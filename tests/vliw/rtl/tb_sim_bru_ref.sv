/* Referencia BRU/PC y top originales. Captura buses antes del flanco y los
 * efectos después de NBA, incluidos flush, enlaces, DMEM y tres puertos WB. */
`timescale 1ns/1ps
module tb_sim_bru_ref;
    logic clk=0,reset=1;
    wire [31:0] debug_result;
    wire [4:0] debug_rd;
    wire debug_write;
    top DUT(.clk(clk),.reset(reset),.debug_result(debug_result),.debug_rd(debug_rd),.debug_write(debug_write));

    logic reset_ref=1,is_branch=0,is_jump=0;
    logic [31:0] a,b,contexto;
    logic [3:0] op;
    logic [10:0] br_imm;
    logic [15:0] jmp_imm;
    logic [4:0] rd;
    wire flush_ref,enlace_we;
    wire [31:0] target_ref,enlace,pc_ref;
    wire [4:0] enlace_rd;
    bru BRU_REF(.clk(clk),.reset(reset_ref),.rf1_data(a),.rf2_data(b),.pc_current(contexto),
        .bru_op(op),.is_branch(is_branch),.is_jump(is_jump),.br_imm(br_imm),.jmp_imm(jmp_imm),
        .link_rd_in(rd),.branch_flush(flush_ref),.target_pc(target_ref),.link_we_d(enlace_we),
        .link_rd_d(enlace_rd),.link_value_d(enlace));
    pc_branch PC_REF(.clk(clk),.reset(reset_ref),.branch_flush(flush_ref),.target_pc(target_ref),.pc_out(pc_ref));

    // Esta sonda sigue el PC de EX/WB sin realimentar el control. El RTL no
    // almacena PC dentro del registro de resultados de ALU ni del enlace BRU.
    logic [31:0] wb_pc=0;
    always @(posedge clk) if(reset) wb_pc<=0; else wb_pc<=DUT.ex_pc;
    longint unsigned admitidos=0,completados=0,anulados=0;
    always @(posedge clk) begin
        if(reset) begin admitidos<=0; completados<=0; anulados<=0; end
        else begin
            // La admisión excluye IF descartado por flush; WB válido sí completa aun en ese
            // flanco. ID válido anulado se cuenta por separado para contrastar el balance de
            // bundles con C.
            if(DUT.fetch_valid&&!DUT.branch_flush) admitidos<=admitidos+1;
            if(DUT.ex_wb_valid) completados<=completados+1;
            if(DUT.id_valid&&DUT.branch_flush) anulados<=anulados+1;
        end
    end
    logic [31:0] datos_iniciales[0:16383];
    integer archivo,n,indice,codigo,offset,reg_dest,reinicio;
    integer ciclos,reset_despues=0,ignorado;
    logic [31:0] dir_reset,antes_reset,dir_ex;
    string ruta,precarga,previo;

    // Publica los 32 GPR del DUT en el protocolo de la sonda; el llamador decide si observa
    // antes o después del flanco.
    task automatic imprimir_registros;
        // El recorrido incluye los 32 GPR para detectar escrituras indebidas fuera del destino
        // esperado. Esta tarea solo imprime: el instante de observación lo elige quien la llama.
        $write("\"regs\":[");
        for(integer r=0;r<32;r=r+1) $write("%s%0d",r?",":"",DUT.REGFILE.regs[r]);
        $write("]}\n");
    endtask

    // Seleccionamos vectores BRU aislados o un programa completo. Python consume la traza para
    // contrastar decisiones anteriores y efectos posteriores al flanco.
    initial begin
        if($value$plusargs("VECTORES=%s",ruta)) begin
            archivo=$fopen(ruta,"r");
            if(!archivo) $fatal(1,"no se puede abrir VECTORES");
            while(!$feof(archivo)) begin
                n=$fscanf(archivo,"%d %d %h %h %h %d %d %d\n",indice,codigo,contexto,a,b,offset,reg_dest,reinicio);
                if(n==8) begin
                    // Cada vector parte de PC_REF=0. Se contrasta su prioridad
                    // reset > redirect > incremento, además de la BRU aislada.
                    reset_ref=1; #1; clk=1; #1; clk=0;
                    op=codigo; rd=reg_dest; br_imm=offset; jmp_imm=offset;
                    is_branch=indice<4; is_jump=indice>=4; reset_ref=reinicio;
                    #2; previo=$sformatf("B %0d %08h",flush_ref,target_ref);
                    #1; clk=1; #1;
                    $display("%s %0d %0d %08h %08h",previo,enlace_we,enlace_rd,enlace,pc_ref);
                    clk=0; #1;
                end else if(!$feof(archivo)) $fatal(1,"vector BRU inválido");
            end
            $fclose(archivo);
        end else begin
            if(!$value$plusargs("CICLOS=%d",ciclos)) $fatal(1,"falta CICLOS");
            ignorado=$value$plusargs("RESET_DESPUES=%d",reset_despues);
            // La DMEM inicial se iguala explícitamente con la imagen C. Se
            // carga antes del primer flanco de ejecución, nunca implícitamente.
            #1;
            for(integer p=0;p<16384;p=p+1) datos_iniciales[p]=0;
            if($value$plusargs("DMEM=%s",precarga)) $readmemh(precarga,datos_iniciales);
            for(integer p=0;p<16384;p=p+1) DUT.DMEM.mem[p]=datos_iniciales[p];
            #1; clk=1; #1; clk=0; #1; reset=0; #2;
            for(integer c=1;c<=ciclos;c=c+1) begin
                if(reset_despues>0 && c==reset_despues+1) begin
                    // Reset domina el redirect y descarta enlaces; DMEM aún
                    // publica un store ya activo en EX, como se verificó en S4.
                    dir_reset=(DUT.id_ex_valid&&DUT.ex_lsu_valid)?DUT.mem_addr:0;
                    antes_reset=DUT.DMEM.mem[dir_reset[15:2]];
                    reset=1; #1; clk=1; #1; clk=0;
                    $write("{\"reset\":1,\"dir\":%0d,\"antes\":%0d,\"despues\":%0d,",dir_reset,antes_reset,DUT.DMEM.mem[dir_reset[15:2]]);
                    imprimir_registros(); reset=0; #2;
                end
                dir_ex=DUT.mem_addr;
                // El PC por etapa es el contexto anterior al flanco; la decisión
                // BRU se observa ahora, no después de invalidar sus entradas.
                previo=$sformatf("{\"c\":%0d,\"pc_pre\":%0d,\"stages\":[%0d,%0d,%0d,%0d],\"pcs\":[%0d,%0d,%0d,%0d],\"idalu\":[%0d,%0d],\"idlsu\":[%0d,%0d,%0d,%0d],\"idbru\":[%0d,%0d,%0d,%0d],\"exalu\":[%0d,%0d,%0d],\"x\":[%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d],\"w0\":[%0d,%0d,%0d],\"w1\":[%0d,%0d,%0d],\"w2\":[%0d,%0d,%0d],\"w1_valida\":%0d,\"w2_valida\":%0d,\"conflicto\":%0d,\"flush\":%0d,\"target\":%0d,\"kill\":[%0d,%0d],",
                    reset_despues>0&&c>reset_despues?c-reset_despues:c,DUT.pc,
                    DUT.fetch_valid,DUT.id_valid,DUT.id_ex_valid,DUT.ex_wb_valid,
                    DUT.pc,DUT.id_pc,DUT.ex_pc,wb_pc,
                    DUT.alu_rs1_data,DUT.alu_use_imm?{{21{DUT.alu_imm[10]}},DUT.alu_imm}:DUT.alu_rs2_data,
                    DUT.lsu_valid,DUT.lsu_rs1_data,DUT.lsu_rs2_data,$signed(DUT.lsu_imm),
                    DUT.bru_is_branch||DUT.bru_is_jump,DUT.bru_rs1_data,DUT.bru_rs2_data,
                    $signed(DUT.bru_is_jump&&DUT.bru_op==0?{{16{DUT.bru_jmp_imm[15]}},DUT.bru_jmp_imm}:{{21{DUT.bru_br_imm[10]}},DUT.bru_br_imm}),
                    DUT.ex_operand_a,DUT.ex_operand_b,DUT.alu_result,
                    DUT.id_ex_valid&&DUT.ex_lsu_valid,DUT.mem_addr,{16'b0,DUT.mem_addr[15:2],2'b0},DUT.mem_be,DUT.mem_wdata,DUT.mem_rdata,
                    DUT.ex_lsu_op==9?DUT.LSU.rdata_byte_sext:DUT.mem_rdata,DUT.mem_we,DUT.ex_lsu_op==8||DUT.ex_lsu_op==9,
                    DUT.wb_we[0],DUT.wb_waddr[0],DUT.wb_wdata[0],DUT.wb_we[1],DUT.wb_waddr[1],DUT.wb_wdata[1],
                    DUT.wb_we[2],DUT.wb_waddr[2],DUT.wb_wdata[2],DUT.LSU.load_pending_d,DUT.bru_link_we_d,
                    DUT.wb_conflict,DUT.branch_flush,DUT.bru_target_pc,DUT.branch_flush&&DUT.fetch_valid,DUT.branch_flush&&DUT.id_valid);
                #1; clk=1; #1;
                $write("%s\"pc\":%0d,\"postmem\":%0d,\"pending\":[%0d,%0d,%0d],\"link\":[%0d,%0d,%0d],\"next\":[%0d,%0d,%0d],\"next_pc\":[%0d,%0d,%0d],",previo,DUT.pc,
                    DUT.DMEM.mem[dir_ex[15:2]],DUT.LSU.load_pending_d,DUT.LSU.load_rd_d,DUT.LSU.load_data_d,
                    DUT.bru_link_we_d,DUT.bru_link_rd_d,DUT.bru_link_value_d,DUT.id_valid,DUT.id_ex_valid,DUT.ex_wb_valid,
                    DUT.id_pc,DUT.ex_pc,wb_pc);
                $write("\"cuentas\":[%0d,%0d,%0d],",admitidos,completados,anulados);
                imprimir_registros(); clk=0; #2;
            end
            // Comparación completa de DMEM final: las palabras omitidas son 0.
            for(integer p=0;p<16384;p=p+1)
                if(DUT.DMEM.mem[p]!==32'b0) $display("{\"mem\":%0d,\"valor\":%0d}",p*4,DUT.DMEM.mem[p]);
        end
        $finish;
    end
endmodule
