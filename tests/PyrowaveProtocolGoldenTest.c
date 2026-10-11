#include "PyrowaveProtocol.h"
#include "DynamicHdr.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int headersEqual(const LI_PYROWAVE_PACKET_HEADER* left,
                        const LI_PYROWAVE_PACKET_HEADER* right) {
    return left->version == right->version &&
           left->packetKind == right->packetKind &&
           left->flags == right->flags &&
           left->reserved == right->reserved &&
           left->headerLength == right->headerLength &&
           left->metadataFlags == right->metadataFlags &&
           left->frameId == right->frameId &&
           left->rtpTimestamp == right->rtpTimestamp &&
           left->codecPayloadLength == right->codecPayloadLength &&
           left->protectedPayloadLength == right->protectedPayloadLength &&
           left->metadataLength == right->metadataLength &&
           left->fecScheme == right->fecScheme &&
           left->reserved2 == right->reserved2 &&
           left->dataBlockCount == right->dataBlockCount &&
           left->parityBlockCount == right->parityBlockCount &&
           left->fecBlockPayloadSize == right->fecBlockPayloadSize &&
           left->reserved3 == right->reserved3 &&
           left->blockIndex == right->blockIndex &&
           left->blockCount == right->blockCount &&
           left->fecGroupIndex == right->fecGroupIndex &&
           left->fecDataCount == right->fecDataCount &&
           left->fecParityCount == right->fecParityCount &&
           left->fecShardIndex == right->fecShardIndex &&
           left->reserved4 == right->reserved4 &&
           left->payloadLength == right->payloadLength &&
           left->reserved5 == right->reserved5 &&
           left->reserved6 == right->reserved6;
}

