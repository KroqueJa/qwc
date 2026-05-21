CXX      = g++
CC       = gcc
CFLAGS   = -O3
C_TARGET = wcl-c
C_SRC    = wcl.c
TARGET   = wcl
SRCS     = src/main.cpp src/countlines.cpp src/processfile.cpp

# Platform-specific SIMD flags
UNAME := $(shell uname -m)
ifeq ($(UNAME), x86_64)
  CXXFLAGS = --std=c++17 -msse4.1 -mavx2 -O3 -Iinclude
else
  CXXFLAGS = --std=c++17 -O3 -Iinclude
endif

.PHONY: all clean gen-test-data bench c

all: $(TARGET)

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

c: $(C_TARGET)