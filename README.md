# DMA Memory Subsystem

School/personal project - tried to build a mini version of how AI accelerators move data around without bothering the CPU for every copy.

Basically two parts that talk to each other:
- a custom memory allocator in C++ (no `malloc`, does its own thing, O(1))
- a DMA controller I wrote in VHDL and then ported to Verilog so it could run with Verilator

Verilator turns the Verilog into a C++ class, and then I hooked it up so the "hardware" reads/writes straight into the same memory buffer the allocator manages. So it's not just two separate demos, they actually run together.

## Folders

```
allocator/    just the C++ allocator, no hardware stuff
dma_vhdl/     DMA controller in VHDL + GHDL testbench
dma_cosim/    Verilog version of the DMA + Verilator co-sim with the allocator
```

## Running it

Need g++, verilator, ghdl (apt install all three).

# allocator only
cd allocator && make run

# VHDL testbench
cd dma_vhdl && bash run_sim.sh

# the actual co-simulation
cd dma_cosim
verilator --cc --exe --build -j 0 -CFLAGS "-std=c++20 -I$(pwd)/harness/include" \
  --top-module dma_controller rtl/dma_controller.v harness/main.cpp --Mdir obj_dir
./obj_dir/Vdma_controller

Tested with 1000 words, took ~6 cycles per word which lines up with how the FSM is designed (read + write + handshakes = 6 steps per word).

Also did a quick benchmark comparing CPU time if it copies manually vs just telling the DMA to do it - manual copy time grows with size, DMA command time stays flat (~40ns no matter the size) since the CPU just writes a few registers and moves on. That's the whole point basically.

## Bug worth mentioning

Spent a while debugging why the DMA always finished instantly no matter what - turned out I was checking `words_left == 0` in the state where `words_left` gets loaded, so it was reading the old value (0) before the load actually happened. Fixed by checking the input signal directly instead of the not-yet-updated register.

## TODO

- real AXI4 bursts instead of the simplified one-word-at-a-time version
- error handling tests
- maybe try on an actual FPGA at some point
