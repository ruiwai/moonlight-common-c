#pragma once

#include <stddef.h>
#include <stdint.h>

// NV control stream packet header for TCP
typedef struct _NVCTL_TCP_PACKET_HEADER {
    unsigned short type;
    unsigned short payloadLength;
} NVCTL_TCP_PACKET_HEADER, *PNVCTL_TCP_PACKET_HEADER;

typedef struct _NVCTL_ENET_PACKET_HEADER_V1 {
    unsigned short type;
} NVCTL_ENET_PACKET_HEADER_V1, *PNVCTL_ENET_PACKET_HEADER_V1;

typedef struct _NVCTL_ENET_PACKET_HEADER_V2 {
    unsigned short type;
    unsigned short payloadLength;
} NVCTL_ENET_PACKET_HEADER_V2, *PNVCTL_ENET_PACKET_HEADER_V2;

#define AES_GCM_TAG_LENGTH 16
typedef struct _NVCTL_ENCRYPTED_PACKET_HEADER {
    unsigned short encryptedHeaderType; // Always LE 0x0001
    unsigned short length; // sizeof(seq) + 16 byte tag + secondary header and data
    unsigned int seq; // Monotonically increasing sequence number (used as IV for AES-GCM)

    // encrypted NVCTL_ENET_PACKET_HEADER_V2 and payload data follow
} NVCTL_ENCRYPTED_PACKET_HEADER, *PNVCTL_ENCRYPTED_PACKET_HEADER;

// Returns zero if the payload and encryption overhead cannot fit the wire length.
static inline unsigned short getEncryptedControlPacketLength(int payloadLength) {
    const size_t overhead = sizeof(((PNVCTL_ENCRYPTED_PACKET_HEADER)0)->seq) +
                            AES_GCM_TAG_LENGTH + sizeof(NVCTL_ENET_PACKET_HEADER_V2);

    if (payloadLength < 0 || (size_t)payloadLength > UINT16_MAX - overhead) {
        return 0;
    }

    return (unsigned short)(overhead + (size_t)payloadLength);
}
