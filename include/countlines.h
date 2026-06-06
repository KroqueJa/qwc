#pragma once
#include <cstddef>

// Counts occurrences of `target` in buffer. Defaults to '\n' (line counting).
size_t countLines( const char* buffer, size_t length, char target = '\n' );