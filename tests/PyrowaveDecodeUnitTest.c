// Exercise the real owned-frame/queued DECODE_UNIT path without a network session.
#define notifyKeyFrameReceived testNotifyKeyFrameReceived
#define connectionReceivedCompleteFrame testConnectionReceivedCompleteFrame
#define LiGetCurrentHostDisplayHdrMode testGetCurrentHostDisplayHdrMode
#include "../src/VideoDepacketizer.c"
#include "../src/DynamicHdr.h"
#include "PyrowaveDynamicHdrFixtures.h"

#include <stdio.h>
#include <string.h>

void testNotifyKeyFrameReceived(void) {}
void testConnectionReceivedCompleteFrame(uint32_t frameNumber, bool frameIsLTR) {
    (void)frameNumber;
    (void)frameIsLTR;
}
bool testGetCurrentHostDisplayHdrMode(void) { return true; }

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static size_t fixtureMetadata(const PYROWAVE_DYNAMIC_HDR_FIXTURE* fixture,
                              uint8_t* output, size_t capacity) {
    const size_t hexLength = strlen(fixture->payloadHex);
    const size_t payloadLength = hexLength / 2;
    const size_t length = 8 + payloadLength + (fixture->hlgNominalPeakNits != 0 ? 10 : 0);
    if ((hexLength & 1) != 0 || length > capacity || payloadLength > UINT16_MAX) {
        return 0;
    }
    const uint8_t header[] = {(uint8_t)(fixture->type >> 8), (uint8_t)fixture->type,
                             0, LI_PYROWAVE_METADATA_FLAG_PROTECTED | LI_PYROWAVE_METADATA_FLAG_REQUIRED,
                             0, 0, (uint8_t)(payloadLength >> 8), (uint8_t)payloadLength};
    memcpy(output, header, sizeof(header));
    for (size_t i = 0; i < payloadLength; ++i) {
        const char hexByte[] = {fixture->payloadHex[i * 2], fixture->payloadHex[i * 2 + 1], 0};
        output[8 + i] = (uint8_t)strtoul(hexByte, NULL, 16);
    }
    if (fixture->hlgNominalPeakNits != 0) {
        const uint8_t peak[] = {0, LI_PYROWAVE_METADATA_HLG_NOMINAL_PEAK,
                               0, LI_PYROWAVE_METADATA_FLAG_PROTECTED | LI_PYROWAVE_METADATA_FLAG_REQUIRED,
                               0, 0, 0, 2, (uint8_t)(fixture->hlgNominalPeakNits >> 8),
                               (uint8_t)fixture->hlgNominalPeakNits};
        memcpy(output + 8 + payloadLength, peak, sizeof(peak));
    }
    return length;
}

