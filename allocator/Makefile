CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Iinclude

demo: src/main.cpp include/arena_allocator.hpp include/pool_allocator.hpp
	$(CXX) $(CXXFLAGS) src/main.cpp -o demo

run: demo
	./demo

clean:
	rm -f demo

.PHONY: run clean
