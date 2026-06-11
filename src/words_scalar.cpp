#include "words.h"

#include "words_kernel.h"

// Scalar word counter -- the semantic reference for the SIMD kernels, and the
// words() driver for builds without one. A word is a maximal non-separator
// run containing at least one printable character, counted when it ends. All
// of the machinery -- the strict UTF-8 decoder, the classifiers, the
// per-code-point step and the owned-range walker -- lives in words_kernel.h,
// shared with words_avx2.cpp and words_neon.cpp; this TU just drives it over
// the owned region. See include/words.h and
// docs/superpowers/specs/2026-06-10-unicode-whitespace-words-design.md.

using namespace qwc::words_kernel;

void words( const char* buf, const usize len, const usize ownedBegin,
            const usize ownedEnd, WordScan& s, const WordsMode& m )
{
  const u8* base = reinterpret_cast<const u8*>( buf );

  if ( !m.utf8 ) {
    for ( usize i = ownedBegin; i < ownedEnd; ++i )
      step( classifyC( base[i] ), s );
    return;
  }

  scalarUtf8( base, len, ownedBegin, ownedEnd, s, m.nbspace );
}
