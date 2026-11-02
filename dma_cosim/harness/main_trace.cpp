// =============================================================================
// main.cpp - Ko-simulacija: C++ ArenaAllocator + Verilog DMA kontroler (Verilator)
// -----------------------------------------------------------------------------
// Ovo je "harness" o kome smo pricali - obican C++ program koji:
//   1) napravi nas C++ alokator (PRAVA memorija u ovom programu),
//   2) alocira dva bloka - izvorni i odredisni,
//   3) upise test-podatke u izvorni blok,
//   4) zada komandu simuliranom DMA kontroleru (Verilator "prerusen" hardver),
//   5) vrti simulaciju takt-po-takt dok DMA ne javi da je gotov,
//   6) proveri da su podaci stvarno stigli tamo gde treba.
//
// Ovo je direktna, radna realizacija "Faze 3" iz naseg plana projekta.
// =============================================================================

#include "Vdma_controller.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include "arena_allocator.hpp"
#include "axi_lite_slave.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

using Slave = AxiLiteMemorySlave<Vdma_controller>;

// Jedan pun takt simulacije - identicno kao ranije, samo sada svaka faza
// (clk=0, clk=1, posle-komita) upisuje trenutno stanje SVIH signala u
// .vcd fajl preko tfp->dump(sim_time), da GTKWave moze da nacrta talasne
// oblike, takt po takt.
static void tick(Vdma_controller* top, Slave& slave,
                  VerilatedVcdC* tfp, vluint64_t& sim_time) {
    top->clk = 0;
    top->eval();
    if (tfp) tfp->dump(sim_time);
    ++sim_time;

    slave.compute_next(top);

    top->clk = 1;
    top->eval();
    if (tfp) tfp->dump(sim_time);
    ++sim_time;

    slave.commit();
    slave.drive_inputs(top);
    top->eval();
    if (tfp) tfp->dump(sim_time);
    ++sim_time;
}

int main(int argc, char** argv) {
    // ---- 1) Verilator kontekst + instanca naseg (bivseg VHDL) DMA kontrolera ----
    auto contextp = std::make_unique<VerilatedContext>();
    contextp->commandArgs(argc, argv);
    contextp->traceEverOn(true); // OBAVEZNO pre pravljenja modela, ako hocemo trace

    auto top = std::make_unique<Vdma_controller>(contextp.get());

    // Napravimo VCD "snimac" - ovo ce belezi svaki signal, svaki takt
    VerilatedVcdC* tfp = new VerilatedVcdC;
    top->trace(tfp, 99); // 99 = dubina - snimi i unutrasnje signale, ne samo portove
    tfp->open("dma_trace.vcd");
    vluint64_t sim_time = 0;

    // ---- 2) Nas C++ alokator - PRAVA memorija u ovom programu ----
    constexpr std::size_t ARENA_SIZE = 16384;
    mem::ArenaAllocator arena(ARENA_SIZE);

    constexpr int NUM_WORDS = 4;
    uint32_t* src = arena.allocate<uint32_t>(NUM_WORDS);
    uint32_t* dst = arena.allocate<uint32_t>(NUM_WORDS);

    if (src == nullptr || dst == nullptr) {
        std::fprintf(stderr, "Alokacija nije uspela!\n");
        return 1;
    }

    // Popunimo izvor prepoznatljivim vrednostima, odredisni blok neka bude nula
    for (int i = 0; i < NUM_WORDS; ++i) {
        src[i] = 0x12340000u + static_cast<uint32_t>(i);
    }
    std::memset(dst, 0, NUM_WORDS * sizeof(uint32_t));

    // Adrese koje saljemo DMA kontroleru su BAJT-OFSETI unutar arena bafera -
    // bas onako kako bi u realnom sistemu alokator "rekao gde" DMA treba da
    // cita/pise, samo ovde je "cela memorija" jedan C++ bafer.
    auto byte_offset = [&](void* p) -> uint32_t {
        return static_cast<uint32_t>(
            reinterpret_cast<std::byte*>(p) - arena.base());
    };
    uint32_t src_addr = byte_offset(src);
    uint32_t dst_addr = byte_offset(dst);

    // ---- 3) AXI4-Lite slave koji cita/pise BAS IZ arena bafera ----
    Slave slave(reinterpret_cast<uint8_t*>(arena.base()), ARENA_SIZE);

    // ---- 4) Reset ----
    top->rst_n       = 0;
    top->start       = 0;
    top->src_addr_in = 0;
    top->dst_addr_in = 0;
    top->length_in   = 0;
    slave.drive_inputs(top.get());
    for (int i = 0; i < 5; ++i) tick(top.get(), slave, tfp, sim_time);
    top->rst_n = 1;
    tick(top.get(), slave, tfp, sim_time);

    // ---- 5) CPU izdaje komandu DMA kontroleru ----
    top->src_addr_in = src_addr;
    top->dst_addr_in = dst_addr;
    top->length_in   = NUM_WORDS;
    top->start       = 1;
    tick(top.get(), slave, tfp, sim_time);
    top->start = 0;

    std::printf("Komanda poslata DMA-u (preko AXI4-Lite simulacije). Cekam 'done'...\n");

    // ---- 6) Vrti simulaciju dok DMA ne javi da je gotov ----
    bool finished = false;
    for (int cycle = 0; cycle < 20000; ++cycle) {
        tick(top.get(), slave, tfp, sim_time);
        if (top->done) {
            std::printf("DMA je javio 'done' posle %d takta.\n", cycle + 1);
            finished = true;
            break;
        }
    }

    if (!finished) {
        std::fprintf(stderr, "GRESKA: DMA nikad nije zavrsio prenos!\n");
        return 1;
    }

    // ---- 7) Provera: da li su podaci stvarno stigli tamo gde treba ----
    bool ok = (std::memcmp(src, dst, NUM_WORDS * sizeof(uint32_t)) == 0);

    if (ok) {
        std::printf(
            "USPEH: svih %d reci ispravno preneto kroz C++ alokator + "
            "Verilog DMA + Verilator ko-simulaciju.\n", NUM_WORDS);
    } else {
        std::printf("NEUSPEH: podaci na odredistu se ne poklapaju sa izvorom!\n");
        for (int i = 0; i < NUM_WORDS; ++i) {
            std::printf("  [%2d] src=0x%08x dst=0x%08x %s\n",
                        i, src[i], dst[i], (src[i] == dst[i] ? "OK" : "MISMATCH"));
        }
    }

    top->final();
    tfp->close();
    delete tfp;

    std::printf("\nWaveform sacuvan u dma_trace.vcd - otvori ga u GTKWave da vidis signale.\n");

    return ok ? 0 : 1;
}
