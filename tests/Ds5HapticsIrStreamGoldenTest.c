#include "Ds5HapticsIrStream.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while (0)

static void writeLe16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void writeLe32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void writeLe64(uint8_t* data, uint64_t value) {
    writeLe32(data, (uint32_t)value);
    writeLe32(data + 4, (uint32_t)(value >> 32));
}

static void writeLeFloat(uint8_t* data, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    writeLe32(data, bits);
}

static void makeValidPacket(uint8_t packet[DS5_HAPTICS_IR_STREAM_WIRE_SIZE]) {
    int lane;
    memset(packet, 0, DS5_HAPTICS_IR_STREAM_WIRE_SIZE);
    packet[0] = DS5_HAPTICS_IR_STREAM_PROTOCOL_VERSION;
    packet[1] = LI_DS5_HAPTICS_IR_FLAG_DISCONTINUITY;
    writeLe16(packet + 2, DS5_HAPTICS_IR_STREAM_WIRE_SIZE);
    writeLe16(packet + 4, 3);
    writeLe32(packet + 8, UINT32_C(0x11223344));
    writeLe64(packet + 12, UINT64_C(0x0102030405060708));
    writeLe32(packet + 20, 240);
    for (lane = 0; lane < 2; lane++) {
        uint8_t* out = packet + 24 + lane * 20;
        writeLeFloat(out, 0.25f + lane * 0.1f);
        writeLeFloat(out + 4, 0.5f + lane * 0.1f);
        writeLeFloat(out + 8, 0.75f);
        writeLeFloat(out + 12, 0.2f);
        writeLeFloat(out + 16, 1200.0f + lane * 100.0f);
    }
    writeLeFloat(packet + 64, -0.5f);
}

static void captureFrame(const LI_DS5_HAPTICS_IR_FRAME_V2* frame, void* context) {
    LI_DS5_HAPTICS_IR_FRAME_V2* captured = context;
    *captured = *frame;
}

static void testValidFrame(void) {
    uint8_t packet[DS5_HAPTICS_IR_STREAM_WIRE_SIZE];
    LI_DS5_HAPTICS_IR_FRAME_V2 captured = {0};
    makeValidPacket(packet);

    CHECK(processDs5HapticsIrStreamPacket(packet, sizeof(packet), captureFrame, &captured));
    CHECK(captured.flags == LI_DS5_HAPTICS_IR_FLAG_DISCONTINUITY);
    CHECK(captured.controllerNumber == 3);
    CHECK(captured.sourceSequenceNumber == UINT32_C(0x11223344));
    CHECK(captured.timestampUs == UINT64_C(0x0102030405060708));
    CHECK(captured.sourceFrameCount == 240);
    CHECK(fabsf(captured.lanes[0].rmsAmplitude - 0.25f) < 0.0001f);
    CHECK(fabsf(captured.lanes[1].zeroCrossingRateHz - 1300.0f) < 0.0001f);
    CHECK(fabsf(captured.laneCorrelation + 0.5f) < 0.0001f);
}

static void testMalformedFrames(void) {
    uint8_t packet[DS5_HAPTICS_IR_STREAM_WIRE_SIZE];
    LI_DS5_HAPTICS_IR_FRAME_V2 captured = {0};
    makeValidPacket(packet);

    CHECK(!processDs5HapticsIrStreamPacket(packet, sizeof(packet) - 1, captureFrame, &captured));
    packet[0] = 1;
    CHECK(!processDs5HapticsIrStreamPacket(packet, sizeof(packet), captureFrame, &captured));
    packet[0] = DS5_HAPTICS_IR_STREAM_PROTOCOL_VERSION;
    packet[1] = 0x80;
    CHECK(!processDs5HapticsIrStreamPacket(packet, sizeof(packet), captureFrame, &captured));
    packet[1] = 0;
    writeLe16(packet + 6, 1);
    CHECK(!processDs5HapticsIrStreamPacket(packet, sizeof(packet), captureFrame, &captured));
    writeLe16(packet + 6, 0);
    writeLe32(packet + 24, UINT32_C(0x7fc00000));
    CHECK(!processDs5HapticsIrStreamPacket(packet, sizeof(packet), captureFrame, &captured));
    makeValidPacket(packet);
    writeLeFloat(packet + 24, 0.75f);
    writeLeFloat(packet + 28, 0.5f);
    CHECK(!processDs5HapticsIrStreamPacket(packet, sizeof(packet), captureFrame, &captured));
    makeValidPacket(packet);
    writeLeFloat(packet + 64, 1.1f);
    CHECK(!processDs5HapticsIrStreamPacket(packet, sizeof(packet), captureFrame, &captured));
}

int main(void) {
    testValidFrame();
    testMalformedFrames();
    if (failures != 0) {
        fprintf(stderr, "%d DualSense IR protocol checks failed\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
