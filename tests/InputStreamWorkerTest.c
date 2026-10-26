#define PltGetMillis InputStreamWorkerTestGetMillis
#define PltSleepMs InputStreamWorkerTestSleepMs
#define flushInputOnControlStreamCtx InputStreamWorkerTestFlushCtx
#define sendInputPacketOnControlStreamCtx InputStreamWorkerTestSendCtx
#define flushInputOnControlStream InputStreamWorkerTestFlush
#define sendInputPacketOnControlStream InputStreamWorkerTestSend

// Include the production implementation so this test exercises the real input
// worker while replacing only its clock and control-stream boundaries.
#include "../src/InputStream.c"

#undef PltGetMillis
#undef PltSleepMs
#undef flushInputOnControlStreamCtx
#undef sendInputPacketOnControlStreamCtx
#undef flushInputOnControlStream
#undef sendInputPacketOnControlStream

ML_CONNECTION_CONTEXT gConnectionContext;
static ML_CONNECTION_CONTEXT testConnection;
static PML_INPUT_STREAM_CONTEXT ctx = &testConnection.inputContext;
static PML_CONNECTION_CONTEXT threadConnection;
PML_CONNECTION_CONTEXT LiGetThreadConnectionContext(void) { return threadConnection; }
void LiSetThreadConnectionContext(PML_CONNECTION_CONTEXT connection) { threadConnection = connection; }
#define inputSendThread (ctx->inputSendThread)
#define packetQueue (ctx->packetQueue)
#define packetHolderFreeList (ctx->packetHolderFreeList)
#define batchedInputMutex (ctx->batchedInputMutex)
#define initialized (ctx->initialized)
#define encryptedControlStream (ctx->encryptedControlStream)

typedef struct _SENT_INPUT {
    uint32_t magic;
    short deltaX;
    short deltaY;
    short keyCode;
    short width, height;
    short scrollAmount;
    uint8_t button;
    bool moreData;
    int sleepCountBeforeSend;
} SENT_INPUT;

typedef enum _TEST_SCENARIO {
    TEST_PREQUEUED,
    TEST_MOUSE_BUTTON_BARRIER,
    TEST_KEYBOARD_BARRIER,
    TEST_WHEEL_BARRIER,
    TEST_HORIZONTAL_REVERSAL,
    TEST_ARITHMETIC_OVERFLOW,
    TEST_CONTINUOUS_SAME_DIRECTION,
    TEST_QUEUE_COMPRESSION,
    TEST_SATURATED_BUTTON_RELEASE,
    TEST_SATURATED_KEY_UP,
    TEST_SATURATED_VERTICAL_WHEEL,
    TEST_SATURATED_HORIZONTAL_WHEEL,
    TEST_SEMANTIC_ONLY_REJECTION,
} TEST_SCENARIO;

static SENT_INPUT sentInputs[MAX_QUEUED_INPUT_PACKETS + 1];
static int sentInputCount;
static int enqueueResult;
static int sleepCount;
static int flushCount;
static uint64_t fakeNowMs;
static TEST_SCENARIO testScenario;
static int expectedSendCount;

uint64_t InputStreamWorkerTestGetMillis(void) {
    return fakeNowMs;
}

uint64_t LiGetMillis(void) { return fakeNowMs; }

void InputStreamWorkerTestSleepMs(int ms) {
    sleepCount++;
    fakeNowMs += (uint64_t)ms;
}

void InputStreamWorkerTestFlush(void) {
    flushCount++;
}

