// //////////////////////////////////////////////////////////
// md5.h
// Copyright (c) 2014 Stephan Brumme. All rights reserved.
// see http://create.stephan-brumme.com/disclaimer.html
//

#pragma once

//#include "hash.h"
#include <string>

// define fixed size integer types
#ifdef _MSC_VER
// Windows
typedef unsigned __int8  uint8_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#else
// GCC
#include <stdint.h>
#endif


/// compute MD5 hash
/** Usage:
    MD5 md5;
    std::string myHash  = md5("Hello World");     // std::string
    std::string myHash2 = md5("How are you", 11); // arbitrary data, 11 bytes

    // or in a streaming fashion:

    MD5 md5;
    while (more data available)
      md5.add(pointer to fresh data, number of new bytes);
    std::string myHash3 = md5.getHash();
  */
class MD5 //: public Hash
{
public:
  /// split into 64 byte blocks (=> 512 bits), hash is 16 bytes long
  enum { BlockSize = 512 / 8, HashBytes = 16 };

  /// same as reset()
  MD5();

  /// compute MD5 of a memory block
  std::string operator()(const void* data, size_t numBytes);
  /// compute MD5 of a string, excluding final zero
  std::string operator()(const std::string& text);

  /// add arbitrary number of bytes
  void add(const void* data, size_t numBytes);

  /// return latest hash as 32 hex characters
  std::string getHash();
  /// return latest hash as bytes
  void        getHash(unsigned char buffer[HashBytes]);

  /// restart
  void reset();

private:
  /// process 64 bytes
  void processBlock(const void* data);
  /// process everything left in the internal buffer
  void processBuffer();

  /// size of processed data in bytes
  uint64_t m_numBytes;
  /// valid bytes in m_buffer
  size_t   m_bufferSize;
  /// bytes not processed yet
  uint8_t  m_buffer[BlockSize];

  enum { HashValues = HashBytes / 4 };
  /// hash, stored as integers
  uint32_t m_hash[HashValues];

  // mix functions for processBlock()
  inline uint32_t f1(uint32_t b, uint32_t c, uint32_t d)
  {
      return d ^ (b & (c ^ d)); // original: f = (b & c) | ((~b) & d);
  }

  inline uint32_t f2(uint32_t b, uint32_t c, uint32_t d)
  {
      return c ^ (d & (b ^ c)); // original: f = (b & d) | (c & (~d));
  }

  inline uint32_t f3(uint32_t b, uint32_t c, uint32_t d)
  {
      return b ^ c ^ d;
  }

  inline uint32_t f4(uint32_t b, uint32_t c, uint32_t d)
  {
      return c ^ (b | ~d);
  }

  inline uint32_t rotate(uint32_t a, uint32_t c)
  {
      return (a << c) | (a >> (32 - c));
  }

#if defined(__BYTE_ORDER) && (__BYTE_ORDER != 0) && (__BYTE_ORDER == __BIG_ENDIAN)
  inline uint32_t swap(uint32_t x)
  {
#if defined(__GNUC__) || defined(__clang__)
      return __builtin_bswap32(x);
#endif
#ifdef MSC_VER
      return _byteswap_ulong(x);
#endif

      return (x >> 24) |
          ((x >> 8) & 0x0000FF00) |
          ((x << 8) & 0x00FF0000) |
          (x << 24);
  }
#endif
};
