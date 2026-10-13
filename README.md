# DMA Memory Subsystem

A small project I made to understand how AI accelerators and SoCs move data around without making the CPU copy everything by hand.

It has two parts that work together:
- a custom memory allocator in C++ (arena + pool, no `malloc`, O(1) allocation, 64-byte aligned)
- a DMA controller that I wrote in VHDL, then ported to Verilog so I could run it with Verilator

In the co-simulation, Verilator turns the Verilog DMA into a C++ class, and I connected it so the "hardware" reads and writes directly into the memory that my allocator handed out. So the two parts actually run together and not just separately.

## Folders

```
allocator/    the C++ allocator on its own, no hardware
dma_vhdl/     DMA controller in VHDL + testbench (GHDL)
dma_cosim/    Verilog version of the DMA + Verilator co-simulation with the allocator
```

## What you need

```bash
sudo apt install g++ make verilator ghdl
```

I used Ubuntu on WSL. Verilator doesn't really work natively on Windows.

## How to run

Allocator (tests + a quick benchmark against malloc):
```bash
cd allocator
make run
```

VHDL testbench:
```bash
cd dma_vhdl
bash run_sim.sh
```

Co-simulation (allocator + DMA, 1000 words):
```bash
cd dma_cosim
bash build_and_run.sh
```

Benchmark (CPU copying by hand vs just sending a command to the DMA):
```bash
cd dma_cosim
verilator --cc --exe --build -j 0 -CFLAGS "-std=c++20 -O2 -I$(pwd)/harness/include" \
  --top-module dma_controller rtl/dma_controller.v harness/benchmark.cpp --Mdir obj_dir_bench
./obj_dir_bench/Vdma_controller
```

Waveform (needs gtkwave to open it):
```bash
cd dma_cosim
verilator --cc --exe --build -j 0 --trace -CFLAGS "-std=c++20 -I$(pwd)/harness/include" \
  --top-module dma_controller rtl/dma_controller.v harness/main_trace.cpp --Mdir obj_dir_trace
cd obj_dir_trace && ./Vdma_controller
gtkwave dma_trace.vcd
```

## Results

The 1000 word transfer finishes in 6001 cycles, which is 6 per word. That matches the FSM (address, data and response handshakes for read and write).

In the benchmark, copying by hand takes longer the bigger the transfer is, but the CPU side of a DMA transfer stays at around 40 ns no matter how much data moves. This makes sense since the CPU only writes a few registers and the DMA does the rest. The numbers for very small sizes are noisy, so only the larger ones are meaningful.

## Notes

- The bus is AXI4-Lite style (one word per transaction), not full AXI4 with bursts.
- Verilator only reads Verilog, so the VHDL version is the one I tested first with GHDL and the Verilog one is a port of it with the same logic.
- Bug I lost some time on: in the SETUP state I checked `words_left == 0`, but that register only gets loaded in that same state, so it still had the old value and the DMA thought every transfer was empty. Checking `length_in` directly fixed it.

## TODO

- real AXI4 bursts
- tests for error responses
- maybe try it on an FPGA