int main(void) {
    static const uint8_t codec[] = {1, 2, 3, 4};
    uint8_t metadata[512];
    const size_t dynamicLength = fixtureMetadata(&PyrowaveDynamicHdrFixtures[1], metadata, sizeof(metadata) - 10);
    const uint8_t runtime[] = {1, 0, 0, LI_PYROWAVE_METADATA_FLAG_PROTECTED |
                                         LI_PYROWAVE_METADATA_FLAG_RUNTIME |
                                         LI_PYROWAVE_METADATA_FLAG_OPTIONAL,
                               0, 0, 0, 2, 0, 0};
    const size_t metadataLength = dynamicLength + sizeof(runtime);
    VIDEO_FRAME_HANDLE frameHandle;
    PDECODE_UNIT decodeUnit;
    CHECK(dynamicLength != 0);
    memcpy(metadata + dynamicLength, runtime, sizeof(runtime));
    NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE;
    NegotiatedDynamicHdrFormat = DYNAMIC_HDR_FORMAT_VIVID_PQ;
    StreamConfig.hdrMode = 1;
    StreamConfig.fps = 60;
    VideoCallbacks.capabilities = CAPABILITY_PULL_RENDERER;
    initializeVideoDepacketizer(1392);
    for (unsigned frame = 1; frame <= 2; ++frame) {
        uint8_t* allocation = malloc(sizeof(PYROWAVE_FRAME_ENTRY) + sizeof(codec) + metadataLength);
        CHECK(allocation != NULL);
        metadata[metadataLength - 1] = (uint8_t)frame;
        CHECK(processPyrowaveMetadata(metadata, metadataLength,
                                      LI_PYROWAVE_METADATA_FLAG_PROTECTED | LI_PYROWAVE_METADATA_FLAG_RUNTIME));
        memcpy(allocation + sizeof(PYROWAVE_FRAME_ENTRY), codec, sizeof(codec));
        memcpy(allocation + sizeof(PYROWAVE_FRAME_ENTRY) + sizeof(codec), metadata, metadataLength);
        CHECK(queueOwnedFrame(allocation, sizeof(codec), metadataLength));
        frameType = FRAME_TYPE_IDR;
        nextFrameNumber = frame + 1;
        reassembleFrame(frame, false);
    }
    // Reset/reuse receive-side state after queueing: each frame keeps its own TLVs.
    LiPyrowaveReassemblyReset(&pyrowaveReassembly);
    for (unsigned frame = 1; frame <= 2; ++frame) {
        CHECK(LiPollNextVideoFrame(&frameHandle, &decodeUnit));
        CHECK(decodeUnit->frameNumber == (int)frame);
        CHECK(decodeUnit->fullLength == sizeof(codec));
        CHECK(decodeUnit->frameHostProcessingLatency == frame);
        CHECK(decodeUnit->pyrowaveMetadataLength == metadataLength);
        CHECK(decodeUnit->pyrowaveMetadata[metadataLength - 1] == (uint8_t)frame);
        CHECK(memcmp(decodeUnit->bufferList->data, codec, sizeof(codec)) == 0);
        LiCompleteVideoFrame(frameHandle, DR_OK);
    }
    CHECK(!LiPollNextVideoFrame(&frameHandle, &decodeUnit));

    CHECK(LiPyrowaveDynamicHdrMetadataType(DYNAMIC_HDR_FORMAT_NONE) == 0);
    CHECK(LiPyrowaveDynamicHdrMetadataType(-1) == 0);
    CHECK(LiPyrowaveDynamicHdrMetadataType(6) == 0);
    for (size_t i = 0; i < sizeof(PyrowaveDynamicHdrFixtures) / sizeof(PyrowaveDynamicHdrFixtures[0]); ++i) {
        const PYROWAVE_DYNAMIC_HDR_FIXTURE* fixture = &PyrowaveDynamicHdrFixtures[i];
        const bool hlg = fixture->hlgNominalPeakNits != 0;
        uint8_t tlv[512];
        uint8_t duplicate[sizeof(tlv) * 2];
        const size_t length = fixtureMetadata(fixture, tlv, sizeof(tlv));
        CHECK(length != 0);
        CHECK(LiPyrowaveDynamicHdrMetadataType(fixture->format) == fixture->type);
        NegotiatedDynamicHdrFormat = fixture->format;
        StreamConfig.hdrMode = hlg ? 2 : 1;
        CHECK(processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        StreamConfig.hdrMode = hlg ? 1 : 2;
        CHECK(!processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        StreamConfig.hdrMode = 0;
        CHECK(!processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        StreamConfig.hdrMode = hlg ? 2 : 1;
        CHECK(!processPyrowaveMetadata(NULL, 0, 0));
        const size_t prefixLength = fixture->type == LI_PYROWAVE_METADATA_HDR10_PLUS ? 6 :
                                    fixture->type == LI_PYROWAVE_METADATA_HDR_VIVID ? 5 : 2;
        const size_t minimumPayloadLength = prefixLength +
                                           (fixture->type == LI_PYROWAVE_METADATA_DOLBY_VISION_RPU ? 1 : 2);
        for (size_t truncatedLength = 0; truncatedLength < minimumPayloadLength; ++truncatedLength) {
            uint8_t truncated[sizeof(tlv)];
            memcpy(truncated, tlv, 8 + truncatedLength);
            truncated[4] = truncated[5] = truncated[6] = 0;
            truncated[7] = (uint8_t)truncatedLength;
            if (hlg) {
                memcpy(truncated + 8 + truncatedLength, tlv + length - 10, 10);
            }
            CHECK(!processPyrowaveMetadata(truncated, 8 + truncatedLength + (hlg ? 10 : 0),
                                          LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        }
        for (size_t byte = 0; byte < prefixLength; ++byte) {
            uint8_t wrongIdentifier[sizeof(tlv)];
            memcpy(wrongIdentifier, tlv, length);
            wrongIdentifier[8 + byte] ^= 1;
            CHECK(!processPyrowaveMetadata(wrongIdentifier, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        }
        memcpy(duplicate, tlv, length);
        memcpy(duplicate + length, tlv, length);
        CHECK(!processPyrowaveMetadata(duplicate, length * 2, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        tlv[3] = LI_PYROWAVE_METADATA_FLAG_PROTECTED;
        CHECK(!processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        tlv[3] |= LI_PYROWAVE_METADATA_FLAG_OPTIONAL;
        CHECK(!processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        tlv[3] = LI_PYROWAVE_METADATA_FLAG_REQUIRED;
        CHECK(!processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        tlv[3] |= LI_PYROWAVE_METADATA_FLAG_PROTECTED;
        tlv[1] = fixture->type == LI_PYROWAVE_METADATA_HDR10_PLUS
                   ? LI_PYROWAVE_METADATA_HDR_VIVID : LI_PYROWAVE_METADATA_HDR10_PLUS;
        CHECK(!processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        tlv[1] = (uint8_t)fixture->type;
        if (hlg) {
            CHECK(!processPyrowaveMetadata(tlv, length - 10, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
            tlv[length - 2] = tlv[length - 1] = 0;
            CHECK(!processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
            tlv[length - 2] = (uint8_t)(fixture->hlgNominalPeakNits >> 8);
            tlv[length - 1] = (uint8_t)fixture->hlgNominalPeakNits;
            tlv[length - 7] = LI_PYROWAVE_METADATA_FLAG_PROTECTED;
            CHECK(!processPyrowaveMetadata(tlv, length, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
            tlv[length - 7] |= LI_PYROWAVE_METADATA_FLAG_REQUIRED;
            memcpy(duplicate, tlv, length);
            memcpy(duplicate + length, tlv + length - 10, 10);
            CHECK(!processPyrowaveMetadata(duplicate, length + 10, LI_PYROWAVE_METADATA_FLAG_PROTECTED));
        }
    }
    NegotiatedDynamicHdrFormat = 6;
    CHECK(!processPyrowaveMetadata(NULL, 0, 0));
    NegotiatedDynamicHdrFormat = DYNAMIC_HDR_FORMAT_NONE;
    CHECK(processPyrowaveMetadata(NULL, 0, 0));
    CHECK(frameHostProcessingLatency == 0);
    destroyVideoDepacketizer();
    puts("PASS: same-frame metadata ownership and production depacketizer validation");
    return 0;
}