int main(void) {
    static const uint8_t payload[] = { 0x10, 0x20, 0x30, 0x40 };
    uint8_t packet[LI_PYROWAVE_MAX_PACKET_SIZE];
    size_t packetLength = 0;
    const uint8_t* parsedPayload = NULL;
    LI_PYROWAVE_PACKET_HEADER header = {
        .version = LI_PYROWAVE_PROTOCOL_VERSION,
        .packetKind = LI_PYROWAVE_PACKET_DATA,
        .flags = LI_PYROWAVE_FLAG_START_OF_FRAME | LI_PYROWAVE_FLAG_END_OF_FRAME |
                 LI_PYROWAVE_FLAG_CRITICAL,
        .headerLength = LI_PYROWAVE_WIRE_HEADER_SIZE,
        .frameId = 7,
        .rtpTimestamp = 9000,
        .codecPayloadLength = sizeof(payload),
        .protectedPayloadLength = sizeof(payload),
        .fecScheme = LI_PYROWAVE_FEC_SCHEME_NONE,
        .dataBlockCount = 1,
        .parityBlockCount = 0,
        .fecBlockPayloadSize = sizeof(payload),
        .blockIndex = 0,
        .blockCount = 1,
        .payloadLength = sizeof(payload),
    };
    LI_PYROWAVE_PACKET_HEADER parsed = { 0 };
    const LI_PYROWAVE_CAPABILITIES serverCapabilities = {
        .protocolVersion = LI_PYROWAVE_PROTOCOL_VERSION,
        .bitstreamVersion = LI_PYROWAVE_BITSTREAM_VERSION,
        .payloadVersion = LI_PYROWAVE_PAYLOAD_VERSION,
        .capabilityFlags = LI_PYROWAVE_CAPABILITY_REASSEMBLY |
                           LI_PYROWAVE_CAPABILITY_FRAME_DEADLINE |
                           LI_PYROWAVE_REQUIRED_CAPABILITIES,
        .maxPacketSize = LI_PYROWAVE_MAX_PACKET_SIZE,
    };
    const LI_PYROWAVE_CAPABILITIES clientCapabilities = {
        .protocolVersion = LI_PYROWAVE_PROTOCOL_VERSION,
        .bitstreamVersion = LI_PYROWAVE_BITSTREAM_VERSION,
        .payloadVersion = LI_PYROWAVE_PAYLOAD_VERSION,
        .capabilityFlags = LI_PYROWAVE_REQUIRED_CAPABILITIES,
        .maxPacketSize = 4096,
    };
    LI_PYROWAVE_CAPABILITIES negotiated = { 0 };

    {
        const struct {
            int mode;
            uint32_t caps;
            int preference;
            bool dynamic;
        } requests[] = {
            { 0, UINT32_MAX, 0, false },
            { 1, UINT32_MAX, 3, false },
            { 2, UINT32_MAX, 3, false },
            { 1, 0, 0, false },
            { 1, 1u << 31, 0, false },
            { 1, DYNAMIC_HDR_CAPS_VIVID_HLG | DYNAMIC_HDR_CAPS_DOLBY_VISION_84, 0, false },
            { 2, DYNAMIC_HDR_CAPS_HDR10_PLUS | DYNAMIC_HDR_CAPS_VIVID_PQ |
                 DYNAMIC_HDR_CAPS_DOLBY_VISION_81, 0, false },
            { 1, DYNAMIC_HDR_CAPS_HDR10_PLUS, 2, true },
            { 1, DYNAMIC_HDR_CAPS_VIVID_PQ, 2, false },
            { 2, UINT32_MAX, 2, false },
            { 1, DYNAMIC_HDR_CAPS_DOLBY_VISION_81, 1, true },
            { 2, DYNAMIC_HDR_CAPS_DOLBY_VISION_84, 1, true },
            { 1, DYNAMIC_HDR_CAPS_VIVID_PQ, 0, true },
            { 2, DYNAMIC_HDR_CAPS_VIVID_HLG, 0, true },
        };
        for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i) {
            CHECK(LiPyrowaveRequestsDynamicHdr(requests[i].mode, requests[i].caps,
                                              requests[i].preference) == requests[i].dynamic);
        }
        for (int format = 1; format <= 5; ++format) {
            const bool hlg = format == DYNAMIC_HDR_FORMAT_VIVID_HLG ||
                             format == DYNAMIC_HDR_FORMAT_DOLBY_VISION_PROFILE_84;
            CHECK(!LiPyrowaveDynamicHdrMatchesMode(format, 0));
            CHECK(LiPyrowaveDynamicHdrMatchesMode(format, 1) == !hlg);
            CHECK(LiPyrowaveDynamicHdrMatchesMode(format, 2) == hlg);
        }
        CHECK(LiPyrowaveDynamicHdrMatchesMode(DYNAMIC_HDR_FORMAT_NONE, 0));
        CHECK(LiPyrowaveDynamicHdrMatchesMode(DYNAMIC_HDR_FORMAT_NONE, 1));
        CHECK(LiPyrowaveDynamicHdrMatchesMode(DYNAMIC_HDR_FORMAT_NONE, 2));
        CHECK(!LiPyrowaveDynamicHdrMatchesMode(-1, 1));
        CHECK(!LiPyrowaveDynamicHdrMatchesMode(6, 1));
    }

    CHECK(LiPyrowaveValidateCapabilities(&serverCapabilities));
    CHECK(LiPyrowaveValidateCapabilities(&clientCapabilities));
    CHECK(LiPyrowaveNegotiate(
              &serverCapabilities,
              &clientCapabilities,
              LI_PYROWAVE_REQUIRED_CAPABILITIES,
              &negotiated) == LI_PYROWAVE_NEGOTIATION_OK);
    CHECK(negotiated.capabilityFlags ==
          LI_PYROWAVE_REQUIRED_CAPABILITIES);
    CHECK(negotiated.maxPacketSize == clientCapabilities.maxPacketSize);
    {
        LI_PYROWAVE_CAPABILITIES limitedServer = serverCapabilities;
        LI_PYROWAVE_CAPABILITIES limitedClient = clientCapabilities;
        const uint32_t limitedRequired = LI_PYROWAVE_REQUIRED_SDR_BASE_CAPABILITIES |
                                          LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE;
        limitedServer.capabilityFlags |= LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE;
        limitedClient.capabilityFlags = limitedRequired;
        CHECK(LiPyrowaveNegotiate(
                  &limitedServer,
                  &limitedClient,
                  limitedRequired,
                  &negotiated) == LI_PYROWAVE_NEGOTIATION_OK);

        LI_PYROWAVE_CAPABILITIES limitedHdrServer = serverCapabilities;
        LI_PYROWAVE_CAPABILITIES limitedHdrClient = clientCapabilities;
        const uint32_t limitedHdrRequired = LI_PYROWAVE_REQUIRED_HDR10_BASE_CAPABILITIES |
                                             LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE;
        limitedHdrServer.capabilityFlags |= LI_PYROWAVE_CAPABILITY_HDR10_PQ_BT2020 |
                                            LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE;
        limitedHdrClient.capabilityFlags = limitedHdrRequired;
        CHECK(LiPyrowaveNegotiate(
                  &limitedHdrServer,
                  &limitedHdrClient,
                  limitedHdrRequired,
                  &negotiated) == LI_PYROWAVE_NEGOTIATION_OK);

        LI_PYROWAVE_CAPABILITIES hdr10Server = serverCapabilities;
        LI_PYROWAVE_CAPABILITIES hdr10Client = clientCapabilities;
        hdr10Server.capabilityFlags |= LI_PYROWAVE_CAPABILITY_HDR10_PQ_BT2020;
        hdr10Client.capabilityFlags = LI_PYROWAVE_REQUIRED_HDR10_CAPABILITIES;
        CHECK(LiPyrowaveNegotiate(
                  &hdr10Server,
                  &hdr10Client,
                  LI_PYROWAVE_REQUIRED_HDR10_CAPABILITIES,
                  &negotiated) == LI_PYROWAVE_NEGOTIATION_OK);

        LI_PYROWAVE_CAPABILITIES hlgServer = serverCapabilities;
        LI_PYROWAVE_CAPABILITIES hlgClient = clientCapabilities;
        hlgServer.capabilityFlags |= LI_PYROWAVE_CAPABILITY_HLG_BT2020;
        hlgClient.capabilityFlags = LI_PYROWAVE_REQUIRED_HLG_CAPABILITIES;
        CHECK(LiPyrowaveNegotiate(
                  &hlgServer,
                  &hlgClient,
                  LI_PYROWAVE_REQUIRED_HLG_CAPABILITIES,
                  &negotiated) == LI_PYROWAVE_NEGOTIATION_OK);
    }
    {
        LI_PYROWAVE_CAPABILITIES noDeadline = clientCapabilities;
        noDeadline.capabilityFlags &= ~LI_PYROWAVE_CAPABILITY_FRAME_DEADLINE;
        CHECK(LiPyrowaveNegotiate(
                  &serverCapabilities,
                  &noDeadline,
                  LI_PYROWAVE_CAPABILITY_FRAME_DEADLINE,
                  &negotiated) == LI_PYROWAVE_NEGOTIATION_MISSING_CAPABILITY);
    }
    {
        LI_PYROWAVE_CAPABILITIES noColorContract = clientCapabilities;
        noColorContract.capabilityFlags = LI_PYROWAVE_CAPABILITY_REASSEMBLY;
        CHECK(LiPyrowaveNegotiate(
                  &serverCapabilities,
                  &noColorContract,
                  LI_PYROWAVE_REQUIRED_CAPABILITIES,
                  &negotiated) == LI_PYROWAVE_NEGOTIATION_MISSING_CAPABILITY);
    }
    {
        LI_PYROWAVE_CAPABILITIES noFullRangeContract = clientCapabilities;
        noFullRangeContract.capabilityFlags &= ~LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE;
        CHECK(LiPyrowaveNegotiate(
                  &serverCapabilities,
                  &noFullRangeContract,
                  LI_PYROWAVE_REQUIRED_CAPABILITIES,
                  &negotiated) == LI_PYROWAVE_NEGOTIATION_MISSING_CAPABILITY);
    }
    {
        LI_PYROWAVE_CAPABILITIES invalid = serverCapabilities;

        invalid.reserved = 1;
        CHECK(!LiPyrowaveValidateCapabilities(&invalid));
        invalid = serverCapabilities;
        invalid.maxPacketSize = LI_PYROWAVE_WIRE_HEADER_SIZE;
        CHECK(!LiPyrowaveValidateCapabilities(&invalid));
        invalid = clientCapabilities;
        invalid.protocolVersion++;
        CHECK(LiPyrowaveNegotiate(
                  &serverCapabilities, &invalid, 0, &negotiated) ==
              LI_PYROWAVE_NEGOTIATION_VERSION_MISMATCH);
        invalid = serverCapabilities;
        invalid.protocolVersion = UINT16_MAX;
        CHECK(LiPyrowaveNegotiate(
                  &invalid, &clientCapabilities, LI_PYROWAVE_REQUIRED_CAPABILITIES, &negotiated) ==
              LI_PYROWAVE_NEGOTIATION_VERSION_MISMATCH);
        invalid = serverCapabilities;
        invalid.protocolVersion = LI_PYROWAVE_PROTOCOL_VERSION + 1u;
        CHECK(!LiPyrowaveValidateCapabilities(&invalid));
        CHECK(LiPyrowaveNegotiate(
                  &invalid, &invalid, LI_PYROWAVE_REQUIRED_CAPABILITIES, &negotiated) ==
              LI_PYROWAVE_NEGOTIATION_VERSION_MISMATCH);
    }

    CHECK(LiPyrowaveValidatePacketHeader(&header));
    CHECK(LiPyrowaveBuildPacket(
              &header, payload, sizeof(payload), packet, sizeof(packet), &packetLength) ==
          LI_PYROWAVE_PACKET_OK);
    CHECK(packetLength == LI_PYROWAVE_WIRE_HEADER_SIZE + sizeof(payload));
    CHECK(LiPyrowaveParsePacket(packet, packetLength, &parsed, &parsedPayload) ==
          LI_PYROWAVE_PACKET_OK);
    CHECK(headersEqual(&header, &parsed));
    CHECK(parsedPayload == packet + LI_PYROWAVE_WIRE_HEADER_SIZE);
    CHECK(memcmp(parsedPayload, payload, sizeof(payload)) == 0);

    {
        LI_PYROWAVE_PACKET_HEADER fecHeader = header;
        fecHeader.packetKind = LI_PYROWAVE_PACKET_PARITY;
        fecHeader.flags = LI_PYROWAVE_FLAG_FEC_PARITY;
        fecHeader.headerLength = LI_PYROWAVE_WIRE_FEC_HEADER_SIZE;
        fecHeader.blockIndex = 2;
        fecHeader.blockCount = 3;
        fecHeader.dataBlockCount = 2;
        fecHeader.parityBlockCount = 1;
        fecHeader.codecPayloadLength = 8;
        fecHeader.protectedPayloadLength = 8;
        fecHeader.fecScheme = LI_PYROWAVE_FEC_SCHEME_XOR;
        fecHeader.fecGroupIndex = 0;
        fecHeader.fecDataCount = 2;
        fecHeader.fecParityCount = 1;
        fecHeader.fecShardIndex = 2;
        fecHeader.fecBlockPayloadSize = sizeof(payload);
        CHECK(LiPyrowaveBuildPacket(
                  &fecHeader, payload, sizeof(payload), packet, sizeof(packet), &packetLength) ==
              LI_PYROWAVE_PACKET_OK);
        CHECK(packetLength == LI_PYROWAVE_WIRE_FEC_HEADER_SIZE + sizeof(payload));
        CHECK(LiPyrowaveParsePacket(packet, packetLength, &parsed, &parsedPayload) ==
              LI_PYROWAVE_PACKET_OK);
        CHECK(headersEqual(&fecHeader, &parsed));
        CHECK(parsedPayload == packet + LI_PYROWAVE_WIRE_FEC_HEADER_SIZE);

        /* A data packet may not use the parity shard index.  Without this
           check it can address the parity slot as a data block in the
           reassembler. */
        fecHeader.packetKind = LI_PYROWAVE_PACKET_DATA;
        fecHeader.flags = 0;
        fecHeader.blockIndex = 2;
        fecHeader.fecShardIndex = fecHeader.fecDataCount;
        CHECK(!LiPyrowaveValidatePacketHeader(&fecHeader));

        /* The implementation currently has one XOR parity slot per group. */
        fecHeader.flags = LI_PYROWAVE_FLAG_FEC_PARITY;
        fecHeader.packetKind = LI_PYROWAVE_PACKET_PARITY;
        fecHeader.fecShardIndex = fecHeader.fecDataCount;
        fecHeader.fecParityCount = 2;
        CHECK(!LiPyrowaveValidatePacketHeader(&fecHeader));
    }

    packet[0] = 'X';
    CHECK(LiPyrowaveParsePacket(packet, packetLength, &parsed, &parsedPayload) ==
          LI_PYROWAVE_PACKET_BAD_MAGIC);
    packet[0] = 'P';

    header.packetKind = LI_PYROWAVE_PACKET_DATA;
    header.dataBlockCount = 2;
    header.blockCount = 2;
    header.codecPayloadLength = 8;
    header.protectedPayloadLength = 8;
    header.fecBlockPayloadSize = 4;
    header.blockIndex = 1;
    header.flags = LI_PYROWAVE_FLAG_END_OF_FRAME;
    CHECK(LiPyrowaveBuildPacket(
              &header, payload, sizeof(payload), packet, sizeof(packet), &packetLength) ==
          LI_PYROWAVE_PACKET_OK);
    CHECK(LiPyrowaveParsePacket(packet, packetLength, &parsed, &parsedPayload) ==
          LI_PYROWAVE_PACKET_OK);

    header.blockIndex = 0;
    header.flags = LI_PYROWAVE_FLAG_END_OF_FRAME;
    CHECK(!LiPyrowaveValidatePacketHeader(&header));
    CHECK(LiPyrowaveParsePacket(packet, LI_PYROWAVE_WIRE_HEADER_SIZE - 1, &parsed, &parsedPayload) ==
          LI_PYROWAVE_PACKET_TRUNCATED);

    packet[packetLength] = 0;
    CHECK(LiPyrowaveParsePacket(packet, packetLength + 1, &parsed, &parsedPayload) ==
          LI_PYROWAVE_PACKET_INVALID_HEADER);

    return 0;
}
