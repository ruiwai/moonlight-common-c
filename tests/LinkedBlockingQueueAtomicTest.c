#include "LinkedBlockingQueue.h"

#include <errno.h>
#include <pthread.h>

typedef struct _TEST_ITEM {
    int value;
} TEST_ITEM;

typedef enum _PRODUCER_KIND {
    PRODUCER_TAIL_MERGE,
    PRODUCER_OLDEST_EVICTION,
} PRODUCER_KIND;

typedef struct _INTERLEAVING_CONTEXT {
    LINKED_BLOCKING_QUEUE queue;
    pthread_mutex_t gateMutex;
    pthread_cond_t gateCondition;
    bool predicateEntered;
    bool producerAttempting;
    bool releasePredicate;
    int producerCallbackCount;
    int consumerResult;
    int producerResult;
    void* consumerData;
    void* evictedData;
    TEST_ITEM offeredItem;
    LINKED_BLOCKING_QUEUE_ENTRY offeredEntry;
    PRODUCER_KIND producerKind;
} INTERLEAVING_CONTEXT;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL: %s (line %d)\n", #condition, __LINE__); return EXIT_FAILURE; } } while (0)

static bool classifyAndPause(void* data, void* context) {
    INTERLEAVING_CONTEXT* test = context;
    TEST_ITEM* item = data;

    pthread_mutex_lock(&test->gateMutex);
    test->predicateEntered = true;
    pthread_cond_broadcast(&test->gateCondition);
    while (!test->releasePredicate) {
        pthread_cond_wait(&test->gateCondition, &test->gateMutex);
    }
    pthread_mutex_unlock(&test->gateMutex);

    return item->value == 7;
}

static bool mutateTailIfCalled(void* tailData, void* offeredData, void* context) {
    INTERLEAVING_CONTEXT* test = context;
    TEST_ITEM* tail = tailData;

    (void)offeredData;
    test->producerCallbackCount++;
    tail->value = 99;
    return true;
}

static bool evictAnyItem(void* data, void* context) {
    INTERLEAVING_CONTEXT* test = context;

    (void)data;
    test->producerCallbackCount++;
    return true;
}

static void* consumerThread(void* context) {
    INTERLEAVING_CONTEXT* test = context;

    test->consumerResult = LbqPollQueueElementIf(&test->queue,
                                                 classifyAndPause,
                                                 test,
                                                 &test->consumerData);
    return NULL;
}

static void* producerThread(void* context) {
    INTERLEAVING_CONTEXT* test = context;

    pthread_mutex_lock(&test->gateMutex);
    while (!test->predicateEntered) {
        pthread_cond_wait(&test->gateCondition, &test->gateMutex);
    }
    test->producerAttempting = true;
    pthread_cond_broadcast(&test->gateCondition);
    pthread_mutex_unlock(&test->gateMutex);

    if (test->producerKind == PRODUCER_TAIL_MERGE) {
        test->producerResult = LbqOfferQueueItemWithTailMerge(&test->queue,
                                                             &test->offeredItem,
                                                             &test->offeredEntry,
                                                             mutateTailIfCalled,
                                                             test);
    }
    else {
        test->producerResult = LbqOfferQueueItemWithOldestEviction(&test->queue,
                                                                  &test->offeredItem,
                                                                  &test->offeredEntry,
                                                                  evictAnyItem,
                                                                  test,
                                                                  &test->evictedData);
    }

    return NULL;
}

