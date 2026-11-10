/* Referencia LSU y top originales: compara buses preflanco, DMEM y registros
 * postflanco sin agregar depuración ni controles al procesador de Arquitectura. */
`timescale 1ns/1ps
module tb_sim_lsu_ref;
    logic clk=0, reset=1;
    wire [31:0] debug_result;
    wire [4:0] debug_rd;
    wire debug_write;
    top DUT(.clk(clk),.reset(reset),.debug_result(debug_result),.debug_rd(debug_rd),.debug_write(debug_write));

    logic valid_ref, reset_ref;
    logic [3:0] op;
    logic [4:0] rd;
    logic [10:0] imm;
    logic [31:0] base,dato;
    wire we,wb_we;
    wire [3:0] be;
    wire [31:0] addr,wdata,rdata,wb_data;
    wire [4:0] wb_rd;
    lsu LSU_REF(.clk(clk),.reset(reset_ref),.valid_in(valid_ref),.lsu_op(op),.rd(rd),.imm(imm),
        .rf1_data(base),.rf2_data(dato),.mem_we(we),.mem_be(be),.mem_addr(addr),
        .mem_wdata(wdata),.mem_rdata(rdata),.wb_we(wb_we),.wb_rd(wb_rd),.wb_data(wb_data));
    memory MEM_REF(.clk(clk),.we(we),.be(be),.addr(addr),.wdata(wdata),.rdata(rdata),
        .we_b(1'b0),.addr_b(32'b0),.wdata_b(32'b0),.rdata_b());

    // Sonda de PC que acompaña EX/WB; no controla ninguna entrada del DUT.
    logic [31:0] wb_pc=0;
    always @(posedge clk) if(reset) wb_pc<=0; else wb_pc<=DUT.ex_pc;
    logic [31:0] datos_iniciales[0:16383];
    integer archivo,n,indice,codigo,raw_imm,reg_dest,valid,reinicio;
    integer ciclos,reset_despues=0,ignorado;
    logic [31:0] inicial,dir_reset,antes_reset,dir_ex;
    string ruta,precarga,previo;
    bit restaurar=0;

    task automatic imprimir_registros;
        $write("\"regs\":[");
        for(integer r=0;r<32;r=r+1) $write("%s%0d",r?",":"",DUT.REGFILE.regs[r]);
        $write("]}\n");
    endtask

    initial begin
        if($value$plusargs("VECTORES=%s",ruta)) begin
            archivo=$fopen(ruta,"r");
            if(!archivo) $fatal(1,"no se puede abrir VECTORES");
            while(!$feof(archivo)) begin
                n=$fscanf(archivo,"%d %d %h %h %h %d %h %d %d\n",indice,codigo,base,raw_imm,dato,reg_dest,inicial,valid,reinicio);
                if(n==9) begin
                    op=codigo; imm=raw_imm; rd=reg_dest; valid_ref=valid; reset_ref=reinicio;
                    // Cada vector precarga la palabra seleccionada explícitamente.
                    // Esta preparación del fixture no es una escritura de la LSU.
                    #1; MEM_REF.mem[addr[15:2]]=inicial; #1;
                    previo=$sformatf("V %08h %0d %h %08h %08h",addr,we,be,wdata,rdata);
                    #1; clk=1; #1;
                    $display("%s %0d %0d %08h %08h",previo,wb_we,wb_rd,wb_data,MEM_REF.mem[addr[15:2]]);
                    clk=0; #1;
                end else if(!$feof(archivo)) $fatal(1,"vector inválido");
            end
            $fclose(archivo);
        end else begin
            if(!$value$plusargs("CICLOS=%d",ciclos)) $fatal(1,"falta CICLOS");
            ignorado=$value$plusargs("RESET_DESPUES=%d",reset_despues);
            restaurar=$test$plusargs("RESTAURAR");
            // Los datos BALB no aparecen automáticamente en RTL. Se carga el
            // mismo fixture de 16384 palabras que la imagen inicial del modelo C.
            #1;
            for(integer p=0;p<16384;p=p+1) datos_iniciales[p]=0;
            if($value$plusargs("DMEM=%s",precarga)) $readmemh(precarga,datos_iniciales);
            for(integer p=0;p<16384;p=p+1) DUT.DMEM.mem[p]=datos_iniciales[p];
            #1; clk=1; #1; clk=0; #1; reset=0; #2;
            for(integer c=1;c<=ciclos;c=c+1) begin
                if(reset_despues>0 && c==reset_despues+1) begin
                    // Store de EX sigue habilitado durante reset: memory no
                    // recibe reset. Se observa ese flanco antes de limpiar EX.
                    dir_reset=(DUT.id_ex_valid&&DUT.ex_lsu_valid)?DUT.mem_addr:0;
                    antes_reset=DUT.DMEM.mem[dir_reset[15:2]];
                    reset=1; #1; clk=1; #1; clk=0;
                    if(restaurar)
                        for(integer p=0;p<16384;p=p+1) DUT.DMEM.mem[p]=datos_iniciales[p];
                    $write("{\"reset\":1,\"dir\":%0d,\"antes\":%0d,\"despues\":%0d,",dir_reset,antes_reset,DUT.DMEM.mem[dir_reset[15:2]]);
                    imprimir_registros();
                    reset=0; #2;
                end
                dir_ex=DUT.mem_addr;
                // Todo lo combinacional se conserva antes del flanco. WB y
                // memoria posteriores se imprimen solo tras resolver las NBA.
                previo=$sformatf("{\"c\":%0d,\"pc_pre\":%0d,\"stages\":[%0d,%0d,%0d,%0d],\"pcs\":[%0d,%0d,%0d,%0d],\"idalu\":[%0d,%0d],\"idlsu\":[%0d,%0d,%0d,%0d],\"exalu\":[%0d,%0d,%0d],\"x\":[%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d],\"w0\":[%0d,%0d,%0d],\"w1\":[%0d,%0d,%0d],\"conflicto\":%0d,\"w1_valida\":%0d,",
                    reset_despues>0&&c>reset_despues?c-reset_despues:c,DUT.pc,
                    DUT.fetch_valid,DUT.id_valid,DUT.id_ex_valid,DUT.ex_wb_valid,
                    DUT.pc,DUT.id_pc,DUT.ex_pc,wb_pc,
                    DUT.alu_rs1_data,DUT.alu_use_imm?{{21{DUT.alu_imm[10]}},DUT.alu_imm}:DUT.alu_rs2_data,
                    DUT.lsu_valid,DUT.lsu_rs1_data,DUT.lsu_rs2_data,$signed(DUT.lsu_imm),
                    DUT.ex_operand_a,DUT.ex_operand_b,DUT.alu_result,
                    DUT.id_ex_valid&&DUT.ex_lsu_valid,DUT.mem_addr,{16'b0,DUT.mem_addr[15:2],2'b0},DUT.mem_be,DUT.mem_wdata,DUT.mem_rdata,
                    DUT.ex_lsu_op==9?DUT.LSU.rdata_byte_sext:DUT.mem_rdata,DUT.mem_we,
                    DUT.ex_lsu_op==8||DUT.ex_lsu_op==9,
                    DUT.wb_we[0],DUT.wb_waddr[0],DUT.wb_wdata[0],
                    DUT.wb_we[1],DUT.wb_waddr[1],DUT.wb_wdata[1],DUT.wb_conflict,DUT.LSU.load_pending_d);
                #1; clk=1; #1;
                $write("%s\"pc\":%0d,\"postmem\":%0d,\"pending\":[%0d,%0d,%0d],",previo,DUT.pc,
                    DUT.DMEM.mem[dir_ex[15:2]],DUT.LSU.load_pending_d,DUT.LSU.load_rd_d,DUT.LSU.load_data_d);
                imprimir_registros();
                clk=0; #2;
            end
            // El volcado disperso representa TODA DMEM: lo omitido debe ser
            // cero. Python compara el estado completo, además de cada acceso.
            for(integer p=0;p<16384;p=p+1)
                if(DUT.DMEM.mem[p]!==32'b0) $display("{\"mem\":%0d,\"valor\":%0d}",p*4,DUT.DMEM.mem[p]);
        end
        $finish;
    end
endmodule
