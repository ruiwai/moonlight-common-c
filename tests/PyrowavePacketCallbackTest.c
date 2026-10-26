// Link the real Common library; future ASAN runs check ownership, not free counts.
#include "Limelight-internal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#undef VideoCallbacks
#undef NegotiatedVideoFormat

typedef struct {
    unsigned int calls;
    LI_PYROWAVE_RECEIVED_PACKET last;
    uint8_t bytes[LI_PYROWAVE_MAX_PACKET_SIZE];
} CAPTURE;

static ML_CONNECTION_CONTEXT connections[2];
static CAPTURE captures[2];
static unsigned int setupCalls;

static void submit(void* context, const LI_PYROWAVE_RECEIVED_PACKET* packet) {
    CAPTURE* capture = context;
    assert(capture != NULL && packet->length <= sizeof(capture->bytes));
    capture->last = *packet;
    memcpy(capture->bytes, packet->data, packet->length);
    capture->last.data = capture->bytes; // Retain only the application's owned copy.
    capture->calls++;
}

static void submitNull(void* context, const LI_PYROWAVE_RECEIVED_PACKET* packet) {
    assert(context == NULL);
    submit(&captures[1], packet);
}

static PRTP_PACKET makePacket(int innerLength, uint8_t header, uint32_t frame, int* length) {
    int offset = (int)sizeof(RTP_PACKET) + ((header & FLAG_EXTENSION) ? 4 : 0);
    *length = offset + (int)sizeof(NV_VIDEO_PACKET) + innerLength;
    PRTP_PACKET packet = malloc((size_t)*length);
    assert(packet != NULL);
    memset(packet, 0, (size_t)*length);
    packet->header = header;
    packet->sequenceNumber = 60000;
    packet->timestamp = 0x12345678;
    PNV_VIDEO_PACKET nv = (PNV_VIDEO_PACKET)((uint8_t*)packet + offset);
    nv->frameIndex = frame; // Direct depacketizer input is already host endian.
    nv->flags = FLAG_SOF | FLAG_EOF;
    for (int i = 0; i < innerLength; i++) {
        ((uint8_t*)(nv + 1))[i] = (uint8_t)(i ^ 0xA5); // Arbitrary bytes, not PYRF validation.
    }
    return packet;
}

static void direct(ML_CONNECTION_CONTEXT* connection, int size, uint8_t header,
                   bool parity, uint64_t receive, uint64_t presentation) {
    RTPV_QUEUE_ENTRY entry = {0};
    entry.packet = makePacket(size, header, 0xFEDCBA98, &entry.length);
    entry.rtpTimestamp = 0x87654321; // Intentionally distinct from packet timestamp/payload.
    entry.receiveTimeUs = receive;
    entry.presentationTimeUs = presentation;
    entry.isParity = parity;
    queueRtpPacketCtx(&connection->videoContext.depacketizerContext, &entry);
}

static void expect(const CAPTURE* capture, size_t length, uint32_t frame,
                   uint32_t timestamp, uint64_t receive, uint64_t presentation) {
    assert(capture->last.length == length && capture->last.frameId == frame);
    assert(capture->last.rtpTimestamp == timestamp);
    assert(capture->last.receiveTimeUs == receive);
    assert(capture->last.presentationTimeUs == presentation);
    assert(capture->last.data == capture->bytes);
    for (size_t i = 0; i < length; i++) {
        assert(capture->bytes[i] == (uint8_t)(i ^ 0xA5));
    }
}

static void shortDirect(ML_CONNECTION_CONTEXT* connection, int length) {
    RTPV_QUEUE_ENTRY entry = {0};
    entry.length = length;
    if (length > 0) {
        entry.packet = malloc((size_t)length);
        assert(entry.packet != NULL);
        memset(entry.packet, 0, (size_t)length);
        ((uint8_t*)entry.packet)[0] = 0x90; // May be shorter than an RTP_PACKET.
    }
    queueRtpPacketCtx(&connection->videoContext.depacketizerContext, &entry);
}

static int setupTrap(int format, int width, int height, int fps, void* context, int flags) {
    (void)format; (void)width; (void)height; (void)fps; (void)context; (void)flags;
    setupCalls++;
    return -99; // Even a broken gate must not open sockets in this test.
}

