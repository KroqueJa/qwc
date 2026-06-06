#pragma once
#include "typedef.h"

// Counts occurrences of `target` in buffer. Defaults to '\n' (line counting).
usize count( const char* buffer, usize length, char target = '\n' );