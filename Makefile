CXX = g++
CXXFLAGS = --std=c++17 -msse4.1 -mavx2 -O3
TARGET = wcl
SRC = wcl.cpp

.PHONY: all clean gen-test-data bench

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

gen-test-data:
	python3 benchmarks/gen-test-data.py

bench: $(TARGET)
	python3 benchmarks/bench.py

