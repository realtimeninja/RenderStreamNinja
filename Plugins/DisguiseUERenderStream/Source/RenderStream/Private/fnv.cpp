// fnv.cpp
#include "fnv.hpp"
#include <algorithm>
#include <stdexcept>

// quick hash. 
// http://programmers.stackexchange.com/questions/49550/which-hashing-algorithm-is-best-for-uniqueness-and-speed

// For block padding see:
// https://en.wikipedia.org/wiki/Merkle%E2%80%93Damg%C3%A5rd_construction

static const uint64_t FNV_OFFSET_BIAS = 14695981039346656037ULL;
static const uint64_t FNV_PRIME = 1099511628211ULL;

uint64_t fnvHash(const uint8_t* buffer, size_t nBytes)
{
    uint64_t hash = FNV_OFFSET_BIAS;
    if (nBytes > 0)
    {
        const uint64_t* buf = (const uint64_t*)buffer;

        // All Complete blocks
        const size_t nUlongints = (nBytes + 7) / 8;
        for (size_t i = 0; i < nUlongints - 1; i++)
        {
            hash ^= *buf;
            hash *= FNV_PRIME;
            buf++;
        }

        // Last (potentially incomplete) block
        const int nBytesOver = nBytes & 7;
        const uint64_t masks[8] = { 0xffffffffffffffff, 0xff, 0xffff, 0xffffff, 0xffffffff, 0xffffffffff, 0xffffffffffff, 0xffffffffffffff };
        const uint64_t pads[8] = { 0x0000000000000000, 0x0100, 0x010000, 0x01000000, 0x0100000000, 0x010000000000, 0x01000000000000, 0x0100000000000000 };
        const uint64_t u = *buf & masks[nBytesOver] | pads[nBytesOver];
        hash ^= u;
        hash *= FNV_PRIME;

        // Add length as a final block to avoid length ambiguity
        hash ^= nBytes;
        hash *= FNV_PRIME;
    }
    return hash;
}

StreamFNV::StreamFNV()
{
    reset();
}

void StreamFNV::addData(const unsigned char* buffer, const size_t nBytes)
{
    if (!m_streamEnded)
    {
        const unsigned char* readPos = buffer;
        size_t bytesLeft = nBytes;
        m_length += nBytes;

        // Partial data still in our buffer?
        if (m_partialDataSize != 0)
        {
            // Read bytes from input stream
            const size_t bytesToFillPartialData = 8 - m_partialDataSize;
            const size_t bytesToCopy = std::min(bytesToFillPartialData, bytesLeft);
            uint64_t newData = 0;
            for (size_t i = 0; i < bytesToCopy; i++)
            {
                newData >>= 8;
                newData |= uint64_t(*readPos++) << 56;
            }
            newData >>= 8 * (bytesToFillPartialData - bytesToCopy);
            m_partialData |= newData;
            m_partialDataSize += bytesToCopy;
            bytesLeft -= bytesToCopy;

            // Got a whole piece of data now?
            if (m_partialDataSize == 8)
            {
                m_hash ^= m_partialData;
                m_hash *= FNV_PRIME;
                m_partialData = 0;
                m_partialDataSize = 0;
            }
        }

        // Copy as many 64bit sized quantities as we can
        if (bytesLeft >= 8)
        {
            const size_t nUlongints = (bytesLeft + 7) / 8 - 1;
            for (size_t i = 0; i < nUlongints; i++)
            {
                const uint64_t data = *(uint64_t*)readPos;
                m_hash ^= data;
                m_hash *= FNV_PRIME;
                readPos += 8;
            }
            bytesLeft -= nUlongints * 8;
        }

        // Finish off with any extra partial data
        if (bytesLeft)
        {
            uint64_t newData = 0;
            for (size_t i = 0; i < bytesLeft; i++)
            {
                newData >>= 8;
                newData |= uint64_t(*readPos++) << 56;
            }
            newData >>= 8 * (8 - bytesLeft);
            m_partialData = newData;
            m_partialDataSize = bytesLeft;
        }
    }
    else
    {
        throw std::runtime_error("Cannot add data after a result has been obtained");
    }
}

uint64_t StreamFNV::getHash()
{
    if (!m_streamEnded)
    {
        if (m_length > 0)
        {
            // Last (potentially incomplete) block
            if (m_partialDataSize)
            {
                const int nBytesOver = m_partialDataSize & 7;
                const uint64_t masks[8] = { 0xffffffffffffffff, 0xff, 0xffff, 0xffffff, 0xffffffff, 0xffffffffff, 0xffffffffffff, 0xffffffffffffff };
                const uint64_t pads[8] = { 0x0000000000000000, 0x0100, 0x010000, 0x01000000, 0x0100000000, 0x010000000000, 0x01000000000000, 0x0100000000000000 };
                const uint64_t data = m_partialData & masks[nBytesOver] | pads[nBytesOver];
                m_hash ^= data;
                m_hash *= FNV_PRIME;
            }

            // Add length as a final block to avoid length ambiguity
            m_hash ^= m_length;
            m_hash *= FNV_PRIME;
        }

        // Stream is now finished
        m_streamEnded = true;
    }

    return m_hash;
}


void StreamFNV::reset()
{
    m_partialData = 0;
    m_partialDataSize = 0;
    m_length = 0;
    m_hash = FNV_OFFSET_BIAS;
    m_streamEnded = false;
}