static void invalidCases(ML_CONNECTION_CONTEXT* connection, const CAPTURE* capture) {
    unsigned int before = capture->calls;
    shortDirect(connection, 0);
    shortDirect(connection, 1);
    shortDirect(connection, (int)sizeof(RTP_PACKET) - 1);
    shortDirect(connection, (int)sizeof(RTP_PACKET) + 3);
    shortDirect(connection, MAX_RTP_HEADER_SIZE + (int)sizeof(NV_VIDEO_PACKET) - 1);
    direct(connection, 64, 0x50, false, 0, 0); // Wrong RTP version.
    direct(connection, 64, 0x91, false, 0, 0); // CSRC unsupported.
    direct(connection, 64, 0xB0, false, 0, 0); // Padding unsupported.
    direct(connection, 63, 0x90, false, 0, 0);
    direct(connection, LI_PYROWAVE_MAX_PACKET_SIZE + 1, 0x90, false, 0, 0);
    direct(connection, 64, 0x90, true, 0, 0);
    connection->NegotiatedPyrowave.maxPacketSize = 80;
    direct(connection, 81, 0x90, false, 0, 0);
    assert(capture->calls == before);
    connection->NegotiatedPyrowave.maxPacketSize = LI_PYROWAVE_MAX_PACKET_SIZE;

    connection->VideoCallbacks.setup = setupTrap;
    connection->VideoCallbacks.pyrowave.submit = NULL;
    direct(connection, 64, 0x90, false, 0, 0);
    assert(startVideoStreamCtx(&connection->videoContext, NULL, 0) == -1);
    assert(connection->videoContext.firstFrameSocket == INVALID_SOCKET);
    connection->VideoCallbacks.pyrowave.submit = submit;
    connection->VideoCallbacks.pyrowave.version = 0;
    direct(connection, 64, 0x90, false, 0, 0);
    assert(startVideoStreamCtx(&connection->videoContext, NULL, 0) == -1);
    connection->VideoCallbacks.pyrowave.version = 2;
    direct(connection, 64, 0x90, false, 0, 0);
    connection->VideoCallbacks.pyrowave.version = LI_PYROWAVE_PACKET_CALLBACK_VERSION1;
    connection->VideoCallbacks.capabilities = 0;
    direct(connection, 64, 0x90, false, 0, 0);
    assert(startVideoStreamCtx(&connection->videoContext, NULL, 0) == -1);
    connection->VideoCallbacks.capabilities = CAPABILITY_PYROWAVE;
    assert(setupCalls == 0 && capture->calls == before);
    assert(startVideoStreamCtx(&connection->videoContext, NULL, 0) == -99);
    assert(setupCalls == 1 && capture->calls == before); // Setup failure prevents sockets.
}

static void queueCases(ML_CONNECTION_CONTEXT* connection, CAPTURE* capture) {
    RTP_VIDEO_QUEUE* queue = &connection->videoContext.rtpQueue;
    uint8_t controlBefore[sizeof(connection->controlContext)];
    memcpy(controlBefore, &connection->controlContext, sizeof(controlBefore));
    const uint32_t frames[] = {65535, 65536, 65534, 0xfffffffe, 0xffffffff, 0};
    unsigned int before = capture->calls;
    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        RTPV_QUEUE_ENTRY entry = {0};
        int length;
        PRTP_PACKET packet = makePacket(80, i == 1 ? 0x80 : 0x90, LE32(frames[i]), &length);
        packet->sequenceNumber = (uint16_t)(60000 + i);
        uint64_t earliest = PltGetMicroseconds();
        assert(RtpvAddPacket(queue, packet, length, &entry) == RTPF_RET_QUEUED);
        assert(capture->calls == before + i + 1);
        assert(entry.receiveTimeUs >= earliest && entry.receiveTimeUs <= PltGetMicroseconds());
        expect(capture, 80, frames[i], 0x12345678, entry.receiveTimeUs,
               ((uint64_t)0x12345678 * 1000) / 90);
        assert(entry.presentationTimeUs == capture->last.presentationTimeUs);
    }
    // Reordering and full 32-bit wrap are delivered; inner reassembly owns stale filtering.
    assert(queue->currentFrameNumber == 0);
    assert(memcmp(controlBefore, &connection->controlContext, sizeof(controlBefore)) == 0);
    assert(queue->pendingFecBlockList.count == 0 && queue->completedFecBlockList.count == 0);
}

