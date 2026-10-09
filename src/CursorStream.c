#include "CursorStream.h"

#include <stdlib.h>
#include <string.h>

#define SS_CURSOR_FLAG_SHAPE 0x01
#define SS_CURSOR_FLAG_VISIBLE 0x02
#define SS_CURSOR_MAX_WIDTH 256
#define SS_CURSOR_MAX_HEIGHT 256
#define SS_CURSOR_MAX_BYTES (SS_CURSOR_MAX_WIDTH * SS_CURSOR_MAX_HEIGHT * 4)

#define CURSOR_WIRE_HEADER_SIZE 28

static uint16_t readLe16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t readLe32(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

void initializeCursorStreamState(PCURSOR_STREAM_STATE state) {
    memset(state, 0, sizeof(*state));
}

void destroyCursorStreamState(PCURSOR_STREAM_STATE state) {
    free(state->pixels);
    memset(state, 0, sizeof(*state));
}

void processCursorStreamPacket(PCURSOR_STREAM_STATE state,
                               const uint8_t* payload,
                               int payloadLength,
                               CursorStreamUpdateCallback callback,
                               void* context) {
    LI_CURSOR_UPDATE update;
    uint8_t flags;
    uint16_t headerSize;
    uint32_t shapeId;
    uint16_t width;
    uint16_t height;
    int16_t hotspotX;
    int16_t hotspotY;
    uint32_t totalSize;
    uint32_t offset;
    uint16_t chunkSize;
    bool hasShape;
    bool visible;

    if (state == NULL || payload == NULL || callback == NULL ||
            payloadLength < CURSOR_WIRE_HEADER_SIZE) {
        return;
    }

    flags = payload[1];
    if (payload[0] != CURSOR_STREAM_PROTOCOL_VERSION ||
            (flags & ~(SS_CURSOR_FLAG_SHAPE | SS_CURSOR_FLAG_VISIBLE)) != 0) {
        destroyCursorStreamState(state);
        return;
    }

    headerSize = readLe16(payload + 2);
    shapeId = readLe32(payload + 4);
    width = readLe16(payload + 8);
    height = readLe16(payload + 10);
    hotspotX = (int16_t)readLe16(payload + 12);
    hotspotY = (int16_t)readLe16(payload + 14);
    totalSize = readLe32(payload + 16);
    offset = readLe32(payload + 20);
    chunkSize = readLe16(payload + 24);
    hasShape = (flags & SS_CURSOR_FLAG_SHAPE) != 0;
    visible = (flags & SS_CURSOR_FLAG_VISIBLE) != 0;

    if (headerSize != CURSOR_WIRE_HEADER_SIZE ||
            chunkSize != payloadLength - (int)headerSize) {
        destroyCursorStreamState(state);
        return;
    }

    if (!hasShape) {
        if (width != 0 || height != 0 || totalSize != 0 || offset != 0 || chunkSize != 0) {
            destroyCursorStreamState(state);
            return;
        }

        destroyCursorStreamState(state);
        memset(&update, 0, sizeof(update));
        update.flags = visible ? LI_CURSOR_UPDATE_FLAG_VISIBLE : 0;
        update.shapeId = shapeId;
        callback(&update, context);
        return;
    }

    if (width == 0 || width > SS_CURSOR_MAX_WIDTH ||
            height == 0 || height > SS_CURSOR_MAX_HEIGHT ||
            hotspotX < 0 || hotspotX >= (int16_t)width ||
            hotspotY < 0 || hotspotY >= (int16_t)height ||
            totalSize == 0 || totalSize > SS_CURSOR_MAX_BYTES ||
            totalSize != (uint32_t)width * (uint32_t)height * 4u ||
            chunkSize == 0 || offset > totalSize || chunkSize > totalSize - offset) {
        destroyCursorStreamState(state);
        return;
    }

    if (offset == 0) {
        destroyCursorStreamState(state);
        state->pixels = malloc(totalSize);
        if (state->pixels == NULL) {
            return;
        }

        state->shapeId = shapeId;
        state->totalSize = totalSize;
        state->width = width;
        state->height = height;
        state->hotspotX = hotspotX;
        state->hotspotY = hotspotY;
        state->visible = visible;
    }

    if (state->pixels == NULL ||
            state->shapeId != shapeId ||
            state->totalSize != totalSize ||
            state->width != width ||
            state->height != height ||
            state->hotspotX != hotspotX ||
            state->hotspotY != hotspotY ||
            state->visible != visible ||
            state->receivedSize != offset) {
        destroyCursorStreamState(state);
        return;
    }

    memcpy(state->pixels + offset, payload + headerSize, chunkSize);
    state->receivedSize += chunkSize;
    if (state->receivedSize != state->totalSize) {
        return;
    }

    memset(&update, 0, sizeof(update));
    update.flags = LI_CURSOR_UPDATE_FLAG_SHAPE |
                   (state->visible ? LI_CURSOR_UPDATE_FLAG_VISIBLE : 0);
    update.shapeId = state->shapeId;
    update.width = state->width;
    update.height = state->height;
    update.hotspotX = state->hotspotX;
    update.hotspotY = state->hotspotY;
    update.pixels = state->pixels;
    update.pixelDataLength = state->totalSize;
    callback(&update, context);
    destroyCursorStreamState(state);
}
