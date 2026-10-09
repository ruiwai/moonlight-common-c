#include "ControlStreamPacket.h"

#include <limits.h>
#include <stdio.h>

static int failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while (0)

int main(void) {
    CHECK(sizeof(NVCTL_TCP_PACKET_HEADER) == 4);
    CHECK(sizeof(NVCTL_ENET_PACKET_HEADER_V1) == 2);
    CHECK(sizeof(NVCTL_ENET_PACKET_HEADER_V2) == 4);
    CHECK(sizeof(NVCTL_ENCRYPTED_PACKET_HEADER) == 8);

    CHECK(getEncryptedControlPacketLength(0) == 24);
    CHECK(getEncryptedControlPacketLength(1) == 25);
    CHECK(getEncryptedControlPacketLength(240) == 264);
    CHECK(getEncryptedControlPacketLength(241) == 265);
    CHECK(getEncryptedControlPacketLength(32767) == 32791);
    CHECK(getEncryptedControlPacketLength(32768) == 32792);
    CHECK(getEncryptedControlPacketLength(65500) == 65524);
    CHECK(getEncryptedControlPacketLength(65511) == UINT16_MAX);

    CHECK(getEncryptedControlPacketLength(65512) == 0);
    CHECK(getEncryptedControlPacketLength(65535) == 0);
    CHECK(getEncryptedControlPacketLength(65536) == 0);
    CHECK(getEncryptedControlPacketLength(-1) == 0);
    CHECK(getEncryptedControlPacketLength(INT_MIN) == 0);
    CHECK(getEncryptedControlPacketLength(INT_MAX) == 0);

    if (failures != 0) {
        fprintf(stderr, "%d control packet length checks failed\n", failures);
        return 1;
    }

    puts("control packet length checks passed");
    return 0;
}