static void rejectedCases(ML_CONNECTION_CONTEXT* connection) {
    RTP_VIDEO_QUEUE* queue = &connection->videoContext.rtpQueue;
    RTPV_QUEUE_ENTRY entry = {0};
    assert(RtpvAddPacket(queue, NULL, 0, &entry) == RTPF_RET_REJECTED);
    const int sizes[] = {1, (int)sizeof(RTP_PACKET) - 1,
                         MAX_RTP_HEADER_SIZE + (int)sizeof(NV_VIDEO_PACKET) - 1};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        PRTP_PACKET packet = malloc((size_t)sizes[i]);
        assert(packet != NULL);
        memset(packet, 0, (size_t)sizes[i]);
        ((uint8_t*)packet)[0] = 0x90; // Deliberately truncated allocation.
        assert(RtpvAddPacket(queue, packet, sizes[i], &entry) == RTPF_RET_REJECTED);
        free(packet); // REJECTED retains caller ownership.
    }
}

int main(void) {
    for (size_t i = 0; i < 2; i++) {
        ML_CONNECTION_CONTEXT* connection = &connections[i];
        LiInitializeVideoCallbacks(&connection->VideoCallbacks);
        assert(connection->VideoCallbacks.pyrowave.version == 0);
        assert(connection->VideoCallbacks.pyrowave.submit == NULL);
        assert(connection->VideoCallbacks.pyrowave.context == NULL);
        connection->NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE;
        connection->NegotiatedPyrowave.maxPacketSize = LI_PYROWAVE_MAX_PACKET_SIZE;
        connection->VideoCallbacks.capabilities = CAPABILITY_PYROWAVE;
        connection->VideoCallbacks.pyrowave.version = LI_PYROWAVE_PACKET_CALLBACK_VERSION1;
        connection->VideoCallbacks.pyrowave.context = &captures[i];
        connection->VideoCallbacks.pyrowave.submit = submit;
        connection->videoContext.connectionContext = connection;
        connection->videoContext.depacketizerContext.connectionContext = connection;
        RtpvInitializeQueue(&connection->videoContext.rtpQueue,
                            &connection->videoContext.depacketizerContext);
    }
    LiSetThreadConnectionContext(&connections[1]); // Opposite TLS must not route submissions.
    direct(&connections[0], 64, 0x90, false, 1234567, 7654321);
    assert(captures[0].calls == 1 && captures[1].calls == 0);
    expect(&captures[0], 64, 0xFEDCBA98, 0x87654321, 1234567, 7654321);
    LiSetThreadConnectionContext(&connections[0]);
    direct(&connections[1], 96, 0x80, false, 0, 0); // No extension; zero microseconds preserved.
    assert(captures[0].calls == 1 && captures[1].calls == 1);
    expect(&captures[1], 96, 0xFEDCBA98, 0x87654321, 0, 0);
    connections[1].VideoCallbacks.pyrowave.context = NULL;
    connections[1].VideoCallbacks.pyrowave.submit = submitNull;
    direct(&connections[1], LI_PYROWAVE_MAX_PACKET_SIZE, 0x90, false, 0, 0);
    expect(&captures[1], LI_PYROWAVE_MAX_PACKET_SIZE, 0xFEDCBA98, 0x87654321, 0, 0);
    connections[1].VideoCallbacks.pyrowave.context = &captures[1];
    connections[1].VideoCallbacks.pyrowave.submit = submit;
    invalidCases(&connections[0], &captures[0]);
    LiSetThreadConnectionContext(NULL);
    queueCases(&connections[0], &captures[0]);
    queueCases(&connections[1], &captures[1]);
    for (size_t i = 0; i < 2; i++) {
        unsigned int before = captures[i].calls;
        rejectedCases(&connections[i]);
        connections[i].NegotiatedVideoFormat = VIDEO_FORMAT_H264;
        shortDirect(&connections[i], 1);
        shortDirect(&connections[i], MAX_RTP_HEADER_SIZE + (int)sizeof(NV_VIDEO_PACKET) - 1);
        rejectedCases(&connections[i]);
        assert(captures[i].calls == before);
        assert(connections[i].videoContext.depacketizerContext.nalChainHead == NULL);
        RtpvCleanupQueue(&connections[i].videoContext.rtpQueue);
    }
    return 0;
}