int InputStreamWorkerTestSend(unsigned char* data, int length, uint8_t channelId,
                              uint32_t flags, bool moreData) {
    NV_INPUT_HEADER* header = (NV_INPUT_HEADER*)data;
    SENT_INPUT* sent;

    (void)length;
    (void)channelId;
    (void)flags;

    if (sentInputCount >= (int)(sizeof(sentInputs) / sizeof(sentInputs[0]))) {
        return -1;
    }

    sent = &sentInputs[sentInputCount++];
    sent->magic = header->magic;
    sent->moreData = moreData;
    sent->sleepCountBeforeSend = sleepCount;

    if (header->magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5)) {
        NV_REL_MOUSE_MOVE_PACKET* mouse = (NV_REL_MOUSE_MOVE_PACKET*)data;
        sent->deltaX = (short)BE16(mouse->deltaX);
        sent->deltaY = (short)BE16(mouse->deltaY);
    }
    else if (header->magic == LE32(MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5) ||
            header->magic == LE32(MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5)) {
        sent->button = ((NV_MOUSE_BUTTON_PACKET*)data)->button;
    }
    else if (header->magic == LE32(KEY_UP_EVENT_MAGIC)) {
        sent->keyCode = LE16(((NV_KEYBOARD_PACKET*)data)->keyCode);
    }
    else if (header->magic == LE32(SCROLL_MAGIC_GEN5)) {
        sent->scrollAmount = (short)BE16(((NV_SCROLL_PACKET*)data)->scrollAmt1);
    }
    else if (header->magic == LE32(SS_HSCROLL_MAGIC)) {
        sent->scrollAmount = (short)BE16(((SS_HSCROLL_PACKET*)data)->scrollAmount);
    }

    if (header->magic == LE32(MOUSE_MOVE_ABS_MAGIC)) {
        NV_ABS_MOUSE_MOVE_PACKET* mouse = (NV_ABS_MOUSE_MOVE_PACKET*)data;
        sent->deltaX = (short)BE16(mouse->x);
        sent->deltaY = (short)BE16(mouse->y);
        sent->width = (short)BE16(mouse->width);
        sent->height = (short)BE16(mouse->height);
    }
    if (testScenario == TEST_PREQUEUED) {
        if (sentInputCount == expectedSendCount) inputSendThread.cancelled = true;
        return 0;
    }

    // Queue the barrier only after the first motion has been transmitted. This
    // puts the next motion inside the worker's one-millisecond throttle window.
    if (sentInputCount == 1) {
        if (testScenario == TEST_QUEUE_COMPRESSION) {
            inputSendThread.cancelled = true;
        }
        else if (testScenario == TEST_SATURATED_BUTTON_RELEASE ||
                 testScenario == TEST_SATURATED_KEY_UP ||
                 testScenario == TEST_SATURATED_VERTICAL_WHEEL ||
                 testScenario == TEST_SATURATED_HORIZONTAL_WHEEL) {
            // This scenario queues all inputs before starting the worker.
        }
        else {
            enqueueResult = LiSendMouseMoveEventCtx(ctx, 6, -2);
        }
        if (enqueueResult == LBQ_SUCCESS &&
                testScenario != TEST_QUEUE_COMPRESSION &&
                testScenario != TEST_SATURATED_BUTTON_RELEASE &&
                testScenario != TEST_SATURATED_KEY_UP &&
                testScenario != TEST_SATURATED_VERTICAL_WHEEL &&
                testScenario != TEST_SATURATED_HORIZONTAL_WHEEL) {
            if (testScenario == TEST_MOUSE_BUTTON_BARRIER) {
                enqueueResult = LiSendMouseButtonEventCtx(ctx, BUTTON_ACTION_PRESS, BUTTON_LEFT);
            }
            else if (testScenario == TEST_KEYBOARD_BARRIER) {
                enqueueResult = LiSendKeyboardEventCtx(ctx, 0x41, KEY_ACTION_UP, 0);
            }
            else if (testScenario == TEST_WHEEL_BARRIER) {
                enqueueResult = LiSendScrollEventCtx(ctx, -1);
            }
            else if (testScenario == TEST_HORIZONTAL_REVERSAL) {
                enqueueResult = LiSendMouseMoveEventCtx(ctx, -2, 3);
            }
            else if (testScenario == TEST_ARITHMETIC_OVERFLOW) {
                enqueueResult = LiSendMouseMoveEventCtx(ctx, INT16_MAX, 3);
            }
            else if (testScenario == TEST_CONTINUOUS_SAME_DIRECTION) {
                enqueueResult = LiSendMouseMoveEventCtx(ctx, 5, 3);
            }
        }
    }
    else if (testScenario == TEST_HORIZONTAL_REVERSAL && sentInputCount == 3) {
        enqueueResult = LiSendMouseMoveEventCtx(ctx, -3, 4);
    }
    else if (testScenario == TEST_ARITHMETIC_OVERFLOW && sentInputCount == 3) {
        enqueueResult = LiSendMouseMoveEventCtx(ctx, 1, 4);
    }
    else if (sentInputCount == expectedSendCount) {
        inputSendThread.cancelled = true;
    }

    return 0;
}

