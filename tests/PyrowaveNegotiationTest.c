// Actual Common helpers/serializer, without sockets; not live RTSP interoperability.
#include "Limelight-internal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef StreamConfig
#undef VideoCallbacks
#undef NegotiatedVideoFormat
#undef RemoteAddr

static const char* const names[5] = {
    "protocolVersion", "bitstreamVersion", "payloadVersion", "capabilityFlags", "maxPacketSize"
};
static const char* const defaults[5] = {"2", "2", "3", "2047", "1200"};
static char sdp[2048];
static size_t sdpLength;
static ML_CONNECTION_CONTEXT contexts[2];

static size_t describe(char* output, const char* const values[5], const char* eol) {
    size_t length = 0;
    for (size_t i = 0; i < 5; i++) {
        if (values[i] != NULL) {
            int n = snprintf(output + length, 2048 - length, "a=x-ss-pyrowave.%s:%s%s",
                             names[i], values[i], eol);
            assert(n > 0 && (size_t)n < 2048 - length);
            length += (size_t)n;
        }
    }
    return length;
}

static void expectZero(const LI_PYROWAVE_CAPABILITIES* caps) {
    const LI_PYROWAVE_CAPABILITIES zero = {0};
    assert(memcmp(caps, &zero, sizeof(zero)) == 0);
}

static void rejectParse(const char* input, size_t length) {
    LI_PYROWAVE_CAPABILITIES output;
    memset(&output, 0xA5, sizeof(output));
    assert(!LiParsePyrowaveServerSdp(input, length, &output));
    expectZero(&output);
}

static void parserCases(void) {
    char buffer[2048];
    LI_PYROWAVE_CAPABILITIES caps;
    const char* values[5];
    const char* endings[] = {"\n", "\r\n"};
    memcpy(values, defaults, sizeof(values));
    for (size_t i = 0; i < 2; i++) {
        values[0] = " \t2\t ";
        size_t n = describe(buffer, values, endings[i]);
        assert(LiParsePyrowaveServerSdp(buffer, n, &caps));
        assert(caps.protocolVersion == 2 && caps.bitstreamVersion == 2 && caps.payloadVersion == 3);
        assert(caps.capabilityFlags == 2047 && caps.maxPacketSize == 1200 && caps.reserved == 0);
        assert(LiParsePyrowaveServerSdp(buffer, n - strlen(endings[i]), &caps)); // No final LF.
        n += (size_t)snprintf(buffer + n, sizeof(buffer) - n,
            "a=x-ss-pyrowave.unknown:bad\na=x-ss-pyrowave.protocolVersionExtra:9\n");
        assert(LiParsePyrowaveServerSdp(buffer, n, &caps));
        // Scan beyond a complete set: even identical late duplicates must fail.
        for (size_t j = 0; j < 5; j++) {
            size_t m = (size_t)snprintf(buffer + n, sizeof(buffer) - n,
                                     "a=x-ss-pyrowave.%s:%s\n", names[j], defaults[j]);
            rejectParse(buffer, n + m);
        }
    }
    const char* bad[] = {"", " ", "+2", "-2", "0x2", "2x", "2 0", "2\t0", "2\r0",
                         "4294967296", "999999999999999999999", "\v2", "\200"};
    for (size_t i = 0; i < 5; i++) {
        memcpy(values, defaults, sizeof(values));
        values[i] = NULL;
        rejectParse(buffer, describe(buffer, values, "\n"));
        for (size_t j = 0; j < sizeof(bad) / sizeof(bad[0]); j++) {
            values[i] = bad[j];
            rejectParse(buffer, describe(buffer, values, "\n"));
        }
        values[i] = i < 3 ? "65536" : "4294967295";
        size_t n = describe(buffer, values, "\n");
        if (i < 3) { rejectParse(buffer, n); }
        else { assert(LiParsePyrowaveServerSdp(buffer, n, &caps)); }
    }
    const char* unsupported[] = {"65535", "0", "1", "0", "0"};
    size_t n = describe(buffer, unsupported, "\n");
    assert(LiParsePyrowaveServerSdp(buffer, n, &caps)); // Syntax, not policy.
    assert(caps.protocolVersion == UINT16_MAX && caps.maxPacketSize == 0);
    n = describe(buffer, defaults, "\n");
    buffer[n - 1] = '\r';
    rejectParse(buffer, n); // A bare final CR is not CRLF.
    buffer[n - 1] = '\0';
    rejectParse(buffer, n);
    rejectParse(NULL, 0);
    rejectParse(NULL, 1);
    rejectParse(buffer, 0);
    rejectParse(buffer, 1024u * 1024u + 1u); // Must reject before reading tiny allocation.
    assert(!LiParsePyrowaveServerSdp(buffer, n, NULL));
}

