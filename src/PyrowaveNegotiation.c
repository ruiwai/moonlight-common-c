#include "Limelight-internal.h"

#undef StreamConfig
#undef VideoCallbacks
#undef NegotiatedVideoFormat

static bool parseDecimal(const char* begin, const char* end, uint32_t* output) {
    uint32_t value = 0;
    while (begin < end && (*begin == ' ' || *begin == '\t')) {
        begin++;
    }
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    if (begin == end) {
        return false;
    }
    for (; begin < end; begin++) {
        if (*begin < '0' || *begin > '9') {
            return false;
        }
        uint32_t digit = (uint32_t)(*begin - '0');
        if (value > (UINT32_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }
    *output = value;
    return true;
}

static bool parseAttribute(const char* line, size_t length, uint32_t values[5], unsigned int* seen) {
    static const char prefix[] = "a=x-ss-pyrowave.";
    static const char* const names[] = {
        "protocolVersion:", "bitstreamVersion:", "payloadVersion:",
        "capabilityFlags:", "maxPacketSize:"
    };
    if (length < sizeof(prefix) - 1 || memcmp(line, prefix, sizeof(prefix) - 1) != 0) {
        return true;
    }
    line += sizeof(prefix) - 1;
    length -= sizeof(prefix) - 1;
    for (unsigned int i = 0; i < 5; i++) {
        size_t nameLength = strlen(names[i]);
        if (length >= nameLength && memcmp(line, names[i], nameLength) == 0) {
            if ((*seen & (1u << i)) || !parseDecimal(line + nameLength, line + length, &values[i]) ||
                    (i < 3 && values[i] > UINT16_MAX)) {
                return false;
            }
            *seen |= 1u << i;
            break;
        }
    }
    return true;
}

bool LiParsePyrowaveServerSdp(const char* sdp, size_t length, LI_PYROWAVE_CAPABILITIES* output) {
    uint32_t values[5] = {0};
    unsigned int seen = 0;
    if (output == NULL) {
        return false;
    }
    memset(output, 0, sizeof(*output));
    // Validate the span before even looking for its first byte or terminator.
    if (sdp == NULL || length == 0 || length > 1024u * 1024u || memchr(sdp, '\0', length) != NULL) {
        return false;
    }
    for (size_t offset = 0; offset < length;) {
        size_t start = offset;
        while (offset < length && sdp[offset] != '\n') {
            offset++;
        }
        size_t end = offset;
        if (offset < length && end > start && sdp[end - 1] == '\r') {
            end--;
        }
        if (!parseAttribute(sdp + start, end - start, values, &seen)) {
            return false;
        }
        if (offset < length) {
            offset++;
        }
    }
    if (seen != 31u) {
        return false;
    }
    output->protocolVersion = (uint16_t)values[0];
    output->bitstreamVersion = (uint16_t)values[1];
    output->payloadVersion = (uint16_t)values[2];
    output->capabilityFlags = values[3];
    output->maxPacketSize = values[4];
    return true;
}

static bool buildLocalCapabilities(PML_CONNECTION_CONTEXT ctx, LI_PYROWAVE_CAPABILITIES* local) {
    if (ctx == NULL || !IS_SUNSHINE_CTX(ctx) || ctx->AppVersionQuad[0] < 7 ||
            !(ctx->StreamConfig.supportedVideoFormats & VIDEO_FORMAT_PYROWAVE) ||
            !(ctx->VideoCallbacks.capabilities & CAPABILITY_PYROWAVE) ||
            ctx->VideoCallbacks.pyrowave.version != LI_PYROWAVE_PACKET_CALLBACK_VERSION2 ||
            ctx->VideoCallbacks.pyrowave.submit == NULL ||
            ctx->StreamConfig.width < 2 || ctx->StreamConfig.width > 16384 ||
            ctx->StreamConfig.height < 2 || ctx->StreamConfig.height > 16384 ||
            (ctx->StreamConfig.width & 1) || (ctx->StreamConfig.height & 1) ||
            ctx->StreamConfig.dynamicRangeMode != DYNAMIC_RANGE_MODE_SDR ||
            ctx->StreamConfig.colorSpace != COLORSPACE_REC_709 ||
            (ctx->StreamConfig.colorRange != COLOR_RANGE_LIMITED &&
             ctx->StreamConfig.colorRange != COLOR_RANGE_FULL) ||
            ctx->StreamConfig.packetSize <= 16 + 2 * (int)LI_PYROWAVE_WIRE_HEADER_SIZE ||
            ctx->StreamConfig.packetSize > (int)LI_PYROWAVE_MAX_PACKET_SIZE + 16) {
        return false;
    }
    memset(local, 0, sizeof(*local));
    local->protocolVersion = LI_PYROWAVE_PROTOCOL_VERSION;
    local->bitstreamVersion = LI_PYROWAVE_BITSTREAM_VERSION;
    local->payloadVersion = LI_PYROWAVE_PAYLOAD_VERSION;
    local->capabilityFlags = LI_PYROWAVE_REQUIRED_SDR_BASE_CAPABILITIES |
        (ctx->StreamConfig.colorRange == COLOR_RANGE_FULL ? LI_PYROWAVE_CAPABILITY_YUV_FULL_RANGE :
                                                         LI_PYROWAVE_CAPABILITY_YUV_LIMITED_RANGE);
    // StreamConfig.packetSize excludes RTP; subtract only the 16-byte NV header.
    local->maxPacketSize = (uint32_t)(ctx->StreamConfig.packetSize - 16);
    return true;
}

bool negotiatePyrowaveSdrCtx(PML_CONNECTION_CONTEXT ctx, const char* sdp, size_t length) {
    LI_PYROWAVE_CAPABILITIES server, local, negotiated;
    if (ctx == NULL) {
        return false;
    }
    memset(&ctx->NegotiatedPyrowave, 0, sizeof(ctx->NegotiatedPyrowave));
    if (!LiParsePyrowaveServerSdp(sdp, length, &server) || !buildLocalCapabilities(ctx, &local) ||
            LiPyrowaveNegotiate(&server, &local, local.capabilityFlags, &negotiated) !=
                LI_PYROWAVE_NEGOTIATION_OK ||
            negotiated.maxPacketSize <= 2u * LI_PYROWAVE_WIRE_HEADER_SIZE) {
        return false;
    }
    ctx->NegotiatedPyrowave = negotiated;
    return true;
}

bool isPyrowaveSdrNegotiatedCtx(PML_CONNECTION_CONTEXT ctx) {
    LI_PYROWAVE_CAPABILITIES local;
    if (ctx == NULL || ctx->NegotiatedVideoFormat != VIDEO_FORMAT_PYROWAVE ||
            !buildLocalCapabilities(ctx, &local)) {
        return false;
    }
    const LI_PYROWAVE_CAPABILITIES* stored = &ctx->NegotiatedPyrowave;
    return LiPyrowaveValidateCapabilities(stored) && stored->capabilityFlags == local.capabilityFlags &&
        stored->maxPacketSize > 2u * LI_PYROWAVE_WIRE_HEADER_SIZE &&
        stored->maxPacketSize <= local.maxPacketSize;
}
