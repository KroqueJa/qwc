CXX      = g++
CFLAGS   = -O3
TARGET   = wcl

UNAME := $(shell uname -m)
ifeq ($(UNAME), x86_64)
  CXXFLAGS   = --std=c++17 -msse4.1 -mavx2 -O3 -Iinclude
  COUNTLINES = src/countlines_avx2.cpp
else ifeq ($(UNAME), arm64)
  CXXFLAGS   = --std=c++17 -O3 -Iinclude
  COUNTLINES = src/countlines_neon.cpp
else
  CXXFLAGS   = --std=c++17 -O3 -Iinclude
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