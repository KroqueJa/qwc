CXX      = g++
CFLAGS   = -O3
TARGET   = wcl

UNAME := $(shell uname -m)
CXXFLAGS   = --std=c++17 -O3 -Iinclude

# Pick the SIMD counter that matches the host architecture. Pass SCALAR=1 to
# force the portable scalar implementation instead (e.g. `make SCALAR=1`).
ifdef SCALAR
  COUNTLINES = src/countlines_scalar.cpp
else ifneq (,$(filter $(UNAME),arm64 aarch64))
  COUNTLINES = src/countlines_neon.cpp
else ifneq (,$(filter $(UNAME),x86_64 amd64))
  COUNTLINES = src/countlines_avx2.cpp
  CXXFLAGS  += -mavx2
else
  COUNTLINES = src/countlines_scalar.cpp
endif

SRCS = src/main.cpp src/processfile.cpp $(COUNTLINES)

.PHONY: all clean gen-test-data bench

all: $(TARGET)
	@echo "Built $(TARGET) with $(COUNTLINES) on $(UNAME)"
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

$(C_TARGET): $(C_SRC)
	$(CC) $(CFLAGS) -o $(C_TARGET) $(C_SRC) -lpthread

clean:
	rm -f $(TARGET) $(C_TARGET)

gen-test-data:
	python3 benchmarks/gen-test-data.py

bench: $(TARGET)
	python3 benchmarks/bench.py

wcl-scalar: src/main.cpp src/processfile.cpp src/countlines_scalar.cpp
	$(CXX) --std=c++17 -O3 -Iinclude -o wcl-scalar src/main.cpp src/processfile.cpp src/countlines_scalar.cpp
	@echo "Built wcl-scalar with scalar countlines"