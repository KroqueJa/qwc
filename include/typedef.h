#pragma once
#include <cstddef>
#include <cstdint>

// Rust-style aliases for the numeric primitives -- easier on the eyes than the
// _t soup. Width-explicit integers map to the fixed-width stdint types; usize /
// isize are the pointer-sized counterparts (size_t / ptrdiff_t), matching how
// Rust uses them for sizes, offsets and indexing.
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using usize = std::size_t;
using isize = std::ptrdiff_t;

using f32 = float;
using f64 = double;
