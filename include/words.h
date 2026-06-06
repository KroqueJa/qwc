#pragma once
#include "typedef.h"

// Counts whitespace-separated words in buffer -- the `wc -w` equivalent. A
// "word" is a maximal run of non-whitespace, so the result is the number of
// whitespace-to-word transitions. `inWord` carries the in-a-word state across
// successive buffers of one stream: initialise it to false before the first
// chunk and reuse the same variable for the rest, so a word straddling a buffer
// boundary is counted exactly once.
//
// Whitespace follows the C locale: space, tab, newline, vertical tab, form feed
// and carriage return. UTF-8 multibyte sequences never use these byte values,
// so byte-wise scanning still matches wc's C-locale behaviour on UTF-8 input.
usize words( const char* buffer, usize length, bool& inWord );