#include "CursorStream.h"

#include <stdio.h>
#include <string.h>

typedef struct _CAPTURED_UPDATE {
    int count;
    LI_CURSOR_UPDATE update;
    uint8_t pixels[32];
} CAPTURED_UPDATE;

static int failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while (0)

static void captureUpdate(const LI_CURSOR_UPDATE* update, void* context) {
    CAPTURED_UPDATE* captured = context;

    captured->count++;
    captured->update = *update;
    if (update->pixels != NULL && update->pixelDataLength <= sizeof(captured->pixels)) {
        memcpy(captured->pixels, update->pixels, update->pixelDataLength);
        captured->update.pixels = captured->pixels;
    }
}

static void testVisibilityOnly(void) {
    static const uint8_t packet[] = {
        0x01, 0x02, 0x1c, 0x00,
        0x44, 0x33, 0x22, 0x11,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    CURSOR_STREAM_STATE state;
    CAPTURED_UPDATE captured = {0};

    initializeCursorStreamState(&state);
    processCursorStreamPacket(&state, packet, sizeof(packet), captureUpdate, &captured);

    CHECK(captured.count == 1);
    CHECK(captured.update.flags == LI_CURSOR_UPDATE_FLAG_VISIBLE);
    CHECK(captured.update.shapeId == 0x11223344);
    CHECK(captured.update.pixelDataLength == 0);
    destroyCursorStreamState(&state);
}

static void testSingleChunkShape(void) {
    static const uint8_t packet[] = {
        0x01, 0x01, 0x1c, 0x00,
        0x04, 0x03, 0x02, 0x01,
        0x02, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00,
        0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80,
    };
    static const uint8_t expectedPixels[] = {
        0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80,
    };
    CURSOR_STREAM_STATE state;
    CAPTURED_UPDATE captured = {0};

    initializeCursorStreamState(&state);
    processCursorStreamPacket(&state, packet, sizeof(packet), captureUpdate, &captured);

    CHECK(captured.count == 1);
    CHECK(captured.update.flags == LI_CURSOR_UPDATE_FLAG_SHAPE);
    CHECK(captured.update.shapeId == 0x01020304);
    CHECK(captured.update.width == 2);
    CHECK(captured.update.height == 1);
    CHECK(captured.update.hotspotX == 1);
    CHECK(captured.update.hotspotY == 0);
    CHECK(captured.update.pixelDataLength == sizeof(expectedPixels));
    CHECK(memcmp(captured.update.pixels, expectedPixels, sizeof(expectedPixels)) == 0);
    destroyCursorStreamState(&state);
}

static void testMultiChunkShape(void) {
    static const uint8_t first[] = {
        0x01, 0x03, 0x1c, 0x00,
        0xdd, 0xcc, 0xbb, 0xaa,
        0x02, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x01, 0x00,
        0x10, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    };
    static const uint8_t second[] = {
        0x01, 0x03, 0x1c, 0x00,
        0xdd, 0xcc, 0xbb, 0xaa,
        0x02, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x01, 0x00,
        0x10, 0x00, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x00,
        0x0a, 0x00, 0x00, 0x00,
        0x06, 0x07, 0x08, 0x09, 0x0a,
        0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    static const uint8_t expectedPixels[] = {
        0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b,
        0x0c, 0x0d, 0x0e, 0x0f,
    };
    CURSOR_STREAM_STATE state;
    CAPTURED_UPDATE captured = {0};

    initializeCursorStreamState(&state);
    processCursorStreamPacket(&state, first, sizeof(first), captureUpdate, &captured);
    CHECK(captured.count == 0);
    processCursorStreamPacket(&state, second, sizeof(second), captureUpdate, &captured);

    CHECK(captured.count == 1);
    CHECK(captured.update.flags ==
          (LI_CURSOR_UPDATE_FLAG_SHAPE | LI_CURSOR_UPDATE_FLAG_VISIBLE));
    CHECK(captured.update.shapeId == 0xaabbccdd);
    CHECK(captured.update.pixelDataLength == sizeof(expectedPixels));
    CHECK(memcmp(captured.update.pixels, expectedPixels, sizeof(expectedPixels)) == 0);
    destroyCursorStreamState(&state);
}

static void testOutOfOrderChunkRecovers(void) {
    static const uint8_t first[] = {
        0x01, 0x01, 0x1c, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x02, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00,
        0xaa, 0xbb, 0xcc, 0xdd,
    };
    static const uint8_t second[] = {
        0x01, 0x01, 0x1c, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x02, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00,
        0x11, 0x22, 0x33, 0x44,
    };
    CURSOR_STREAM_STATE state;
    CAPTURED_UPDATE captured = {0};

    initializeCursorStreamState(&state);
    processCursorStreamPacket(&state, second, sizeof(second), captureUpdate, &captured);
    CHECK(captured.count == 0);
    processCursorStreamPacket(&state, first, sizeof(first), captureUpdate, &captured);
    processCursorStreamPacket(&state, second, sizeof(second), captureUpdate, &captured);
    CHECK(captured.count == 1);
    destroyCursorStreamState(&state);
}

static void testMalformedPacketResetsPartialShape(void) {
    static const uint8_t first[] = {
        0x01, 0x01, 0x1c, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x02, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00,
        0xaa, 0xbb, 0xcc, 0xdd,
    };
    static const uint8_t malformed[] = {
        0x01, 0x81, 0x1c, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    static const uint8_t second[] = {
        0x01, 0x01, 0x1c, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x02, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00,
        0x11, 0x22, 0x33, 0x44,
    };
    CURSOR_STREAM_STATE state;
    CAPTURED_UPDATE captured = {0};

    initializeCursorStreamState(&state);
    processCursorStreamPacket(&state, first, sizeof(first), captureUpdate, &captured);
    processCursorStreamPacket(&state, malformed, sizeof(malformed), captureUpdate, &captured);
    processCursorStreamPacket(&state, second, sizeof(second), captureUpdate, &captured);

    CHECK(captured.count == 0);
    CHECK(state.pixels == NULL);
    destroyCursorStreamState(&state);
}

int main(void) {
    testVisibilityOnly();
    testSingleChunkShape();
    testMultiChunkShape();
    testOutOfOrderChunkRecovers();
    testMalformedPacketResetsPartialShape();

    if (failures != 0) {
        fprintf(stderr, "%d cursor protocol golden-vector checks failed\n", failures);
        return 1;
    }

    puts("cursor protocol golden vectors passed");
    return 0;
}
