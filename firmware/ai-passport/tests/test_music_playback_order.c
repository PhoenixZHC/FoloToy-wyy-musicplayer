#include "music_playback_order.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t seed;
    unsigned rng_calls, allocations, frees;
    bool fail_rng, fail_alloc, zero_rng;
    const uint32_t *script;
    size_t script_count, script_cursor;
} fixture_t;

static bool rng(void *ctx, uint32_t *value)
{
    fixture_t *f = ctx;
    ++f->rng_calls;
    if (f->fail_rng) return false;
    if (f->script_cursor < f->script_count) *value = f->script[f->script_cursor++];
    else if (f->zero_rng) *value = 0;
    else { f->seed = f->seed * 1664525u + 1013904223u; *value = f->seed; }
    return true;
}

static void *allocate(void *ctx, size_t bytes)
{
    fixture_t *f = ctx;
    ++f->allocations;
    return f->fail_alloc ? NULL : malloc(bytes);
}

static void release(void *ctx, void *ptr)
{
    fixture_t *f = ctx;
    ++f->frees;
    free(ptr);
}

static music_playback_order_t make_order(fixture_t *f)
{
    music_playback_order_t order;
    music_playback_config_t config = { rng, f, allocate, release, f };
    assert(music_playback_order_init(&order, &config) == MUSIC_PLAYBACK_OK);
    return order;
}

static uint32_t move(music_playback_order_t *o, music_playback_direction_t dir)
{
    music_playback_selection_t s;
    assert(music_playback_order_peek(o, dir, &s) == MUSIC_PLAYBACK_OK);
    assert(music_playback_order_commit(o, &s) == MUSIC_PLAYBACK_OK);
    return s.index;
}

static void test_sequential_and_empty(void)
{
    fixture_t f = {0};
    music_playback_order_t o = make_order(&f);
    music_playback_selection_t s;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SEQUENTIAL, "p/a/list", 0, 0, 0) == MUSIC_PLAYBACK_OK);
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_NEXT, &s) == MUSIC_PLAYBACK_END);
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SEQUENTIAL, "p/a/list", 3, 1, 0) == MUSIC_PLAYBACK_OK);
    assert(move(&o, MUSIC_PLAYBACK_NEXT) == 2);
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_NEXT, &s) == MUSIC_PLAYBACK_END);
    assert(move(&o, MUSIC_PLAYBACK_PREVIOUS) == 1);
    assert(move(&o, MUSIC_PLAYBACK_PREVIOUS) == 0);
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_PREVIOUS, &s) == MUSIC_PLAYBACK_END);
    assert(f.allocations == 0);
    music_playback_order_clear(&o);
}

static void test_rounds(uint32_t count)
{
    fixture_t f = {.seed = 31};
    music_playback_order_t o = make_order(&f);
    bool *seen = calloc(count, sizeof(bool));
    uint32_t *round = malloc((size_t)count * sizeof(uint32_t));
    assert(seen && round);
    uint32_t start = count / 2;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "n/user/playlist@v1", count, start, SIZE_MAX) == MUSIC_PLAYBACK_OK);
    assert(o.current == start && o.allocated_bytes == (size_t)count * 12);
    music_playback_selection_t s;
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_PREVIOUS, &s) == MUSIC_PLAYBACK_END);
    for (unsigned r = 0; r < 4; ++r) {
        memset(seen, 0, (size_t)count * sizeof(bool));
        round[0] = o.current;
        seen[o.current] = true;
        for (uint32_t i = 1; i < count; ++i) {
            uint32_t index = move(&o, MUSIC_PLAYBACK_NEXT);
            assert(index < count && !seen[index]);
            seen[index] = true;
            round[i] = index;
        }
        if (count > 1) {
            for (uint32_t i = count - 1; i > 0; --i)
                assert(move(&o, MUSIC_PLAYBACK_PREVIOUS) == round[i - 1]);
            for (uint32_t i = 1; i < count; ++i)
                assert(move(&o, MUSIC_PLAYBACK_NEXT) == round[i]);
        }
        uint32_t last = o.current;
        unsigned allocations = f.allocations;
        assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_NEXT, &s) == MUSIC_PLAYBACK_OK);
        music_playback_selection_t retry;
        unsigned rng_calls = f.rng_calls;
        assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_NEXT, &retry) == MUSIC_PLAYBACK_OK);
        assert(s.index == retry.index && s.token == retry.token && rng_calls == f.rng_calls);
        assert(o.current == last && f.allocations == allocations);
        assert(music_playback_order_commit(&o, &s) == MUSIC_PLAYBACK_OK);
        uint32_t first = o.current;
        if (count > 1) assert(first != last);
        assert(move(&o, MUSIC_PLAYBACK_PREVIOUS) == last);
        assert(move(&o, MUSIC_PLAYBACK_NEXT) == first);
    }
    music_playback_order_clear(&o);
    assert(o.mode == MUSIC_PLAYBACK_SHUFFLE && !o.storage && !o.total);
    assert(f.allocations == f.frees);
    free(round);
    free(seen);
}

