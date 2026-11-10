#include "music_playback_order.h"

#include <stdlib.h>
#include <string.h>

static void *default_alloc(void *ctx, size_t bytes)
{
    (void)ctx;
    return malloc(bytes);
}

static void default_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static uint64_t next_token(music_playback_order_t *order)
{
    if (++order->serial == 0) ++order->serial;
    return order->serial;
}

void music_playback_order_cancel(music_playback_order_t *order)
{
    if (!order) return;
    order->pending = false;
    next_token(order);
}

void music_playback_order_clear(music_playback_order_t *order)
{
    if (!order) return;
    if (order->storage) order->config.free(order->config.alloc_ctx, order->storage);
    order->storage = order->queue = order->previous = order->staging = NULL;
    order->allocated_bytes = 0;
    order->total = order->current = order->cursor = 0;
    order->identity[0] = '\0';
    order->previous_valid = order->in_previous = false;
    music_playback_order_cancel(order);
}

music_playback_result_t music_playback_order_init(music_playback_order_t *order,
                                                 const music_playback_config_t *config)
{
    if (!order || (config && (!!config->alloc != !!config->free)))
        return MUSIC_PLAYBACK_INVALID;
    memset(order, 0, sizeof(*order));
    if (config) order->config = *config;
    if (!order->config.alloc) {
        order->config.alloc = default_alloc;
        order->config.free = default_free;
    }
    return MUSIC_PLAYBACK_OK;
}

static bool uniform(music_playback_order_t *order, uint32_t bound, uint32_t *result)
{
    /* 拒绝不能均分到bound的低端余数，避免取模偏差。上限防止故障RNG挂住worker。 */
    uint32_t threshold = (uint32_t)(0u - bound) % bound;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        uint32_t value;
        if (!order->config.rng(order->config.rng_ctx, &value)) return false;
        if (value >= threshold) {
            *result = value % bound;
            return true;
        }
    }
    return false;
}

static bool shuffle(music_playback_order_t *order, uint32_t *queue, uint32_t total)
{
    for (uint32_t i = 0; i < total; ++i) queue[i] = i;
    for (uint32_t i = total; i > 1; --i) {
        uint32_t j;
        if (!uniform(order, i, &j)) return false;
        uint32_t tmp = queue[i - 1];
        queue[i - 1] = queue[j];
        queue[j] = tmp;
    }
    return true;
}

music_playback_result_t music_playback_order_bind(music_playback_order_t *order,
    music_playback_mode_t mode, const char *identity, uint32_t total,
    uint32_t current, size_t budget)
{
    if (!order || !order->config.alloc || !identity ||
        (mode != MUSIC_PLAYBACK_SEQUENTIAL && mode != MUSIC_PLAYBACK_SHUFFLE) ||
        (total ? current >= total : current != 0)) return MUSIC_PLAYBACK_INVALID;
    size_t length = 0;
    while (length <= MUSIC_PLAYBACK_IDENTITY_MAX && identity[length]) ++length;
    if (length > MUSIC_PLAYBACK_IDENTITY_MAX) return MUSIC_PLAYBACK_INVALID;
    uint32_t *storage = NULL;
    size_t bytes = 0;
    if (mode == MUSIC_PLAYBACK_SHUFFLE && total) {
        if (3 * sizeof(uint32_t) > SIZE_MAX / (size_t)total) return MUSIC_PLAYBACK_BUDGET;
        bytes = (size_t)total * 3 * sizeof(uint32_t);
        if (bytes > budget || order->allocated_bytes > budget - bytes)
            return MUSIC_PLAYBACK_BUDGET;
        if (!order->config.rng && total > 1) return MUSIC_PLAYBACK_INVALID;
        storage = order->config.alloc(order->config.alloc_ctx, bytes);
        if (!storage) return MUSIC_PLAYBACK_NO_MEMORY;
        if (!shuffle(order, storage, total)) {
            order->config.free(order->config.alloc_ctx, storage);
            return MUSIC_PLAYBACK_RNG_ERROR;
        }
        /* 固定起点，其余条目仍为均匀随机排列。 */
        for (uint32_t i = 0; i < total; ++i) {
            if (storage[i] == current) {
                storage[i] = storage[0];
                storage[0] = current;
                break;
            }
        }
    }
    /* identity允许指向order->identity，释放前先保存。 */
    char identity_copy[MUSIC_PLAYBACK_IDENTITY_MAX + 1];
    memcpy(identity_copy, identity, length + 1);
    music_playback_order_clear(order);
    order->mode = mode;
    memcpy(order->identity, identity_copy, length + 1);
    order->total = total;
    order->current = current;
    order->storage = order->queue = storage;
    order->allocated_bytes = bytes;
    if (storage) {
        order->previous = storage + total;
        order->staging = storage + (size_t)total * 2;
    }
    return MUSIC_PLAYBACK_OK;
}

