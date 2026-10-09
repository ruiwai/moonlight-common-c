#pragma once

#include "Limelight.h"

#define CURSOR_STREAM_PROTOCOL_VERSION 1

typedef struct _CURSOR_STREAM_STATE {
    uint8_t* pixels;
    uint32_t shapeId;
    uint32_t totalSize;
    uint32_t receivedSize;
    uint16_t width;
    uint16_t height;
    int16_t hotspotX;
    int16_t hotspotY;
    bool visible;
} CURSOR_STREAM_STATE, *PCURSOR_STREAM_STATE;

typedef void(*CursorStreamUpdateCallback)(const LI_CURSOR_UPDATE* update, void* context);

void initializeCursorStreamState(PCURSOR_STREAM_STATE state);
void destroyCursorStreamState(PCURSOR_STREAM_STATE state);
void processCursorStreamPacket(PCURSOR_STREAM_STATE state,
                               const uint8_t* payload,
                               int payloadLength,
                               CursorStreamUpdateCallback callback,
                               void* context);
