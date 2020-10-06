#pragma once

#include <cinttypes>

// FNC functions
uint64_t      fnvHash(const uint8_t* buffer, size_t nBytes);         // quick 64-bit hash on any-sized buffer
class StreamFNV
{
public:
    StreamFNV();
    void addData(const unsigned char* buffer, const size_t nBytes);         // Add a chunk of data to the hash. Chunks can be arbitrarily size
    uint64_t getHash();                                                     // Get the hash. Once you have the hash you cannot add more data
    void reset();                                                           // Reset back to initial state
private:
    uint64_t m_partialData;
    size_t m_partialDataSize;
    uint64_t m_length;
    uint64_t m_hash;

    bool m_streamEnded;
};
