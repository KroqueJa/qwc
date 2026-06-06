#pragma once
#include "typedef.h"

// Counts occurrences of `target` in buffer. Defaults to '\n' (line counting).
usize countLines( const char* buffer, usize length, char target = '\n' );