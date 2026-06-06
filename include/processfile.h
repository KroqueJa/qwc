#pragma once
#include "typedef.h"

usize processFile( const char* filename, usize bytesPerThread = 64 * 1024 * 1024,
                   char target = '\n' );