static int runInterleaving(PRODUCER_KIND producerKind) {
    INTERLEAVING_CONTEXT test = {0};
    LINKED_BLOCKING_QUEUE_ENTRY candidateEntry;
    TEST_ITEM candidateItem = {7};
    pthread_t consumer;
    pthread_t producer;
    void* remainingData = NULL;

    test.producerKind = producerKind;
    test.offeredItem.value = 11;

    CHECK(pthread_mutex_init(&test.gateMutex, NULL) == 0);
    CHECK(pthread_cond_init(&test.gateCondition, NULL) == 0);
    CHECK(LbqInitializeLinkedBlockingQueue(&test.queue, 1) == 0);
    CHECK(LbqOfferQueueItem(&test.queue, &candidateItem, &candidateEntry) == LBQ_SUCCESS);

    CHECK(pthread_create(&consumer, NULL, consumerThread, &test) == 0);
    CHECK(pthread_create(&producer, NULL, producerThread, &test) == 0);

    pthread_mutex_lock(&test.gateMutex);
    while (!test.producerAttempting) {
        pthread_cond_wait(&test.gateCondition, &test.gateMutex);
    }

    // The classifier is paused while holding the queue mutex, so the producer
    // cannot enter its merge or eviction callback until removal is complete.
    CHECK(pthread_mutex_trylock(&test.queue.mutex) == EBUSY);
    CHECK(test.producerCallbackCount == 0);
    test.releasePredicate = true;
    pthread_cond_broadcast(&test.gateCondition);
    pthread_mutex_unlock(&test.gateMutex);

    CHECK(pthread_join(consumer, NULL) == 0);
    CHECK(pthread_join(producer, NULL) == 0);

    CHECK(test.consumerResult == LBQ_SUCCESS);
    CHECK(test.consumerData == &candidateItem);
    CHECK(candidateItem.value == 7);
    CHECK(test.producerResult == LBQ_SUCCESS);
    CHECK(test.producerCallbackCount == 0);
    CHECK(test.evictedData == NULL);

    CHECK(LbqPollQueueElement(&test.queue, &remainingData) == LBQ_SUCCESS);
    CHECK(remainingData == &test.offeredItem);

    LbqSignalQueueShutdown(&test.queue);
    CHECK(LbqDestroyLinkedBlockingQueue(&test.queue) == NULL);
    CHECK(pthread_cond_destroy(&test.gateCondition) == 0);
    CHECK(pthread_mutex_destroy(&test.gateMutex) == 0);
    return 0;
}

static bool valueIsSeven(void* data, void* context) {
    (void)context;
    return ((TEST_ITEM*)data)->value == 7;
}

static int testQueueEdges(void) {
    LINKED_BLOCKING_QUEUE queue;
    LINKED_BLOCKING_QUEUE_ENTRY entries[4];
    TEST_ITEM items[4] = {{1}, {7}, {7}, {2}};
    void* data = NULL;
    void* evicted = NULL;
    CHECK(LbqInitializeLinkedBlockingQueue(&queue, 3) == 0);
    for (int i = 0; i < 3; i++)
        CHECK(LbqOfferQueueItem(&queue, &items[i], &entries[i]) == LBQ_SUCCESS);
    CHECK(LbqOfferQueueItemWithOldestEviction(&queue, &items[3], &entries[3],
          valueIsSeven, NULL, &evicted) == LBQ_SUCCESS);
    CHECK(evicted == &items[1]);
    CHECK(LbqGetItemCount(&queue) == 3);
    CHECK(LbqPollQueueElementIf(&queue, valueIsSeven, NULL, &data) == LBQ_NO_ELEMENT);
    CHECK(LbqPollQueueElement(&queue, &data) == LBQ_SUCCESS && data == &items[0]);
    CHECK(LbqPollQueueElementIf(&queue, valueIsSeven, NULL, &data) == LBQ_SUCCESS);
    CHECK(data == &items[2]);
    CHECK(LbqPollQueueElement(&queue, &data) == LBQ_SUCCESS && data == &items[3]);
    LbqSignalQueueDrain(&queue);
    CHECK(LbqOfferQueueItemWithTailMerge(&queue, &items[0], &entries[0],
          NULL, NULL) == LBQ_INTERRUPTED);
    CHECK(LbqOfferQueueItemWithOldestEviction(&queue, &items[0], &entries[0],
          valueIsSeven, NULL, &evicted) == LBQ_INTERRUPTED && evicted == NULL);
    CHECK(LbqPollQueueElementIf(&queue, valueIsSeven, NULL, &data) == LBQ_INTERRUPTED);
    CHECK(LbqDestroyLinkedBlockingQueue(&queue) == NULL);
    return 0;
}

int main(void) {
    int result;

    result = testQueueEdges();
    if (result != 0) return result;
    result = runInterleaving(PRODUCER_TAIL_MERGE);
    if (result != 0) {
        return result;
    }

    return runInterleaving(PRODUCER_OLDEST_EVICTION);
}