music_playback_result_t music_playback_order_peek(music_playback_order_t *order,
    music_playback_direction_t direction, music_playback_selection_t *selection)
{
    if (!order || !selection ||
        (direction != MUSIC_PLAYBACK_NEXT && direction != MUSIC_PLAYBACK_PREVIOUS))
        return MUSIC_PLAYBACK_INVALID;
    if (order->pending && order->pending_direction == direction) {
        *selection = order->selection;
        return MUSIC_PLAYBACK_OK;
    }
    music_playback_order_cancel(order);
    if (!order->total) return MUSIC_PLAYBACK_END;
    uint32_t index, cursor = order->cursor;
    bool previous = order->in_previous, rollover = false;
    if (order->mode == MUSIC_PLAYBACK_SEQUENTIAL) {
        if (direction == MUSIC_PLAYBACK_NEXT) {
            if (order->current == order->total - 1) return MUSIC_PLAYBACK_END;
            index = order->current + 1;
        } else {
            if (!order->current) return MUSIC_PLAYBACK_END;
            index = order->current - 1;
        }
    } else {
        if (direction == MUSIC_PLAYBACK_PREVIOUS) {
            if (cursor) --cursor;
            else if (!previous && order->previous_valid) {
                previous = true;
                cursor = order->total - 1;
            } else return MUSIC_PLAYBACK_END;
        } else if (cursor < order->total - 1) ++cursor;
        else if (previous) {
            previous = false;
            cursor = 0;
        } else {
            if (!shuffle(order, order->staging, order->total)) return MUSIC_PLAYBACK_RNG_ERROR;
            if (order->total > 1 && order->staging[0] == order->current) {
                uint32_t other;
                if (!uniform(order, order->total - 1, &other)) return MUSIC_PLAYBACK_RNG_ERROR;
                ++other;
                order->staging[0] = order->staging[other];
                order->staging[other] = order->current;
            }
            cursor = 0;
            rollover = true;
        }
        index = rollover ? order->staging[cursor] :
            (previous ? order->previous[cursor] : order->queue[cursor]);
    }
    order->pending = true;
    order->pending_direction = direction;
    order->pending_cursor = cursor;
    order->pending_previous = previous;
    order->pending_rollover = rollover;
    order->selection = (music_playback_selection_t){ index, next_token(order) };
    *selection = order->selection;
    return MUSIC_PLAYBACK_OK;
}

music_playback_result_t music_playback_order_commit(music_playback_order_t *order,
    const music_playback_selection_t *selection)
{
    if (!order || !selection) return MUSIC_PLAYBACK_INVALID;
    if (!order->pending || selection->token != order->selection.token ||
        selection->index != order->selection.index) return MUSIC_PLAYBACK_STALE;
    if (order->pending_rollover) {
        uint32_t *old_previous = order->previous;
        order->previous = order->queue;
        order->queue = order->staging;
        order->staging = old_previous;
        order->previous_valid = true;
    }
    order->cursor = order->pending_cursor;
    order->in_previous = order->pending_previous;
    order->current = selection->index;
    music_playback_order_cancel(order);
    return MUSIC_PLAYBACK_OK;
}