static void test_transactions(void)
{
    fixture_t f = {.seed = 9};
    music_playback_order_t o = make_order(&f);
    music_playback_selection_t s, other;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "platform/account/list", 8, 4, 96) == MUSIC_PLAYBACK_OK);
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_NEXT, &s) == MUSIC_PLAYBACK_OK);
    assert(o.current == 4);
    uint32_t *old = o.storage;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "platform/new/list", 8, 1, 191) == MUSIC_PLAYBACK_BUDGET);
    assert(o.storage == old && o.current == 4 && o.pending);
    f.fail_alloc = true;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "platform/new/list", 8, 1, 192) == MUSIC_PLAYBACK_NO_MEMORY);
    f.fail_alloc = false;
    f.fail_rng = true;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "platform/new/list", 8, 1, 192) == MUSIC_PLAYBACK_RNG_ERROR);
    f.fail_rng = false;
    assert(o.storage == old && o.current == 4);
    assert(music_playback_order_commit(&o, &s) == MUSIC_PLAYBACK_OK);
    assert(music_playback_order_commit(&o, &s) == MUSIC_PLAYBACK_STALE);
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_NEXT, &s) == MUSIC_PLAYBACK_OK);
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_PREVIOUS, &other) == MUSIC_PLAYBACK_OK);
    assert(music_playback_order_commit(&o, &s) == MUSIC_PLAYBACK_STALE);
    music_playback_order_cancel(&o);
    assert(music_playback_order_commit(&o, &other) == MUSIC_PLAYBACK_STALE);
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "platform/new/list@v2", 8, 6, 192) == MUSIC_PLAYBACK_OK);
    assert(o.current == 6 && !strcmp(o.identity, "platform/new/list@v2"));
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SEQUENTIAL, o.identity, 8, 6, 0) == MUSIC_PLAYBACK_OK);
    assert(!strcmp(o.identity, "platform/new/list@v2") && !o.storage && move(&o, MUSIC_PLAYBACK_NEXT) == 7);
    music_playback_order_clear(&o);
}

static void test_rejection_and_rng_failure(void)
{
    fixture_t f = {0};
    music_playback_order_t o = make_order(&f);
    const uint32_t script[] = { 0, 1, 0 };
    f.script = script;
    f.script_count = 3;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "id", 3, 0, 36) == MUSIC_PLAYBACK_OK);
    assert(f.script_cursor == 3 && f.rng_calls == 3); /* bound=3拒绝0，bound=2接受0。 */
    move(&o, MUSIC_PLAYBACK_NEXT);
    move(&o, MUSIC_PLAYBACK_NEXT);
    uint32_t last = o.current;
    music_playback_selection_t s;
    f.fail_rng = true;
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_NEXT, &s) == MUSIC_PLAYBACK_RNG_ERROR);
    assert(o.current == last && !o.pending);
    f.fail_rng = false;
    f.zero_rng = true;
    unsigned before = f.rng_calls;
    assert(music_playback_order_peek(&o, MUSIC_PLAYBACK_NEXT, &s) == MUSIC_PLAYBACK_RNG_ERROR);
    assert(f.rng_calls == before + 128 && o.current == last);
    music_playback_order_clear(&o);
}

static void test_invalid_inputs(void)
{
    fixture_t f = {0};
    music_playback_order_t o = make_order(&f);
    char identity[MUSIC_PLAYBACK_IDENTITY_MAX + 2];
    memset(identity, 'x', sizeof(identity));
    identity[sizeof(identity) - 1] = 0;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, identity, 2, 0, SIZE_MAX) == MUSIC_PLAYBACK_INVALID);
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "id", 0, 1, SIZE_MAX) == MUSIC_PLAYBACK_INVALID);
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "id", 2, 2, SIZE_MAX) == MUSIC_PLAYBACK_INVALID);
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "id", UINT32_MAX, 0, 4096) == MUSIC_PLAYBACK_BUDGET);
    identity[MUSIC_PLAYBACK_IDENTITY_MAX] = 0;
    assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, identity, 0, 0, 0) == MUSIC_PLAYBACK_OK);
    music_playback_order_clear(&o);
    music_playback_config_t bad = {.alloc = allocate};
    assert(music_playback_order_init(&o, &bad) == MUSIC_PLAYBACK_INVALID);
}

static void test_small_permutation_uniformity(void)
{
    /* 穷举3!种等概率Fisher-Yates选择，固定起点后两种后缀各出现3次。 */
    unsigned counts[3] = {0};
    for (uint32_t a = 0; a < 3; ++a) for (uint32_t b = 0; b < 2; ++b) {
        fixture_t f = {0};
        uint32_t script[] = {a == 0 ? 3 : a, b};
        f.script = script;
        f.script_count = 2;
        music_playback_order_t o = make_order(&f);
        assert(music_playback_order_bind(&o, MUSIC_PLAYBACK_SHUFFLE, "id", 3, 0, 36) == MUSIC_PLAYBACK_OK);
        ++counts[move(&o, MUSIC_PLAYBACK_NEXT)];
        music_playback_order_clear(&o);
    }
    assert(counts[0] == 0 && counts[1] == 3 && counts[2] == 3);
}

int main(void)
{
    test_sequential_and_empty();
    test_rounds(1);
    test_rounds(2);
    test_rounds(9);
    test_rounds(1025);
    test_transactions();
    test_rejection_and_rng_failure();
    test_invalid_inputs();
    test_small_permutation_uniformity();
    puts("music_playback_order: 9 tests passed");
    return 0;
}
