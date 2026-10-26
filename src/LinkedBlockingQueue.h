#pragma once

#include "Platform.h"
#include "PlatformThreads.h"

#define LBQ_SUCCESS 0
#define LBQ_INTERRUPTED 1
#define LBQ_BOUND_EXCEEDED 2
#define LBQ_NO_ELEMENT 3
#define LBQ_USER_WAKE 4
#define LBQ_MERGED 5

// Callbacks run under the queue mutex; they must not reenter queue APIs.
// LBQ_MERGED leaves the offered item owned by the caller. Successful eviction
// transfers ownership of *evictedData to the caller; failed offers retain data.
typedef bool (*PLBQ_TAIL_MERGE_CALLBACK)(void* tailData, void* offeredData, void* context);
typedef bool (*PLBQ_EVICTION_PREDICATE)(void* data, void* context);
typedef bool (*PLBQ_POLL_PREDICATE)(void* data, void* context);

typedef struct _LINKED_BLOCKING_QUEUE_ENTRY {
    struct _LINKED_BLOCKING_QUEUE_ENTRY* flink;
    struct _LINKED_BLOCKING_QUEUE_ENTRY* blink;
    void* data;
} LINKED_BLOCKING_QUEUE_ENTRY, *PLINKED_BLOCKING_QUEUE_ENTRY;

typedef struct _LINKED_BLOCKING_QUEUE {
    PLT_MUTEX mutex;
    PLT_COND cond;
    PLINKED_BLOCKING_QUEUE_ENTRY head;
    PLINKED_BLOCKING_QUEUE_ENTRY tail;
    int sizeBound;
    int currentSize;
    int lifetimeSize;
    bool shutdown;
    bool draining;
    bool pendingUserWake;
} LINKED_BLOCKING_QUEUE, *PLINKED_BLOCKING_QUEUE;

int LbqInitializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound);
int LbqOfferQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry);
int LbqOfferQueueItemWithTailMerge(PLINKED_BLOCKING_QUEUE queueHead, void* data,
                                   PLINKED_BLOCKING_QUEUE_ENTRY entry,
                                   PLBQ_TAIL_MERGE_CALLBACK mergeCallback, void* context);
int LbqOfferQueueItemWithOldestEviction(PLINKED_BLOCKING_QUEUE queueHead, void* data,
                                        PLINKED_BLOCKING_QUEUE_ENTRY entry,
                                        PLBQ_EVICTION_PREDICATE evictionPredicate,
                                        void* context, void** evictedData);
int LbqWaitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
int LbqPollQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
int LbqPollQueueElementIf(PLINKED_BLOCKING_QUEUE queueHead,
                          PLBQ_POLL_PREDICATE predicate,
                          void* context,
                          void** data);
int LbqPeekQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
PLINKED_BLOCKING_QUEUE_ENTRY LbqDestroyLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead);
PLINKED_BLOCKING_QUEUE_ENTRY LbqFlushQueueItems(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueShutdown(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueDrain(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueUserWake(PLINKED_BLOCKING_QUEUE queueHead);
int LbqGetItemCount(PLINKED_BLOCKING_QUEUE queueHead);