static void submit(void* context, const LI_PYROWAVE_RECEIVED_PACKET* packet) {
    (void)context; (void)packet;
    assert(false); // Negotiation/serialization must not submit packets.
}

static void initialize(ML_CONNECTION_CONTEXT* ctx, int range) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->AppVersionQuad[0] = 7; ctx->AppVersionQuad[1] = 1;
    ctx->AppVersionQuad[2] = 431; ctx->AppVersionQuad[3] = -1;
    struct sockaddr_in* addr = (struct sockaddr_in*)&ctx->RemoteAddr;
    addr->sin_family = AF_INET; addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ctx->StreamConfig.streamingRemotely = STREAM_CFG_LOCAL;
    ctx->StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    ctx->StreamConfig.width = 1280; ctx->StreamConfig.height = 720;
    ctx->StreamConfig.fps = 60; ctx->StreamConfig.bitrate = 10000;
    ctx->StreamConfig.packetSize = 1392;
    ctx->StreamConfig.colorSpace = COLORSPACE_REC_709; ctx->StreamConfig.colorRange = range;
    ctx->StreamConfig.dynamicRangeMode = DYNAMIC_RANGE_MODE_SDR;
    ctx->StreamConfig.supportedVideoFormats = VIDEO_FORMAT_PYROWAVE | VIDEO_FORMAT_H264;
    ctx->VideoCallbacks.capabilities = CAPABILITY_PYROWAVE;
    ctx->VideoCallbacks.pyrowave.version = LI_PYROWAVE_PACKET_CALLBACK_VERSION1;
    ctx->VideoCallbacks.pyrowave.submit = submit;
    ctx->NegotiatedVideoFormat = VIDEO_FORMAT_H264;
}

static void rejectNegotiation(ML_CONNECTION_CONTEXT* ctx) {
    ctx->NegotiatedPyrowave.maxPacketSize = 999; // Stale selected state must disappear.
    int selected = ctx->NegotiatedVideoFormat;
    assert(!negotiatePyrowaveSdrCtx(ctx, sdp, sdpLength));
    expectZero(&ctx->NegotiatedPyrowave);
    assert(ctx->NegotiatedVideoFormat == selected);
}

#define REJECT_LOCAL(field, value) do { \
    int saved = ctx->field; ctx->field = (value); \
    rejectNegotiation(ctx); ctx->field = saved; \
} while (0)

