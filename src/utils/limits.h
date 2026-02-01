#pragma once

#include <cstddef>
#include <cstdint>

namespace orpheus::limits {

// Memory read/write limits
constexpr size_t MAX_MEMORY_READ = 16 * 1024 * 1024;      // 16 MB
constexpr size_t MAX_MEMORY_SNAPSHOT = 16 * 1024 * 1024;  // 16 MB
constexpr size_t MAX_PATTERN_SCAN = 256 * 1024 * 1024;    // 256 MB

// Cache defaults
constexpr size_t DEFAULT_CACHE_PAGES = 1024;              // 4 MB of pages
constexpr uint32_t DEFAULT_CACHE_TTL_MS = 100;            // 100ms TTL

// Pattern scanner limits
constexpr size_t MAX_PATTERN_INPUT = 4096;                // Max pattern string length
constexpr size_t MAX_PATTERN_BYTES = 1024;                // Max pattern byte length

// String scan limits
constexpr size_t MAX_STRING_RESULTS = 10000;
constexpr size_t MIN_STRING_LENGTH = 4;

// RTTI scan limits
constexpr size_t MAX_RTTI_RESULTS = 10000;

// Function recovery limits
constexpr size_t MAX_FUNCTIONS = 100000;

// Disassembly limits
constexpr size_t DEFAULT_DISASM_COUNT = 20;
constexpr size_t MAX_DISASM_COUNT = 1000;

// Decompiler limits
constexpr size_t DEFAULT_MAX_INSTRUCTIONS = 100000;
constexpr size_t MAX_MAX_INSTRUCTIONS = 10000000;

} // namespace orpheus::limits
