#pragma once
// =============================================================================
// AxiLiteMemorySlave
// -----------------------------------------------------------------------------
// Ovo je C++ "prevod" ponasanja koje smo imali u VHDL testbench-u
// (axi_slave proces) - ali sad, umesto da simulira izmisljen memorijski niz,
// ova klasa cita/pise BAS IZ bafera koji nas ArenaAllocator upravlja.
//
// Ovo je bukvalno "most" o kome smo pricali: DMA kontroler (simuliran
// hardver) misli da razgovara sa pravom memorijom preko AXI4 magistrale,
// a u stvarnosti razgovara sa ovom klasom, koja transparentno prosledjuje
// citanja/pisanja u C++ bafer koji je nas alokator rezervisao.
//
// Vazna napomena o tajmingu (ISTO pravilo koje smo naucili u VHDL fazi):
// registri ove klase se azuriraju u DVA koraka - prvo compute_next()
// izracuna sta TREBA da bude sledece stanje (citajuci trenutne, stabilne
// izlaze DUT-a), pa tek commit() to "izvrsi". Ovo verno oponasa kako se
// pravi hardverski registri ponasaju na ivici takta - svi se azuriraju
// "istovremeno", koristeci vrednosti od PRE ivice, ne posle.
// =============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>

template <typename Top>
class AxiLiteMemorySlave {
public:
    AxiLiteMemorySlave(uint8_t* base, std::size_t size_bytes)
        : base_(base), size_(size_bytes) {}

    // Poziva se DOK je clk=0 (kombinaciona faza) - cita TRENUTNE, stabilne
    // izlaze DMA kontrolera (arvalid, awvalid, wdata...) i izracunava kakvo
    // TREBA da bude sledece stanje slave-a. Jos nista ne primenjuje.
    void compute_next(Top* top) {
        // ---- kanal citanja: da li DMA trazi da procita neku adresu? ----
        next_rvalid_ = rvalid_;
        next_rdata_  = rdata_;
        next_rresp_  = rresp_;

        if (top->m_axi_arvalid && kAlwaysReady) {
            // DMA je poslao adresu, mi (memorija) smo "uvek spremni" da je
            // prihvatimo - odmah pripremimo podatak za sledeci takt.
            uint32_t addr = top->m_axi_araddr;
            next_rdata_  = read_word(addr);
            next_rresp_  = 0; // AXI OKAY
            next_rvalid_ = 1;
        } else if (rvalid_ && top->m_axi_rready) {
            // DMA je preuzeo podatak (rready=1 dok mi drzimo rvalid=1) -
            // spustamo rvalid, transakcija citanja je zavrsena.
            next_rvalid_ = 0;
        }

        // ---- kanal pisanja: da li DMA trazi da upise na neku adresu? ----
        next_wready_ = wready_;
        if (top->m_axi_awvalid && kAlwaysReady) {
            next_wready_ = 1; // najavljujemo da smo spremni da primimo podatak
        }

        next_bvalid_ = bvalid_;
        next_bresp_  = bresp_;

        if (top->m_axi_wvalid && wready_) {
            // DMA nam salje stvarni podatak - upisujemo ga PRAVO U ARENU.
            uint32_t addr = top->m_axi_awaddr;
            write_word(addr, top->m_axi_wdata);
            next_wready_ = 0; // ovo namerno "pregazi" prethodnu liniju iznad,
                              // isto kao redosled naredbi u VHDL procesu
            next_bvalid_ = 1;
            next_bresp_  = 0; // AXI OKAY
        } else if (bvalid_ && top->m_axi_bready) {
            next_bvalid_ = 0;
        }
    }

    // "Izvrsi klok ivicu" za slave - primeni izracunato sledece stanje.
    void commit() {
        rvalid_ = next_rvalid_;
        rdata_  = next_rdata_;
        rresp_  = next_rresp_;
        wready_ = next_wready_;
        bvalid_ = next_bvalid_;
        bresp_  = next_bresp_;
    }

    // Postavi (vec komitovane) izlaze slave-a na ulazne portove DMA kontrolera.
    void drive_inputs(Top* top) const {
        top->m_axi_arready = kAlwaysReady;
        top->m_axi_awready = kAlwaysReady;
        top->m_axi_rvalid  = rvalid_;
        top->m_axi_rdata   = rdata_;
        top->m_axi_rresp   = rresp_;
        top->m_axi_wready  = wready_;
        top->m_axi_bvalid  = bvalid_;
        top->m_axi_bresp   = bresp_;
    }

private:
    static constexpr uint8_t kAlwaysReady = 1;

    uint32_t read_word(uint32_t addr) const {
        uint32_t v = 0;
        std::memcpy(&v, base_ + addr, sizeof(v));
        return v;
    }
    void write_word(uint32_t addr, uint32_t v) {
        std::memcpy(base_ + addr, &v, sizeof(v));
    }

    uint8_t* base_;
    std::size_t size_;

    uint8_t  rvalid_ = 0, next_rvalid_ = 0;
    uint32_t rdata_  = 0, next_rdata_  = 0;
    uint8_t  rresp_  = 0, next_rresp_  = 0;
    uint8_t  wready_ = 0, next_wready_ = 0;
    uint8_t  bvalid_ = 0, next_bvalid_ = 0;
    uint8_t  bresp_  = 0, next_bresp_  = 0;
};
