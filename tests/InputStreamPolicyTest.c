#include "InputStreamPolicy.h"

static int version;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL: %s (line %d)\n", #condition, __LINE__); return EXIT_FAILURE; } } while (0)

int main(void)
{
    version = 7;

    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_KEYBOARD,
                                    LE32(KEY_DOWN_EVENT_MAGIC)));
    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_KEYBOARD,
                                    LE32(KEY_UP_EVENT_MAGIC)));
    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                    LE32(SCROLL_MAGIC_GEN5)));
    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                    LE32(SS_HSCROLL_MAGIC)));

    CHECK(!inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                     LE32(MOUSE_MOVE_REL_MAGIC_GEN5)));
    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                    LE32(MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5)));
    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                    LE32(MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5)));
    CHECK(!inputPacketIsFlushBarrier(version, CTRL_CHANNEL_GENERIC,
                                     LE32(SCROLL_MAGIC_GEN5)));

    version = 4;
    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                    LE32(SCROLL_MAGIC)));
    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                    LE32(BUTTON_ACTION_PRESS)));
    CHECK(inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                    LE32(BUTTON_ACTION_RELEASE)));
    CHECK(!inputPacketIsFlushBarrier(version, CTRL_CHANNEL_MOUSE,
                                     LE32(SCROLL_MAGIC_GEN5)));

    return 0;
}
