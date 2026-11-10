#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_PLAYBACK_IDENTITY_MAX 255

typedef enum { MUSIC_PLAYBACK_SEQUENTIAL = 0, MUSIC_PLAYBACK_SHUFFLE = 1 } music_playback_mode_t;
typedef enum { MUSIC_PLAYBACK_PREVIOUS = -1, MUSIC_PLAYBACK_NEXT = 1 } music_playback_direction_t;
typedef enum {
    MUSIC_PLAYBACK_OK = 0, MUSIC_PLAYBACK_END, MUSIC_PLAYBACK_INVALID,
    MUSIC_PLAYBACK_NO_MEMORY, MUSIC_PLAYBACK_BUDGET, MUSIC_PLAYBACK_RNG_ERROR,
    MUSIC_PLAYBACK_STALE
} music_playback_result_t;

/* RNG须提供均匀的32位随机数；失败返回false，不允许静默退回顺序播放。 */
typedef bool (*music_playback_rng_fn)(void *ctx, uint32_t *value);
typedef void *(*music_playback_alloc_fn)(void *ctx, size_t bytes);
typedef void (*music_playback_free_fn)(void *ctx, void *ptr);
typedef struct {
    music_playback_rng_fn rng;
    void *rng_ctx;
    music_playback_alloc_fn alloc;
    music_playback_free_fn free;
    void *alloc_ctx;
} music_playback_config_t;

typedef struct { uint32_t index; uint64_t token; } music_playback_selection_t;

/* 状态由调用方持有；不直接修改字段。所有操作在同一个worker串行调用。 */
typedef struct {
    music_playback_config_t config;
    music_playback_mode_t mode;
    char identity[MUSIC_PLAYBACK_IDENTITY_MAX + 1];
    uint32_t total, current, cursor;
    uint32_t *storage, *queue, *previous, *staging;
    size_t allocated_bytes;
    bool previous_valid, in_previous, pending, pending_rollover;
    music_playback_direction_t pending_direction;
    uint32_t pending_cursor;
    bool pending_previous;
    music_playback_selection_t selection;
    uint64_t serial;
} music_playback_order_t;

/* alloc/free均为空时使用malloc/free；必须配对设置。init只用于新对象。 */
music_playback_result_t music_playback_order_init(music_playback_order_t *order,
                                                 const music_playback_config_t *config);
/* 每次bind重建本轮，current计入已播放项。identity包含平台/账号/歌单及内容版本，
 * 调用方负责无歧义拼接。total=0时current须为0。失败保持原模式和队列。
 * budget是序号数组的峰值预算，包含原队列与新队列；不包括固定大小的order。
 * 随机模式每项使用3个uint32_t，保留上一轮供回退。仅worker可调用。 */
music_playback_result_t music_playback_order_bind(music_playback_order_t *order,
    music_playback_mode_t mode, const char *identity, uint32_t total,
    uint32_t current, size_t budget);
/* 只选择、不消费。重复同方向peek返回同一token/index；反方向会使旧token失效。
 * 跨轮洗牌不分配内存；网络失败可原样重试，确认成功后再commit。
 * 最多回退到上一轮开头，首轮不可回退到手动起点之前。 */
music_playback_result_t music_playback_order_peek(music_playback_order_t *order,
    music_playback_direction_t direction, music_playback_selection_t *selection);
music_playback_result_t music_playback_order_commit(music_playback_order_t *order,
    const music_playback_selection_t *selection);
/* 取消未提交选择，不改变当前位置；释放仅RAM队列且保留模式和依赖配置。 */
void music_playback_order_cancel(music_playback_order_t *order);
void music_playback_order_clear(music_playback_order_t *order);
