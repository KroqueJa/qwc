#pragma once
#include <cstddef>

size_t processFile( const char* filename, size_t bytesPerThread = 64 * 1024 * 1024,
                    char target = '\n' );