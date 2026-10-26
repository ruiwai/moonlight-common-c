#pragma once

#include "Limelight-internal.h"

static inline bool inputPacketIsFlushBarrier(int appVersionMajor, uint8_t channelId, uint32_t packetMagic)
{
    if (channelId == CTRL_CHANNEL_KEYBOARD) {
        return true;
    }

    if (channelId != CTRL_CHANNEL_MOUSE) {
        return false;
    }

    if (packetMagic == LE32(KEY_DOWN_EVENT_MAGIC) ||
            packetMagic == LE32(KEY_UP_EVENT_MAGIC)) {
        return true;
    }

    const uint32_t scrollMagic = appVersionMajor >= 5 ?
        LE32(SCROLL_MAGIC_GEN5) : LE32(SCROLL_MAGIC);
    const uint32_t mouseButtonDownMagic = appVersionMajor >= 5 ?
        LE32(MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5) : LE32(BUTTON_ACTION_PRESS);
    const uint32_t mouseButtonUpMagic = appVersionMajor >= 5 ?
        LE32(MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5) : LE32(BUTTON_ACTION_RELEASE);

    return packetMagic == scrollMagic ||
           packetMagic == LE32(SS_HSCROLL_MAGIC) ||
           packetMagic == mouseButtonDownMagic ||
           packetMagic == mouseButtonUpMagic;
}
