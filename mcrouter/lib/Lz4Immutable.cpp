/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Lz4Immutable.h"

#include <type_traits>

#include <folly/Bits.h>

namespace facebook {
namespace memcache {

namespace {

constexpr size_t kHashUnit = sizeof(size_t);
constexpr size_t kMaxDictionarySize = 64 * 1024;

// Used for hasing / fill hash table
constexpr uint32_t kHashMask = (1 << kHashLog) - 1;
constexpr uint64_t kPrime5Bytes = 889523592379ULL;

// Min match size.
constexpr size_t kMinMatch = 4;
// Size of the copies.
constexpr size_t kCopyLength = 8;
// Size of the last literals.
constexpr size_t kLastLiterals = 5;
// We will look for matches until there is just this number of bytes remaining.
constexpr size_t kMatchFindLimit = kCopyLength + kMinMatch;
// Minimum input size this algorithm accepts.
constexpr size_t kMinInputSize = kMatchFindLimit + 1;
// Maximum input size this algorithm accepts.
constexpr uint32_t kMaxInputSize = 0x7E000000;

constexpr size_t kSkipTrigger = 6;
constexpr size_t kStepSize = sizeof(uint64_t);

// Output formating - Ml stands for Match Length.
constexpr size_t kMlBits = 4;
constexpr size_t kMlMask = (1U << kMlBits) - 1;
constexpr size_t kRunBits = 8 - kMlBits;
constexpr size_t kRunMask = (1U << kRunBits) - 1;

/**
 * Read "size" bytes from position "pos" of "buffer"
 * NOTE: Buffer can be chained, but computeChainDataLength() MUST
 * be at least: ("pos" + "size").
 *
 * @param dest    Destination buffer.
 * @param buffer  Where to read the data from.
 * @param pos     Position to read within the buffer.
 * @param size    Number of bytes to read.
 */
void readSlow(
    uint8_t* dest,
    const folly::IOBuf& buffer,
    size_t pos,
    size_t size) {
  const folly::IOBuf* cur = &buffer;

  // Skip previous buffers
  while (cur->length() <= pos) {
    pos -= cur->length();
    cur = cur->next();
  }

  size_t bytesRead = 0;
  while (bytesRead < size) {
    size_t toRead = std::min(size - bytesRead, cur->length() - pos);
    std::memcpy(dest, cur->data() + pos, toRead);
    dest += toRead;
    bytesRead += toRead;
    cur = cur->next();
    pos = 0;
  }
}

/**
 * Read sizeof(T) bytes from position "pos" of "buffer"
 * NOTE: Buffer can be chained, but computeChainDataLength() MUST
 * be at least: ("pos" + "size").
 *
 * @param buffer  Where to read the data from.
 * @param pos     Position to read within the buffer.
 */
template <class T>
T read(const folly::IOBuf& buffer, size_t pos) {
  static_assert(std::is_integral<T>::value, "Read requires an integral type");
  if (LIKELY(buffer.length() >= pos + sizeof(T))) {
    return folly::loadUnaligned<T>(buffer.data() + pos);
  }
  uint8_t buf[sizeof(T)];
  readSlow(buf, buffer, pos, sizeof(T));
  T val;
  memcpy(&val, buf, sizeof(T));
  return val;
}

uint32_t hashSequence(size_t sequence) {
  return ((sequence * kPrime5Bytes) >> (40 - kHashLog)) & kHashMask;
}

uint32_t hashPosition(const folly::IOBuf& source, size_t pos) {
  return hashSequence(read<uint64_t>(source, pos));
}

uint32_t getPositionOnHash(const Hashtable& table, uint32_t hash) {
  return table[hash];
}

void putPosition(Lz4ImmutableState& state, size_t pos) {
  uint32_t h = hashPosition(*state.dictionary, pos);
  state.table[h] = pos;
}

Lz4ImmutableState loadDictionary(std::unique_ptr<folly::IOBuf> dictionary) {
  size_t dicSize = dictionary->computeChainDataLength();
  CHECK_GE(dicSize, kHashUnit) << "Dictionary too small";
  CHECK_LE(dicSize, kMaxDictionarySize) << "Dictionary too big";

  Lz4ImmutableState state;
  state.dictionary = std::move(dictionary);
  state.table.fill(0);

  size_t pos = 0;
  while (pos <= dicSize - kHashUnit) {
    putPosition(state, pos);
    pos += 3;
  }

  return state;
}

/**
 * A customized version of std::memcpy that works with chained IOBufs.
 */
void safeCopy(
    uint8_t* dest,
    const folly::IOBuf& source,
    size_t pos,
    size_t count) {
  int64_t left = count;
  uint64_t src;
  do {
    size_t toWrite = std::min(8l, left);
    if (LIKELY(toWrite == sizeof(uint64_t))) {
      src = read<uint64_t>(source, pos);
    } else {
      readSlow(reinterpret_cast<uint8_t*>(&src), source, pos, toWrite);
    }
    std::memcpy(dest, &src, toWrite);
    dest += toWrite;
    pos += toWrite;
    left -= toWrite;
  } while (left > 0);
}

/**
 * A customized (faster) version of safeCopy that may overwrite
 * up to 7 bytes more than "count".
 */
void wildCopy(
    uint8_t* dest,
    const folly::IOBuf& source,
    size_t pos,
    size_t count) {
  const uint8_t* destEnd = dest + count;
  do {
    uint64_t src = read<uint64_t>(source, pos);
    std::memcpy(dest, &src, sizeof(uint64_t));
    dest += sizeof(uint64_t);
    pos += sizeof(uint64_t);
  } while (dest < destEnd);
}

void writeLE(void* dest, uint16_t val) {
  uint16_t valLE = folly::Endian::little(val);
  std::memcpy(dest, &valLE, sizeof(uint16_t));
}

uint16_t readLE(const folly::IOBuf& source, size_t pos) {
  uint16_t val = read<uint16_t>(source, pos);
  return folly::Endian::little(val);
}

/**
 * Given the difference between two number (i.e. num1 ^ num2), return the number
 * of leading bytes that are common in the two numbers.
 *
 * @param diff  Result of XOR between two numbers.
 * @return      The number of bytes that are common in the two numbers.
 */
size_t numCommonBytes(register size_t diff) {
#if (defined(__clang__) || ((__GNUC__ * 100 + __GNUC_MINOR__) >= 304))
  if (folly::Endian::order == folly::Endian::Order::LITTLE) {
    // __builtin_ctzll returns the number of trailling zeros in the binary
    // representation of "diff".
    return __builtin_ctzll(diff) >> 3; // we care about bytes (group of 8 bits).
  } else {
    // __builtin_ctzll returns the number of leading zeros in the binary
    // representation of "diff".
    return __builtin_clzll(diff) >> 3; // we care about bytes (group of 8 bits).
  }
#else
#error "Clang or GCC >= 304 required for Immutable Lz4 compression."
#endif
}

/**
 * Calculates the length of a match given a starting point.
 *
 * @param source      Source buffer.
 * @param pos         Starting index of the match inside source buffer.
 * @param dictionary  The dictionary.
 * @param match       Starting index of the match inside dictionary.
 * @param posLimit    Upper limit that points just past where
 *                    "pos" can go to find a match.
 *
 * @return            The size of the match, in bytes.
 */
size_t calculateMatchLength(
    const folly::IOBuf& source,
    size_t pos,
    const folly::IOBuf& dictionary,
    size_t match,
    size_t posLimit) {

  const size_t posStart = pos;

  while (LIKELY(pos < posLimit - kStepSize - 1)) {
    uint64_t diff =
        read<uint64_t>(dictionary, match) ^ read<uint64_t>(source, pos);
    if (!diff) {
      pos += kStepSize;
      match += kStepSize;
      continue;
    }
    pos += numCommonBytes(diff);
    return pos - posStart;
  }

  if ((pos < posLimit - 3) &&
      (read<uint32_t>(dictionary, match) == read<uint32_t>(source, pos))) {
    pos += 4;
    match += 4;
  }
  if ((pos < posLimit - 1) &&
      (read<uint16_t>(dictionary, match) == read<uint16_t>(source, pos))) {
    pos += 2;
    match += 2;
  }
  if ((pos < posLimit) &&
      (read<uint8_t>(dictionary, match) == read<uint8_t>(source, pos))) {
    ++pos;
  }
  return pos - posStart;
}

} // anonymous namespace

Lz4Immutable::Lz4Immutable(std::unique_ptr<folly::IOBuf> dictionary) noexcept
    : state_(loadDictionary(std::move(dictionary))) {}

size_t Lz4Immutable::compressBound(size_t size) const noexcept {
  return size + ((size / 255) + 16);
}

std::unique_ptr<folly::IOBuf> Lz4Immutable::compress(
    const folly::IOBuf& source) const noexcept {
  const size_t sourceSize = source.computeChainDataLength();
  CHECK_LE(sourceSize, kMaxInputSize) << "Data too large to compress!";

  const auto& dictionary = *state_.dictionary;
  size_t dictionarySize = dictionary.computeChainDataLength();
  size_t dictionaryDiff = kMaxDictionarySize - dictionarySize;
  // Upper limit of where we can look for a match.
  const size_t matchFindLimit = sourceSize - kMatchFindLimit;
  // Upper limit of where a match can go.
  const size_t matchLimit = sourceSize - kLastLiterals;
  const size_t maxOutputSize = compressBound(sourceSize);

  // Destination (compressed) buffer.
  auto destination = folly::IOBuf::create(maxOutputSize);
  // Pointer to where the next compressed position should be written.
  uint8_t* output = destination->writableTail();
  // Lower and upper limit to where the output buffer can go.
  const uint8_t* outputStart = output;
  const uint8_t* outputLimit = output + maxOutputSize;

  // Controls the compression main loop.
  bool running = true;

  // Next position (0..sourceSize] in source buffer that was not
  // yet written to destination buffer.
  size_t anchor = 0;
  // Next position (0..sourceSize] where compression is going to
  // working on source buffer.
  size_t pos = 0;
  uint32_t forwardHash;

  if (sourceSize < kMinInputSize) {
    // Not enough data to compress. Don't even enter the compress loop.
    running = false;
  } else {
    // Skip first byte.
    ++pos;
    forwardHash = hashPosition(source, pos);
  }

  // Main loop
  while (running) {
    // Position (0..sourceSize] of current match in source buffer.
    uint32_t match;
    // LZ4 token
    uint8_t* token;

    // Find a match
    {
      size_t forwardPos = pos;
      size_t step = 1;
      size_t searchMatchNb = 1 << kSkipTrigger;

      do {
        uint32_t hash = forwardHash;
        pos = forwardPos;
        forwardPos += step;
        step = (searchMatchNb++ >> kSkipTrigger);

        if (UNLIKELY(forwardPos > matchFindLimit) ||
            UNLIKELY(pos > kMaxDictionarySize)) {
          // Not enough data to compress, break compression main loop.
          running = false;
          break;
        }

        match = getPositionOnHash(state_.table, hash);

        forwardHash = hashPosition(source, forwardPos);
      } while (
          ((match + dictionaryDiff) <= pos) ||
          (read<uint32_t>(dictionary, match) != read<uint32_t>(source, pos)));

      if (!running) {
        break;
      }
    }

    // Catch up - try to expand the match backwards.
    while ((pos > anchor) && (match > 0) &&
           UNLIKELY(
               read<uint8_t>(source, pos - 1) ==
               read<uint8_t>(dictionary, match - 1))) {
      --pos;
      --match;
    }

    // Write literal
    {
      size_t literalLen = pos - anchor;
      token = output++;

      // Check output limit
      assert(
          output + literalLen + (2 + 1 + kLastLiterals) + (literalLen / 255) <=
          outputLimit);

      // Encode literal length
      if (literalLen >= kRunMask) {
        int len = static_cast<int>(literalLen - kRunMask);
        *token = (kRunMask << kMlBits);
        for (; len >= 255; len -= 255) {
          *output++ = 255;
        }
        *output++ = static_cast<uint8_t>(len);
      } else {
        *token = static_cast<uint8_t>(literalLen << kMlBits);
      }

      // Copy literals to output buffer.
      wildCopy(output, source, anchor, literalLen);
      output += literalLen;
    }

    // Encode offset
    uint16_t offset = dictionarySize - match + pos;
    writeLE(output, static_cast<uint16_t>(offset));
    output += 2;

    // Encode matchLength
    {
      // we cannot go past the dictionary
      size_t posLimit = pos + (dictionarySize - match);
      // Nor go past the source buffer
      posLimit = std::min(posLimit, matchLimit);

      size_t matchLen = calculateMatchLength(
          source, pos + kMinMatch, dictionary, match + kMinMatch, posLimit);
      pos += kMinMatch + matchLen;

      assert(output + (1 + kLastLiterals) + (matchLen >> 8) <= outputLimit);

      // Write match length.
      if (matchLen >= kMlMask) {
        *token += kMlMask;
        matchLen -= kMlMask;
        for (; matchLen >= 510; matchLen -= 510) {
          *output++ = 255;
          *output++ = 255;
        }
        if (matchLen >= 255) {
          matchLen -= 255;
          *output++ = 255;
        }
        *output++ = static_cast<uint8_t>(matchLen);
      } else {
        *token += static_cast<uint8_t>(matchLen);
      }
    }

    // Update anchor
    anchor = pos;

    // Test end of chunk
    if (pos > matchFindLimit) {
      break;
    }

    // Prepare for next loop
    forwardHash = hashPosition(source, +pos);
  }

  // Encode last literals
  {
    size_t lastRun = sourceSize - anchor;

    assert(
        (output - destination->data()) + lastRun + 1 +
            ((lastRun + 255 - kRunMask) / 255) <=
        maxOutputSize);

    if (lastRun >= kRunMask) {
      size_t accumulator = lastRun - kRunMask;
      *output++ = kRunMask << kMlBits;
      for (; accumulator >= 255; accumulator -= 255) {
        *output++ = 255;
      }
      *output++ = static_cast<uint8_t>(accumulator);
    } else {
      *output++ = static_cast<uint8_t>(lastRun << kMlBits);
    }
    safeCopy(output, source, anchor, lastRun);
    output += lastRun;
  }

  destination->append(output - outputStart);
  return destination;
}

std::unique_ptr<folly::IOBuf> Lz4Immutable::decompress(
    const folly::IOBuf& source,
    size_t uncompressedSize) const noexcept {

  const auto& dictionary = *state_.dictionary;
  size_t dictionarySize = dictionary.computeChainDataLength();

  // Destination (uncompressed) buffer.
  auto destination = folly::IOBuf::create(uncompressedSize);
  // Pointer to where the next uncompressed position should be written.
  uint8_t* output = destination->writableTail();
  // Lower and upper limit to where the output buffer can go.
  const uint8_t* outputStart = output;
  const uint8_t* outputLimit = output + uncompressedSize;

  size_t pos = 0;

  // Main loop
  while (true) {
    // LZ4 token
    size_t token = read<uint8_t>(source, pos++);

    // Get literal length
    size_t literalLength = token >> kMlBits;
    if (literalLength == kRunMask) {
      size_t s;
      do {
        s = read<uint8_t>(source, pos++);
        literalLength += s;
      } while (LIKELY(s == 255));
    }

    // Copy literals
    uint8_t* cpy = output + literalLength;
    if (cpy > outputLimit - kCopyLength) {
      if (cpy != outputLimit) {
        return nullptr;
      }
      safeCopy(output, source, pos, literalLength);
      pos += literalLength;
      output += literalLength;
      break; // Necessarily EOF, due to parsing restrictions
    }
    wildCopy(output, source, pos, literalLength);
    pos += literalLength;
    output = cpy;

    // Get match offset
    uint16_t offset = readLE(source, pos);
    size_t match = dictionarySize + (output - outputStart) - offset;
    pos += 2;

    // Get match length
    size_t matchLength = token & kMlMask;
    if (matchLength == kMlMask) {
      size_t s;
      do {
        s = read<uint8_t>(source, pos++);
        matchLength += s;
      } while (s == 255);
    }
    matchLength += kMinMatch;

    // Copy match
    safeCopy(output, dictionary, match, matchLength);
    output += matchLength;
  }

  destination->append(output - outputStart);
  return destination;
}

} // memcache
} // facebook