void InputStreamWorkerTestFlushCtx(PML_CONTROL_STREAM_CONTEXT control) {
    (void)control;
    InputStreamWorkerTestFlush();
}

int InputStreamWorkerTestSendCtx(PML_CONTROL_STREAM_CONTEXT control, unsigned char* data,
                                int length, uint8_t channelId, uint32_t flags, bool moreData) {
    if (control != &ctx->connectionContext->controlContext) return -1;
    return InputStreamWorkerTestSend(data, length, channelId, flags, moreData);
}

bool PltEncryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm, int flags,
                       unsigned char* key, int keyLength,
                       unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength,
                       unsigned char* inputData, int inputDataLength,
                       unsigned char* outputData, int* outputDataLength) {
    (void)ctx;
    (void)algorithm;
    (void)flags;
    (void)key;
    (void)keyLength;
    (void)iv;
    (void)ivLength;
    (void)tag;
    (void)tagLength;
    (void)inputData;
    (void)inputDataLength;
    (void)outputData;
    (void)outputDataLength;
    return false;
}

bool isControlDataInTransitCtx(PML_CONTROL_STREAM_CONTEXT control) {
    (void)control;
    return false;
}

static void freeQueueEntries(PLINKED_BLOCKING_QUEUE_ENTRY entry) {
    while (entry != NULL) {
        PLINKED_BLOCKING_QUEUE_ENTRY next = entry->flink;
        free(entry->data);
        entry = next;
    }
}

static void cleanupWorkerState(void) {
    LbqSignalQueueShutdown(&packetQueue);
    freeQueueEntries(LbqDestroyLinkedBlockingQueue(&packetQueue));

    LbqSignalQueueShutdown(&packetHolderFreeList);
    freeQueueEntries(LbqDestroyLinkedBlockingQueue(&packetHolderFreeList));

    PltDeleteMutex(&batchedInputMutex);
}

