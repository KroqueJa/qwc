/*
 * Copyright Ji Krochmal 2026
 */
#pragma once
#include "typedef.h"

// Counts UTF-8 characters (code points) in buffer -- the `wc -m` equivalent in
// a multibyte (UTF-8) locale. A code point occupies one leading byte followed
// by zero or more continuation bytes (0x80-0xBF, i.e. 10xxxxxx), so the number
// of characters is the number of bytes that are NOT continuation bytes. Each
// byte is classified on its own, so -- unlike word counting -- chunk boundaries
// need no stitching: a multibyte sequence split across two chunks still has its
// sole leading byte counted exactly once, in whichever chunk holds it.
//
// On malformed input the count can diverge from a strict mbrtowc decode, but
// for well-formed UTF-8 it matches `wc -m` exactly. Under a single-byte locale
// a character is a byte, so the frontend counts bytes instead and never calls
// this.
usize chars( const char* buffer, usize length );
