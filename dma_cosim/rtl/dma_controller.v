// =============================================================================
// dma_controller.v
// -----------------------------------------------------------------------------
// Verilog "prevod" naseg vec proverenog VHDL DMA kontrolera. Identicna FSM
// logika, identicni AXI4-Lite Master signali - Verilator ovo pretvara u C++
// klasu koju cemo pozivati iz naseg harness-a.
// =============================================================================

module dma_controller #(
    parameter ADDR_WIDTH = 32,
    parameter DATA_WIDTH = 32
) (
    input  wire clk,
    input  wire rst_n,          // aktivan na 0

    // ---- Kontrolni interfejs (od CPU-a) ----
    input  wire                    start,
    input  wire [ADDR_WIDTH-1:0]   src_addr_in,
    input  wire [ADDR_WIDTH-1:0]   dst_addr_in,
    input  wire [31:0]             length_in,

    // ---- Statusni interfejs (ka CPU-u) ----
    output reg  busy,
    output reg  done,           // puls, 1 takt, kad se prenos zavrsi
    output reg  error,

    // ---- AXI4-Lite Master: citanje adrese ----
    output reg  [ADDR_WIDTH-1:0]   m_axi_araddr,
    output reg                     m_axi_arvalid,
    input  wire                    m_axi_arready,

    // ---- AXI4-Lite Master: citanje podataka ----
    input  wire [DATA_WIDTH-1:0]   m_axi_rdata,
    input  wire                    m_axi_rvalid,
    output reg                     m_axi_rready,
    input  wire [1:0]              m_axi_rresp,

    // ---- AXI4-Lite Master: adresa pisanja ----
    output reg  [ADDR_WIDTH-1:0]   m_axi_awaddr,
    output reg                     m_axi_awvalid,
    input  wire                    m_axi_awready,

    // ---- AXI4-Lite Master: podaci pisanja ----
    output reg  [DATA_WIDTH-1:0]   m_axi_wdata,
    output reg                     m_axi_wvalid,
    input  wire                    m_axi_wready,

    // ---- AXI4-Lite Master: odgovor na pisanje ----
    input  wire                    m_axi_bvalid,
    output reg                     m_axi_bready,
    input  wire [1:0]              m_axi_bresp
);

    localparam [1:0] AXI_OKAY = 2'b00;

    // Stanja FSM-a - identicna onima iz VHDL verzije
    localparam IDLE   = 4'd0,
               SETUP  = 4'd1,
               RADDR  = 4'd2,
               RDATA  = 4'd3,
               WADDR  = 4'd4,
               WDATA  = 4'd5,
               WRESP  = 4'd6,
               NEXTW  = 4'd7,
               DONE_S = 4'd8,
               ERROR_S= 4'd9;

    reg [3:0] state, state_next;

    // Interni registri - isti kao u VHDL: pamte napredak posla
    reg [ADDR_WIDTH-1:0] src_reg, dst_reg;
    reg [31:0]           words_left;
    reg [DATA_WIDTH-1:0] data_hold;

    // -------------------------------------------------------------------
    // Proces 1: registar stanja
    // -------------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            state <= IDLE;
        else
            state <= state_next;
    end

    // -------------------------------------------------------------------
    // Proces 2: kombinaciona logika sledeceg stanja
    // -------------------------------------------------------------------
    always @(*) begin
        state_next = state;
        case (state)
            IDLE: begin
                if (start)
                    state_next = SETUP;
            end

            SETUP: begin
                // Bitno (ista lekcija koju smo naucili u VHDL fazi): koristimo
                // length_in direktno, NE words_left, jer se words_left tek
                // ovog takta puni iz length_in (proces 3).
                if (length_in == 32'd0)
                    state_next = DONE_S;
                else
                    state_next = RADDR;
            end

            RADDR: if (m_axi_arready) state_next = RDATA;

            RDATA: begin
                if (m_axi_rvalid) begin
                    if (m_axi_rresp != AXI_OKAY)
                        state_next = ERROR_S;
                    else
                        state_next = WADDR;
                end
            end

            WADDR: if (m_axi_awready) state_next = WDATA;

            WDATA: if (m_axi_wready) state_next = WRESP;

            WRESP: begin
                if (m_axi_bvalid) begin
                    if (m_axi_bresp != AXI_OKAY)
                        state_next = ERROR_S;
                    else
                        state_next = NEXTW;
                end
            end

            NEXTW: begin
                if (words_left == 32'd0)
                    state_next = DONE_S;
                else
                    state_next = RADDR;
            end

            DONE_S:  state_next = IDLE;
            ERROR_S: state_next = IDLE;

            default: state_next = IDLE;
        endcase
    end

    // -------------------------------------------------------------------
    // Proces 3: registri sa podacima (generator adresa)
    // -------------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            src_reg    <= {ADDR_WIDTH{1'b0}};
            dst_reg    <= {ADDR_WIDTH{1'b0}};
            words_left <= 32'd0;
            data_hold  <= {DATA_WIDTH{1'b0}};
        end else begin
            case (state)
                SETUP: begin
                    src_reg    <= src_addr_in;
                    dst_reg    <= dst_addr_in;
                    words_left <= length_in;
                end

                RDATA: begin
                    if (m_axi_rvalid && m_axi_rresp == AXI_OKAY)
                        data_hold <= m_axi_rdata;
                end

                WRESP: begin
                    if (m_axi_bvalid && m_axi_bresp == AXI_OKAY) begin
                        src_reg    <= src_reg + (DATA_WIDTH/8);
                        dst_reg    <= dst_reg + (DATA_WIDTH/8);
                        words_left <= words_left - 32'd1;
                    end
                end

                default: ; // ostali registri se ne menjaju
            endcase
        end
    end

    // -------------------------------------------------------------------
    // Proces 4: izlazni AXI4-Lite signali i status - kombinaciona logika
    // -------------------------------------------------------------------
    always @(*) begin
        m_axi_araddr  = src_reg;
        m_axi_arvalid = 1'b0;
        m_axi_rready  = 1'b0;

        m_axi_awaddr  = dst_reg;
        m_axi_awvalid = 1'b0;

        m_axi_wdata   = data_hold;
        m_axi_wvalid  = 1'b0;

        m_axi_bready  = 1'b0;

        busy  = 1'b1;
        done  = 1'b0;
        error = 1'b0;

        case (state)
            IDLE:  busy = 1'b0;
            RADDR: m_axi_arvalid = 1'b1;
            RDATA: m_axi_rready  = 1'b1;
            WADDR: m_axi_awvalid = 1'b1;
            WDATA: m_axi_wvalid  = 1'b1;
            WRESP: m_axi_bready  = 1'b1;
            DONE_S: begin
                busy = 1'b0;
                done = 1'b1;
            end
            ERROR_S: begin
                busy  = 1'b0;
                error = 1'b1;
            end
            default: ;
        endcase
    end

endmodule