static void negotiationCases(void) {
    ML_CONNECTION_CONTEXT* ctx = &contexts[0];
    initialize(ctx, COLOR_RANGE_LIMITED);
    initialize(&contexts[1], COLOR_RANGE_FULL);
    sdpLength = describe(sdp, defaults, "\n");
    assert(negotiatePyrowaveSdrCtx(ctx, sdp, sdpLength));
    assert(ctx->NegotiatedVideoFormat == VIDEO_FORMAT_H264); // Handshake owns selection.
    LI_PYROWAVE_CAPABILITIES first = ctx->NegotiatedPyrowave;
    contexts[1].StreamConfig.packetSize = 1000;
    assert(negotiatePyrowaveSdrCtx(&contexts[1], sdp, sdpLength));
    assert(contexts[1].NegotiatedPyrowave.maxPacketSize == 984);
    assert(memcmp(&first, &ctx->NegotiatedPyrowave, sizeof(first)) == 0);
    assert(first.maxPacketSize == 1200);
    ctx->NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE;
    REJECT_LOCAL(AppVersionQuad[0], 6); REJECT_LOCAL(AppVersionQuad[3], 0);
    REJECT_LOCAL(StreamConfig.width, 1); REJECT_LOCAL(StreamConfig.width, 1281);
    REJECT_LOCAL(StreamConfig.width, 16386); REJECT_LOCAL(StreamConfig.height, 0);
    REJECT_LOCAL(StreamConfig.height, 721); REJECT_LOCAL(StreamConfig.height, 16386);
    REJECT_LOCAL(StreamConfig.dynamicRangeMode, DYNAMIC_RANGE_MODE_HDR10_PQ);
    REJECT_LOCAL(StreamConfig.dynamicRangeMode, DYNAMIC_RANGE_MODE_HLG);
    REJECT_LOCAL(StreamConfig.colorSpace, COLORSPACE_REC_601);
    REJECT_LOCAL(StreamConfig.colorSpace, COLORSPACE_REC_2020);
    REJECT_LOCAL(StreamConfig.colorRange, -1); REJECT_LOCAL(StreamConfig.colorRange, 2);
    REJECT_LOCAL(StreamConfig.supportedVideoFormats, VIDEO_FORMAT_H264);
    REJECT_LOCAL(VideoCallbacks.capabilities, 0);
    REJECT_LOCAL(VideoCallbacks.pyrowave.version, 0); REJECT_LOCAL(VideoCallbacks.pyrowave.version, 2);
    ctx->VideoCallbacks.pyrowave.submit = NULL; rejectNegotiation(ctx);
    ctx->VideoCallbacks.pyrowave.submit = submit;
    REJECT_LOCAL(StreamConfig.packetSize, -1); REJECT_LOCAL(StreamConfig.packetSize, 144);
    REJECT_LOCAL(StreamConfig.packetSize, 65553); REJECT_LOCAL(StreamConfig.packetSize, INT_MAX);
    ctx->StreamConfig.packetSize = 145;
    assert(negotiatePyrowaveSdrCtx(ctx, sdp, sdpLength) && ctx->NegotiatedPyrowave.maxPacketSize == 129);
    ctx->StreamConfig.packetSize = 65552;
    ctx->StreamConfig.width = 2; ctx->StreamConfig.height = 16384;
    assert(negotiatePyrowaveSdrCtx(ctx, sdp, sdpLength));
    initialize(ctx, COLOR_RANGE_LIMITED);
    const char* values[5];
    for (size_t i = 0; i < 5; i++) {
        memcpy(values, defaults, sizeof(values));
        values[i] = i < 3 ? "1" : "0";
        sdpLength = describe(sdp, values, "\n"); rejectNegotiation(ctx);
    }
    uint32_t required = LI_PYROWAVE_REQUIRED_SDR_BASE_CAPABILITIES | LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE;
    for (uint32_t bit = 1; bit <= required; bit <<= 1) {
        if (!(required & bit)) { continue; }
        char flags[16]; snprintf(flags, sizeof(flags), "%u", (unsigned int)(2047u & ~bit));
        memcpy(values, defaults, sizeof(values)); values[3] = flags;
        sdpLength = describe(sdp, values, "\n"); rejectNegotiation(ctx);
    }
    const char* badBudgets[] = {"64", "65", "128", "65537", "4294967295"};
    for (size_t i = 0; i < sizeof(badBudgets) / sizeof(badBudgets[0]); i++) {
        memcpy(values, defaults, sizeof(values)); values[4] = badBudgets[i];
        sdpLength = describe(sdp, values, "\n"); rejectNegotiation(ctx);
    }
    memcpy(values, defaults, sizeof(values)); values[4] = "129";
    sdpLength = describe(sdp, values, "\n");
    assert(negotiatePyrowaveSdrCtx(ctx, sdp, sdpLength) && ctx->NegotiatedPyrowave.maxPacketSize == 129);
    assert(!negotiatePyrowaveSdrCtx(NULL, sdp, sdpLength) && !isPyrowaveSdrNegotiatedCtx(NULL));
    assert(!negotiatePyrowaveSdrCtx(ctx, NULL, 0)); expectZero(&ctx->NegotiatedPyrowave);
}

