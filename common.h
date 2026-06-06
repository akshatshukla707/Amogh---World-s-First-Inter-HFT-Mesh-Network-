    #pragma once
#include <cstdint>
#include <net/ethernet.h>

// Custom EtherType — locally administered, no clash with real protocols
static constexpr uint16_t SCP_ETHERTYPE = 0x88B5;

// Our 88-byte wire frame — this IS the entire packet, no IP, no UDP
#pragma pack(push, 1)
struct SCPFrame {
    struct ether_header eth;   // 14 bytes: dst MAC + src MAC + ethertype
    uint64_t  prime_id;        //  8 bytes: synchronized prime tag
    uint8_t   msg_type;        //  1 byte:  1=REAL, 0=DECOY
    uint32_t  firm_id;         //  4 bytes: which firm sent this
    uint64_t  seq_num;         //  8 bytes: sequence counter
    char      payload[49];     // 49 bytes: actual data or garbage
    uint32_t  crc32;           //  4 bytes: integrity check
};
#pragma pack(pop)

static_assert(sizeof(SCPFrame) == 88, "SCPFrame must be exactly 88 bytes");

// Simple CRC32 for integrity check
inline uint32_t compute_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

// Shared secret for both nodes
static constexpr uint64_t SHARED_SECRET = 0xC0FFEE1337BABE42ULL;

// ⚡ OPTIMIZED LCG GENERATOR ⚡
// Replaces the heavy mt19937_64 for single-digit nanosecond execution
inline uint64_t generate_prime(uint64_t shared_secret, uint64_t seq) {
    uint64_t x = shared_secret ^ (seq * 0xDEADBEEFCAFEBABEULL);
    x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    return x;
}