static int setupWorkerState(TEST_SCENARIO scenario, int sendCount) {
    memset(sentInputs, 0, sizeof(sentInputs));
    sentInputCount = 0;
    enqueueResult = LBQ_SUCCESS;
    sleepCount = 0;
    flushCount = 0;
    fakeNowMs = 1000;
    testScenario = scenario;
    expectedSendCount = sendCount;

    ctx->connectionContext = &testConnection;
    ctx->needsBatchedScroll = false;
    encryptedControlStream = true;
    initialized = true;
    inputSendThread.cancelled = false;

    if (LbqInitializeLinkedBlockingQueue(&packetQueue, MAX_QUEUED_INPUT_PACKETS) != 0) {
        return -1;
    }
    if (LbqInitializeLinkedBlockingQueue(&packetHolderFreeList, MAX_QUEUED_INPUT_PACKETS) != 0) {
        return -1;
    }
    if (PltCreateMutex(&batchedInputMutex) != 0) {
        return -1;
    }

    return 0;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", #condition, __LINE__); \
        result = EXIT_FAILURE; \
        goto Exit; \
    } \
} while (0)

static int testMouseButtonBarrier(void) {
    int result = 0;

    CHECK(setupWorkerState(TEST_MOUSE_BUTTON_BARRIER, 3) == 0);

    CHECK(LiSendMouseMoveEventCtx(ctx, 4, 1) == LBQ_SUCCESS);
    inputSendThreadProc(ctx);

    CHECK(enqueueResult == LBQ_SUCCESS);
    CHECK(sentInputCount == 3);

    CHECK(sentInputs[0].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[0].deltaX == 4);
    CHECK(sentInputs[0].deltaY == 1);

    CHECK(sentInputs[1].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[1].deltaX == 6);
    CHECK(sentInputs[1].deltaY == -2);
    CHECK(!sentInputs[1].moreData);

    CHECK(sentInputs[2].magic == LE32(MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5));
    CHECK(sentInputs[2].button == BUTTON_LEFT);
    CHECK(!sentInputs[2].moreData);

    CHECK(sleepCount == 0);
    CHECK(flushCount == 0);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testKeyboardBarrier(void) {
    int result = 0;

    CHECK(setupWorkerState(TEST_KEYBOARD_BARRIER, 3) == 0);

    CHECK(LiSendMouseMoveEventCtx(ctx, 4, 1) == LBQ_SUCCESS);
    inputSendThreadProc(ctx);

    CHECK(enqueueResult == LBQ_SUCCESS);
    CHECK(sentInputCount == 3);

    CHECK(sentInputs[0].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[0].deltaX == 4);
    CHECK(sentInputs[0].deltaY == 1);

    CHECK(sentInputs[1].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[1].deltaX == 6);
    CHECK(sentInputs[1].deltaY == -2);
    CHECK(!sentInputs[1].moreData);

    CHECK(sentInputs[2].magic == LE32(KEY_UP_EVENT_MAGIC));
    CHECK(sentInputs[2].keyCode == 0x41);
    CHECK(!sentInputs[2].moreData);

    CHECK(sleepCount == 0);
    CHECK(flushCount == 0);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testWheelBarrier(void) {
    int result = 0;

    CHECK(setupWorkerState(TEST_WHEEL_BARRIER, 3) == 0);

    CHECK(LiSendMouseMoveEventCtx(ctx, 4, 1) == LBQ_SUCCESS);
    inputSendThreadProc(ctx);

    CHECK(enqueueResult == LBQ_SUCCESS);
    CHECK(sentInputCount == 3);

    CHECK(sentInputs[0].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[0].deltaX == 4);
    CHECK(sentInputs[0].deltaY == 1);

    CHECK(sentInputs[1].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[1].deltaX == 6);
    CHECK(sentInputs[1].deltaY == -2);
    CHECK(!sentInputs[1].moreData);

    CHECK(sentInputs[2].magic == LE32(SCROLL_MAGIC_GEN5));
    CHECK(sentInputs[2].scrollAmount == -LI_WHEEL_DELTA);
    CHECK(!sentInputs[2].moreData);

    CHECK(sleepCount == 0);
    CHECK(flushCount == 0);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testHorizontalReversal(void) {
    int result = 0;

    CHECK(setupWorkerState(TEST_HORIZONTAL_REVERSAL, 4) == 0);

    CHECK(LiSendMouseMoveEventCtx(ctx, 4, 1) == LBQ_SUCCESS);
    inputSendThreadProc(ctx);

    CHECK(enqueueResult == LBQ_SUCCESS);
    CHECK(sentInputCount == 4);

    CHECK(sentInputs[0].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[0].deltaX == 4);
    CHECK(sentInputs[0].deltaY == 1);

    CHECK(sentInputs[1].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[1].deltaX == 6);
    CHECK(sentInputs[1].deltaY == -2);
    CHECK(!sentInputs[1].moreData);
    CHECK(sentInputs[1].sleepCountBeforeSend == 0);

    CHECK(sentInputs[2].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[2].deltaX == -2);
    CHECK(sentInputs[2].deltaY == 3);
    CHECK(!sentInputs[2].moreData);
    CHECK(sentInputs[2].sleepCountBeforeSend == 0);

    CHECK(sentInputs[3].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[3].deltaX == -3);
    CHECK(sentInputs[3].deltaY == 4);
    CHECK(!sentInputs[3].moreData);
    CHECK(sentInputs[3].sleepCountBeforeSend == 1);

    CHECK(sleepCount == 1);
    CHECK(flushCount == 1);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testArithmeticOverflow(void) {
    int result = 0;

    CHECK(setupWorkerState(TEST_ARITHMETIC_OVERFLOW, 4) == 0);

    CHECK(LiSendMouseMoveEventCtx(ctx, 4, 1) == LBQ_SUCCESS);
    inputSendThreadProc(ctx);

    CHECK(enqueueResult == LBQ_SUCCESS);
    CHECK(sentInputCount == 4);

    CHECK(sentInputs[0].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[0].deltaX == 4);
    CHECK(sentInputs[0].deltaY == 1);

    CHECK(sentInputs[1].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[1].deltaX == 6);
    CHECK(sentInputs[1].deltaY == -2);
    CHECK(!sentInputs[1].moreData);
    CHECK(sentInputs[1].sleepCountBeforeSend == 0);

    CHECK(sentInputs[2].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[2].deltaX == INT16_MAX);
    CHECK(sentInputs[2].deltaY == 3);
    CHECK(!sentInputs[2].moreData);
    CHECK(sentInputs[2].sleepCountBeforeSend == 0);

    CHECK(sentInputs[3].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[3].deltaX == 1);
    CHECK(sentInputs[3].deltaY == 4);
    CHECK(!sentInputs[3].moreData);
    CHECK(sentInputs[3].sleepCountBeforeSend == 1);

    CHECK(sleepCount == 1);
    CHECK(flushCount == 1);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testContinuousSameDirection(void) {
    int result = 0;

    CHECK(setupWorkerState(TEST_CONTINUOUS_SAME_DIRECTION, 2) == 0);

    CHECK(LiSendMouseMoveEventCtx(ctx, 4, 1) == LBQ_SUCCESS);
    inputSendThreadProc(ctx);

    CHECK(enqueueResult == LBQ_SUCCESS);
    CHECK(sentInputCount == 2);

    CHECK(sentInputs[0].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[0].deltaX == 4);
    CHECK(sentInputs[0].deltaY == 1);

    CHECK(sentInputs[1].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[1].deltaX == 11);
    CHECK(sentInputs[1].deltaY == 1);
    CHECK(!sentInputs[1].moreData);
    CHECK(sentInputs[1].sleepCountBeforeSend == 1);

    CHECK(sleepCount == 1);
    CHECK(flushCount == 1);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testQueueCompression(void) {
    int result = 0;
    int i;

    CHECK(setupWorkerState(TEST_QUEUE_COMPRESSION, 1) == 0);

    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS + 1; i++) {
        enqueueResult = LiSendMouseMoveEventCtx(ctx, 1, 1);
        if (enqueueResult != LBQ_SUCCESS) {
            break;
        }
    }

    if (i != MAX_QUEUED_INPUT_PACKETS + 1) {
        fprintf(stderr, "FAIL: motion enqueue %d returned %d\n",
                i + 1, enqueueResult);
        result = __LINE__;
        goto Exit;
    }

    inputSendThreadProc(ctx);

    CHECK(sentInputCount == 1);
    CHECK(sentInputs[0].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
    CHECK(sentInputs[0].deltaX == MAX_QUEUED_INPUT_PACKETS + 1);
    CHECK(sentInputs[0].deltaY == MAX_QUEUED_INPUT_PACKETS + 1);
    CHECK(!sentInputs[0].moreData);
    CHECK(sentInputs[0].sleepCountBeforeSend == 0);
    CHECK(sleepCount == 0);
    CHECK(flushCount == 0);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testSaturatedButtonRelease(void) {
    int result = 0;
    int i;

    CHECK(setupWorkerState(TEST_SATURATED_BUTTON_RELEASE,
                           MAX_QUEUED_INPUT_PACKETS) == 0);

    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS; i++) {
        short deltaX = (i % 2 == 0) ? 1 : -1;
        CHECK(LiSendMouseMoveEventCtx(ctx, deltaX, 0) == LBQ_SUCCESS);
    }

    enqueueResult = LiSendMouseButtonEventCtx(ctx, BUTTON_ACTION_RELEASE, BUTTON_LEFT);
    if (enqueueResult != LBQ_SUCCESS) {
        fprintf(stderr, "FAIL: saturated button release returned %d\n",
                enqueueResult);
        result = __LINE__;
        goto Exit;
    }

    inputSendThreadProc(ctx);

    CHECK(sentInputCount == MAX_QUEUED_INPUT_PACKETS);
    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS - 1; i++) {
        short expectedDeltaX = (i % 2 == 0) ? -1 : 1;
        CHECK(sentInputs[i].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
        CHECK(sentInputs[i].deltaX == expectedDeltaX);
        CHECK(sentInputs[i].deltaY == 0);
        CHECK(!sentInputs[i].moreData);
    }

    CHECK(sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].magic ==
          LE32(MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5));
    CHECK(sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].button == BUTTON_LEFT);
    CHECK(!sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].moreData);
    CHECK(sleepCount == 0);
    CHECK(flushCount == 0);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testSaturatedKeyUp(void) {
    int result = 0;
    int i;

    CHECK(setupWorkerState(TEST_SATURATED_KEY_UP,
                           MAX_QUEUED_INPUT_PACKETS) == 0);

    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS; i++) {
        short deltaX = (i % 2 == 0) ? 1 : -1;
        CHECK(LiSendMouseMoveEventCtx(ctx, deltaX, 0) == LBQ_SUCCESS);
    }

    enqueueResult = LiSendKeyboardEventCtx(ctx, 0x41, KEY_ACTION_UP, 0);
    if (enqueueResult != LBQ_SUCCESS) {
        fprintf(stderr, "FAIL: saturated key-up returned %d\n",
                enqueueResult);
        result = __LINE__;
        goto Exit;
    }

    inputSendThreadProc(ctx);

    CHECK(sentInputCount == MAX_QUEUED_INPUT_PACKETS);
    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS - 1; i++) {
        short expectedDeltaX = (i % 2 == 0) ? -1 : 1;
        CHECK(sentInputs[i].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
        CHECK(sentInputs[i].deltaX == expectedDeltaX);
        CHECK(sentInputs[i].deltaY == 0);
        CHECK(!sentInputs[i].moreData);
    }

    CHECK(sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].magic ==
          LE32(KEY_UP_EVENT_MAGIC));
    CHECK(sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].keyCode == 0x41);
    CHECK(!sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].moreData);
    CHECK(sleepCount == 0);
    CHECK(flushCount == 0);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testSaturatedVerticalWheel(void) {
    int result = 0;
    int i;

    CHECK(setupWorkerState(TEST_SATURATED_VERTICAL_WHEEL,
                           MAX_QUEUED_INPUT_PACKETS) == 0);

    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS; i++) {
        short deltaX = (i % 2 == 0) ? 1 : -1;
        CHECK(LiSendMouseMoveEventCtx(ctx, deltaX, 0) == LBQ_SUCCESS);
    }

    enqueueResult = LiSendScrollEventCtx(ctx, -1);
    if (enqueueResult != LBQ_SUCCESS) {
        fprintf(stderr, "FAIL: saturated vertical wheel returned %d\n",
                enqueueResult);
        result = __LINE__;
        goto Exit;
    }

    inputSendThreadProc(ctx);

    CHECK(sentInputCount == MAX_QUEUED_INPUT_PACKETS);
    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS - 1; i++) {
        short expectedDeltaX = (i % 2 == 0) ? -1 : 1;
        CHECK(sentInputs[i].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
        CHECK(sentInputs[i].deltaX == expectedDeltaX);
        CHECK(sentInputs[i].deltaY == 0);
        CHECK(!sentInputs[i].moreData);
    }

    CHECK(sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].magic ==
          LE32(SCROLL_MAGIC_GEN5));
    CHECK(sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].scrollAmount ==
          -LI_WHEEL_DELTA);
    CHECK(!sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].moreData);
    CHECK(sleepCount == 0);
    CHECK(flushCount == 0);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testSaturatedHorizontalWheel(void) {
    int result = 0;
    int i;

    CHECK(setupWorkerState(TEST_SATURATED_HORIZONTAL_WHEEL,
                           MAX_QUEUED_INPUT_PACKETS) == 0);

    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS; i++) {
        short deltaX = (i % 2 == 0) ? 1 : -1;
        CHECK(LiSendMouseMoveEventCtx(ctx, deltaX, 0) == LBQ_SUCCESS);
    }

    enqueueResult = LiSendHScrollEventCtx(ctx, -1);
    CHECK(enqueueResult == LBQ_SUCCESS);

    inputSendThreadProc(ctx);

    CHECK(sentInputCount == MAX_QUEUED_INPUT_PACKETS);
    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS - 1; i++) {
        short expectedDeltaX = (i % 2 == 0) ? -1 : 1;
        CHECK(sentInputs[i].magic == LE32(MOUSE_MOVE_REL_MAGIC_GEN5));
        CHECK(sentInputs[i].deltaX == expectedDeltaX);
        CHECK(sentInputs[i].deltaY == 0);
        CHECK(!sentInputs[i].moreData);
    }

    CHECK(sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].magic ==
          LE32(SS_HSCROLL_MAGIC));
    CHECK(sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].scrollAmount ==
          -LI_WHEEL_DELTA);
    CHECK(!sentInputs[MAX_QUEUED_INPUT_PACKETS - 1].moreData);
    CHECK(sleepCount == 0);
    CHECK(flushCount == 0);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testSemanticOnlySaturationRejectsWithoutEviction(void) {
    int result = 0;
    int i;

    CHECK(setupWorkerState(TEST_SEMANTIC_ONLY_REJECTION,
                           MAX_QUEUED_INPUT_PACKETS) == 0);

    for (i = 0; i < MAX_QUEUED_INPUT_PACKETS; i++) {
        char action = (i % 2 == 0) ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE;
        CHECK(LiSendMouseButtonEventCtx(ctx, action, BUTTON_LEFT) == LBQ_SUCCESS);
    }

    // With no relative-motion holder available to evict, a new semantic
    // transition must fail explicitly so the caller can surface overload.
    CHECK(LiSendKeyboardEventCtx(ctx, 0x41, KEY_ACTION_UP, 0) == LBQ_BOUND_EXCEEDED);

Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testAbsoluteSnapshots(void) {
    int result = 0;
    CHECK(setupWorkerState(TEST_PREQUEUED, 3) == 0);
    CHECK(LiSendMousePositionEventCtx(ctx, 10, 20, 800, 600) == 0);
    CHECK(LiSendMousePositionEventCtx(ctx, 30, 40, 900, 700) == 0);
    CHECK(LiSendKeyboardEventCtx(ctx, 0x41, KEY_ACTION_UP, 0) == 0);
    CHECK(LiSendMousePositionEventCtx(ctx, 50, 60, 1000, 800) == 0);
    CHECK(LiSendMousePositionEventCtx(ctx, 70, 80, 1100, 900) == 0);
    inputSendThreadProc(ctx);
    CHECK(sentInputCount == 3);
    CHECK(sentInputs[0].magic == LE32(MOUSE_MOVE_ABS_MAGIC));
    CHECK(sentInputs[0].deltaX == 30 && sentInputs[0].deltaY == 40);
    CHECK(sentInputs[0].width == 899 && sentInputs[0].height == 699);
    CHECK(sentInputs[1].magic == LE32(KEY_UP_EVENT_MAGIC));
    CHECK(!sentInputs[1].moreData);
    CHECK(sentInputs[2].magic == LE32(MOUSE_MOVE_ABS_MAGIC));
    CHECK(sentInputs[2].deltaX == 70 && sentInputs[2].deltaY == 80);
    CHECK(sentInputs[2].width == 1099 && sentInputs[2].height == 899);
Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testContextIsolation(void) {
    static ML_CONNECTION_CONTEXT other;
    PML_INPUT_STREAM_CONTEXT first = ctx;
    int result = 0;
    CHECK(setupWorkerState(TEST_PREQUEUED, 2) == 0);
    CHECK(LiSendMouseMoveEventCtx(ctx, 12, 3) == 0);
    CHECK(LiSendMousePositionEventCtx(ctx, 22, 33, 800, 600) == 0);
    ctx = &other.inputContext;
    CHECK(setupWorkerState(TEST_PREQUEUED, 2) == 0);
    ctx->connectionContext = &other;
    AppVersionQuad[0] = 7;
    AppVersionQuad[1] = 1;
    AppVersionQuad[2] = 431;
    AppVersionQuad[3] = -1;
    CHECK(LiSendMouseMoveEventCtx(ctx, -8, -9) == 0);
    CHECK(LiSendMousePositionEventCtx(ctx, 77, 88, 1200, 1000) == 0);
    ctx = first;
    inputSendThreadProc(ctx);
    CHECK(sentInputCount == 2);
    CHECK(sentInputs[0].deltaX == 12 && sentInputs[0].deltaY == 3);
    CHECK(sentInputs[1].deltaX == 22 && sentInputs[1].deltaY == 33);
    CHECK(sentInputs[1].width == 799);
    ctx = &other.inputContext;
    sentInputCount = 0;
    inputSendThreadProc(ctx);
    CHECK(sentInputCount == 2);
    CHECK(sentInputs[0].deltaX == -8 && sentInputs[0].deltaY == -9);
    CHECK(sentInputs[1].deltaX == 77 && sentInputs[1].deltaY == 88);
    CHECK(sentInputs[1].width == 1199);
Exit:
    ctx = &other.inputContext;
    initialized = false;
    cleanupWorkerState();
    ctx = first;
    initialized = false;
    cleanupWorkerState();
    return result;
}

// Bypass producer compression so this checks the worker's own atomic merge.
static int queueRawRelative(short x, short y) {
    PPACKET_HOLDER holder = allocatePacketHolder(ctx, 0);
    if (holder == NULL) return -1;
    holder->channelId = CTRL_CHANNEL_MOUSE;
    holder->enetPacketFlags = ENET_PACKET_FLAG_RELIABLE;
    holder->packet.mouseMoveRel.header.size = BE32(sizeof(NV_REL_MOUSE_MOVE_PACKET) - 4);
    holder->packet.mouseMoveRel.header.magic = LE32(MOUSE_MOVE_REL_MAGIC_GEN5);
    holder->packet.mouseMoveRel.deltaX = BE16(x);
    holder->packet.mouseMoveRel.deltaY = BE16(y);
    return LbqOfferQueueItem(&packetQueue, holder, &holder->entry);
}

static int testWorkerMergeAndSignedLimits(void) {
    int result = 0;
    CHECK(setupWorkerState(TEST_PREQUEUED, 4) == 0);
    CHECK(queueRawRelative(4, 7) == 0);
    CHECK(queueRawRelative(0, -9) == 0); // vertical reversal is not a boundary
    CHECK(LiSendKeyboardEventCtx(ctx, 0x41, KEY_ACTION_UP, 0) == 0);
    CHECK(queueRawRelative(0, INT16_MIN) == 0);
    CHECK(queueRawRelative(0, -1) == 0); // vertical signed16 underflow boundary
    inputSendThreadProc(ctx);
    CHECK(sentInputCount == 4);
    CHECK(sentInputs[0].deltaX == 4 && sentInputs[0].deltaY == -2);
    CHECK(!sentInputs[0].moreData && !sentInputs[1].moreData);
    CHECK(sentInputs[2].deltaY == INT16_MIN && !sentInputs[2].moreData);
    CHECK(sentInputs[3].deltaY == -1 && !sentInputs[3].moreData);
    CHECK(sleepCount == 0);
Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

static int testPenSamplesPreserved(void) {
    int result = 0;
    CHECK(setupWorkerState(TEST_PREQUEUED, 4) == 0);
    // Contact MOVE samples must not collapse; only adjacent matching HOVERs may.
    for (int i = 0; i < 5; i++) {
        PPACKET_HOLDER holder = allocatePacketHolder(ctx, 0);
        CHECK(holder != NULL);
        memset(holder, 0, sizeof(*holder));
        holder->channelId = CTRL_CHANNEL_MOUSE;
        holder->packet.pen.header.size = BE32(sizeof(SS_PEN_PACKET) - 4);
        holder->packet.pen.header.magic = LE32(SS_PEN_MAGIC);
        holder->packet.pen.eventType = i < 2 ? LI_TOUCH_EVENT_MOVE : LI_TOUCH_EVENT_HOVER;
        holder->packet.pen.penButtons = i == 4 ? 1 : 0;
        CHECK(LbqOfferQueueItem(&packetQueue, holder, &holder->entry) == 0);
    }
    inputSendThreadProc(ctx);
    CHECK(sentInputCount == 4);
    for (int i = 0; i < 4; i++) CHECK(sentInputs[i].magic == LE32(SS_PEN_MAGIC));
Exit:
    initialized = false;
    cleanupWorkerState();
    return result;
}

int main(void) {
    int result;

    ctx->connectionContext = &testConnection;
    AppVersionQuad[0] = 7;
    AppVersionQuad[1] = 1;
    AppVersionQuad[2] = 431;
    AppVersionQuad[3] = -1;

    result = testWorkerMergeAndSignedLimits();
    if (result != 0) return result;
    result = testPenSamplesPreserved();
    if (result != 0) return result;
    result = testAbsoluteSnapshots();
    if (result != 0) return result;
    result = testContextIsolation();
    if (result != 0) return result;

    result = testQueueCompression();
    if (result != 0) {
        return result;
    }

    result = testMouseButtonBarrier();
    if (result != 0) {
        return result;
    }

    result = testKeyboardBarrier();
    if (result != 0) {
        return result;
    }

    result = testWheelBarrier();
    if (result != 0) {
        return result;
    }

    result = testHorizontalReversal();
    if (result != 0) {
        return result;
    }

    result = testArithmeticOverflow();
    if (result != 0) {
        return result;
    }

    result = testContinuousSameDirection();
    if (result != 0) {
        return result;
    }

    result = testSaturatedButtonRelease();
    if (result != 0) {
        return result;
    }

    result = testSaturatedKeyUp();
    if (result != 0) {
        return result;
    }

    result = testSaturatedVerticalWheel();
    if (result != 0) {
        return result;
    }

    result = testSaturatedHorizontalWheel();
    if (result != 0) {
        return result;
    }

    return testSemanticOnlySaturationRejectsWithoutEviction();
}
