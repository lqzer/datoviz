#include "../include/datoviz/fifo.h"



/*************************************************************************************************/
/*  Thread-safe FIFO queue                                                                       */
/*************************************************************************************************/

DvzFifo dvz_fifo(int32_t capacity)
{
    log_trace("creating generic FIFO queue with a capacity of %d items", capacity);
    ASSERT(capacity >= 2);
    DvzFifo fifo = {0};
    ASSERT(capacity <= DVZ_MAX_FIFO_CAPACITY);
    fifo.capacity = capacity;
    fifo.is_empty = true;
    fifo.items = calloc((uint32_t)capacity, sizeof(void*));

    if (pthread_mutex_init(&fifo.lock, NULL) != 0)
        log_error("mutex creation failed");
    if (pthread_cond_init(&fifo.cond, NULL) != 0)
        log_error("cond creation failed");

    return fifo;
}



void dvz_fifo_enqueue(DvzFifo* fifo, void* item)
{
    ASSERT(fifo != NULL);
    pthread_mutex_lock(&fifo->lock);

    // Old size
    int size = fifo->head - fifo->tail;
    if (size < 0)
        size += fifo->capacity;

    // Old capacity
    int old_cap = fifo->capacity;

    // Resize if queue is full.
    if ((fifo->head + 1) % fifo->capacity == fifo->tail)
    {
        ASSERT(fifo->items != NULL);
        ASSERT(size == fifo->capacity - 1);

        // NOTE: we hard-code the maximum size of FIFO queues for now as the canvas has an array of
        // fixed size for the event objects stored in the FIFO queue.
        // TODO: fix this by making the canvas event array enlargeable.
        ASSERT(fifo->capacity <= DVZ_MAX_FIFO_CAPACITY);

        fifo->capacity *= 2;
        log_debug("FIFO queue is full, enlarging it to %d", fifo->capacity);
        REALLOC(fifo->items, (uint32_t)fifo->capacity * sizeof(void*));
    }

    if ((fifo->head + 1) % fifo->capacity == fifo->tail)
    {
        // Here, the queue buffer has been resized, but the new space should be used instead of the
        // part of the buffer before the tail.

        ASSERT(fifo->head > 0);
        ASSERT(old_cap < fifo->capacity);
        memcpy(&fifo->items[old_cap], &fifo->items[0], (uint32_t)fifo->head * sizeof(void*));

        // Move the head to the new position.
        fifo->head += old_cap;

        // Check new size.
        ASSERT(fifo->head - fifo->tail > 0);
        ASSERT(fifo->head - fifo->tail == size);
    }
    ASSERT((fifo->head + 1) % fifo->capacity != fifo->tail);
    fifo->items[fifo->head] = item;
    fifo->head++;
    if (fifo->head >= fifo->capacity)
        fifo->head -= fifo->capacity;
    fifo->is_empty = false;

    ASSERT(0 <= fifo->head && fifo->head < fifo->capacity);
    pthread_cond_signal(&fifo->cond);
    pthread_mutex_unlock(&fifo->lock);
}



void* dvz_fifo_dequeue(DvzFifo* fifo, bool wait)
{
    ASSERT(fifo != NULL);
    pthread_mutex_lock(&fifo->lock);

    // Wait until the queue is not empty.
    if (wait)
    {
        log_trace("waiting for the queue to be non-empty");
        while (fifo->head == fifo->tail)
            pthread_cond_wait(&fifo->cond, &fifo->lock);
    }

    // Empty queue.
    if (fifo->head == fifo->tail)
    {
        // log_trace("FIFO queue was empty");
        // Don't forget to unlock the mutex before exiting this function.
        pthread_mutex_unlock(&fifo->lock);
        fifo->is_empty = true;
        return NULL;
    }

    ASSERT(0 <= fifo->tail && fifo->tail < fifo->capacity);

    // log_trace("dequeue item, head %d, tail %d", fifo->head, fifo->tail);
    void* item = fifo->items[fifo->tail];

    fifo->tail++;
    if (fifo->tail >= fifo->capacity)
        fifo->tail -= fifo->capacity;

    ASSERT(0 <= fifo->tail && fifo->tail < fifo->capacity);
    pthread_mutex_unlock(&fifo->lock);

    if (fifo->head == fifo->tail)
        fifo->is_empty = true;

    return item;
}



int dvz_fifo_size(DvzFifo* fifo)
{
    ASSERT(fifo != NULL);
    pthread_mutex_lock(&fifo->lock);
    // log_debug("head %d tail %d", fifo->head, fifo->tail);
    int size = fifo->head - fifo->tail;
    if (size < 0)
        size += fifo->capacity;
    ASSERT(0 <= size && size <= fifo->capacity);
    pthread_mutex_unlock(&fifo->lock);
    return size;
}



void dvz_fifo_discard(DvzFifo* fifo, int max_size)
{
    ASSERT(fifo != NULL);
    if (max_size == 0)
        return;
    pthread_mutex_lock(&fifo->lock);
    int size = fifo->head - fifo->tail;
    if (size < 0)
        size += fifo->capacity;
    ASSERT(0 <= size && size <= fifo->capacity);
    if (size > max_size)
    {
        log_trace(
            "discarding %d items in the FIFO queue which is getting overloaded", size - max_size);
        fifo->tail = fifo->head - max_size;
        if (fifo->tail < 0)
            fifo->tail += fifo->capacity;
    }
    pthread_mutex_unlock(&fifo->lock);
}



void dvz_fifo_reset(DvzFifo* fifo)
{
    ASSERT(fifo != NULL);
    pthread_mutex_lock(&fifo->lock);
    fifo->head = 0;
    fifo->tail = 0;
    pthread_cond_signal(&fifo->cond);
    pthread_mutex_unlock(&fifo->lock);
}



void dvz_fifo_destroy(DvzFifo* fifo)
{
    ASSERT(fifo != NULL);
    pthread_mutex_destroy(&fifo->lock);
    pthread_cond_destroy(&fifo->cond);

    ASSERT(fifo->items != NULL);
    FREE(fifo->items);
}