static void attribute(const char* payload, const char* name, uint32_t value) {
    char expected[128];
    snprintf(expected, sizeof(expected), "a=%s:%u \r\n", name, (unsigned int)value);
    const char* found = strstr(payload, expected);
    assert(found != NULL && strstr(found + strlen(expected), expected) == NULL);
}

#define REJECT_STORED(field, value) do { \
    LI_PYROWAVE_CAPABILITIES saved = ctx->NegotiatedPyrowave; \
    ctx->NegotiatedPyrowave.field = (value); \
    assert(!isPyrowaveSdrNegotiatedCtx(ctx)); \
    assert(getSdpPayloadForStreamConfigCtx(ctx, 14, &length) == NULL); \
    ctx->NegotiatedPyrowave = saved; \
} while (0)

static void serializerCases(void) {
    for (int range = COLOR_RANGE_LIMITED; range <= COLOR_RANGE_FULL; range++) {
        ML_CONNECTION_CONTEXT* ctx = &contexts[range];
        initialize(ctx, range); sdpLength = describe(sdp, defaults, "\r\n");
        assert(negotiatePyrowaveSdrCtx(ctx, sdp, sdpLength));
        assert(!isPyrowaveSdrNegotiatedCtx(ctx));
        ctx->NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE;
        LiSetThreadConnectionContext(ctx); // Existing serializer/RFI code uses effective TLS.
        assert(isPyrowaveSdrNegotiatedCtx(ctx));
        int length = 0;
        char* payload = getSdpPayloadForStreamConfigCtx(ctx, 14, &length);
        assert(payload != NULL && length > 0);
        const uint32_t expected[] = {2, 2, 3, ctx->NegotiatedPyrowave.capabilityFlags, 1200};
        for (size_t i = 0; i < 5; i++) {
            char name[80]; snprintf(name, sizeof(name), "x-ml-pyrowave.%s", names[i]);
            attribute(payload, name, expected[i]);
        }
        attribute(payload, "x-ml-general.featureFlags", ML_FF_FEC_STATUS | ML_FF_SESSION_ID_V1 | ML_FF_PYROWAVE);
        attribute(payload, "x-nv-vqos[0].bitStreamFormat", 65536);
        attribute(payload, "x-nv-video[0].dynamicRangeMode", 0);
        attribute(payload, "x-nv-video[0].encoderCscMode", (uint32_t)(2 + range));
        assert(expected[3] == (LI_PYROWAVE_REQUIRED_SDR_BASE_CAPABILITIES |
            (range ? LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE : LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE)));
        free(payload);
        REJECT_STORED(protocolVersion, 1); REJECT_STORED(bitstreamVersion, 1);
        REJECT_STORED(payloadVersion, 1); REJECT_STORED(reserved, 1);
        REJECT_STORED(capabilityFlags, expected[3] ^ LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE);
        REJECT_STORED(capabilityFlags, expected[3] | LI_PYROWAVE_CAPABILITY_PARTIAL_FRAME);
        REJECT_STORED(maxPacketSize, 128); REJECT_STORED(maxPacketSize, 1377);
        ctx->StreamConfig.colorSpace = COLORSPACE_REC_2020;
        assert(!isPyrowaveSdrNegotiatedCtx(ctx) && getSdpPayloadForStreamConfigCtx(ctx, 14, &length) == NULL);
        ctx->StreamConfig.colorSpace = COLORSPACE_REC_709;
        ctx->NegotiatedVideoFormat = VIDEO_FORMAT_H264;
        payload = getSdpPayloadForStreamConfigCtx(ctx, 14, &length);
        assert(payload != NULL && strstr(payload, "x-ml-pyrowave.") == NULL);
        attribute(payload, "x-ml-general.featureFlags", ML_FF_FEC_STATUS | ML_FF_SESSION_ID_V1);
        attribute(payload, "x-nv-vqos[0].bitStreamFormat", 0);
        assert(ctx->StreamConfig.colorRange == range && ctx->StreamConfig.supportedVideoFormats ==
            (VIDEO_FORMAT_PYROWAVE | VIDEO_FORMAT_H264));
        free(payload);
    }
    LiSetThreadConnectionContext(NULL);
}

int main(void) {
    parserCases(); negotiationCases(); serializerCases();
    return 0;
}
