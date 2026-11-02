// =============================================================================
// benchmark.cpp - Faza 4: merenje "koliko je CPU zauzet"
// -----------------------------------------------------------------------------
// Ovo NE meri fizicku brzinu prenosa podataka (ta je ista bez obzira ko
// kopira). Ovo meri koliko vremena CPU (softver) mora da "utrosi", u dva
// scenarija, za rastuce velicine prenosa:
//
//   A) RUCNO KOPIRANJE  - CPU sam, u petlji, kopira svaku rec - vreme raste
//                          linearno sa brojem reci (CPU je "zarobljen").
//   B) DMA KOMANDA      - CPU samo upise 3 registra (src, dst, duzina) i
//                          "start" bit - to je UVEK isti, mali broj operacija,
//                          bez obzira da li prenosimo 16 ili 16 miliona reci.
//                          Sam prenos se desava u hardveru, NEZAVISNO od CPU-a
//                          (to vreme ovde namerno ne dodajemo CPU "racunu").
//
// Dodatno, informativno, prijavljujemo i koliko HARDVERSKIH taktova sam DMA
// treba da zavrsi prenos - ali to vreme tece "u pozadini", paralelno sa CPU
// radom, i NE oduzima CPU vreme - zato ga ne sabiramo sa CPU merenjem.
// =============================================================================

#include "Vdma_controller.h"
#include "verilated.h"

#include "arena_allocator.hpp"
#include "axi_lite_slave.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <vector>

using Slave = AxiLiteMemorySlave<Vdma_controller>;
using Clock = std::chrono::steady_clock;

static void tick(Vdma_controller* top, Slave& slave) {
    top->clk = 0;
    top->eval();
    slave.compute_next(top);
    top->clk = 1;
    top->eval();
    slave.commit();
    slave.drive_inputs(top);
    top->eval();
}

// Vraca broj hardverskih taktova koje je DMA potrosio da zavrsi prenos.
static int run_dma_transfer(Vdma_controller* top, Slave& slave,
                             uint32_t src_addr, uint32_t dst_addr, int num_words) {
    top->rst_n = 0;
    top->start = 0;
    tick(top, slave);
    top->rst_n = 1;
    tick(top, slave);

    top->src_addr_in = src_addr;
    top->dst_addr_in = dst_addr;
    top->length_in   = static_cast<uint32_t>(num_words);
    top->start = 1;
    tick(top, slave);
    top->start = 0;

    int cycles = 0;
    while (cycles < 200000) {
        tick(top, slave);
        ++cycles;
        if (top->done) break;
    }
    return cycles;
}

int main(int argc, char** argv) {
    auto contextp = std::make_unique<VerilatedContext>();
    contextp->commandArgs(argc, argv);
    auto top = std::make_unique<Vdma_controller>(contextp.get());

    // Testiramo rastuce velicine prenosa
    std::vector<int> sizes = {16, 64, 256, 1024, 4096, 16384};

    constexpr std::size_t ARENA_SIZE = 200000; // dovoljno za najveci test + oba bafera
    mem::ArenaAllocator arena(ARENA_SIZE);
    Slave slave(reinterpret_cast<uint8_t*>(arena.base()), ARENA_SIZE);

    std::printf("%10s | %18s | %22s | %20s\n",
                "N (reci)", "Rucno kopiranje", "DMA komanda (CPU)", "DMA hardver (takt.)");
    std::printf("-----------|--------------------|------------------------|---------------------\n");

    for (int n : sizes) {
        arena.reset(); // isprazni arenu pre svakog testa (kao sto smo objasnili - O(1))

        uint32_t* src = arena.allocate<uint32_t>(n);
        uint32_t* dst = arena.allocate<uint32_t>(n);
        for (int i = 0; i < n; ++i) src[i] = static_cast<uint32_t>(i);

        // ---- A) Rucno kopiranje (CPU sam radi posao) ----
        auto t0 = Clock::now();
        for (int i = 0; i < n; ++i) {
            dst[i] = src[i];
        }
        auto t1 = Clock::now();
        double manual_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();

        // Ocisti dst pre DMA testa, da ne "varamo" (dst mora stvarno da se napuni DMA-om)
        std::memset(dst, 0, n * sizeof(uint32_t));

        // ---- B) CPU deo DMA puta: SAMO upis komande (registri + start bit) ----
        uint32_t src_addr = static_cast<uint32_t>(
            reinterpret_cast<std::byte*>(src) - arena.base());
        uint32_t dst_addr = static_cast<uint32_t>(
            reinterpret_cast<std::byte*>(dst) - arena.base());

        auto t2 = Clock::now();
        top->src_addr_in = src_addr;
        top->dst_addr_in = dst_addr;
        top->length_in   = static_cast<uint32_t>(n);
        top->start       = 1; // <- ovo je SVE sto CPU mora da uradi
        auto t3 = Clock::now();
        double dma_cmd_ns = std::chrono::duration<double, std::nano>(t3 - t2).count();
        top->start = 0;

        // ---- Hardverski deo (informativno - NE racuna se u CPU vreme) ----
        int hw_cycles = run_dma_transfer(top.get(), slave, src_addr, dst_addr, n);

        bool ok = (std::memcmp(src, dst, n * sizeof(uint32_t)) == 0);
        if (!ok) {
            std::fprintf(stderr, "GRESKA kod N=%d: podaci se ne poklapaju!\n", n);
            return 1;
        }

        std::printf("%10d | %15.1f ns | %20.1f ns | %18d takt.\n",
                    n, manual_ns, dma_cmd_ns, hw_cycles);
    }

    std::printf("\nSvi testovi ispravni. CPU vreme za DMA komandu ostaje priblizno\n"
                "konstantno bez obzira na N, dok rucno kopiranje raste linearno.\n");

    top->final();
    return 0;
}
