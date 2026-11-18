#define _POSIX_C_SOURCE 200809L
#include "tracking.h"
#include "policy.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

MbTracking mb_tracking;
static pthread_mutex_t mutex;
static pthread_once_t mutex_once = PTHREAD_ONCE_INIT;
static pthread_once_t allowed_once = PTHREAD_ONCE_INIT;
static bool process_allowed;

static void initialize_mutex(void) {
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    // Renderer cleanup can re-enter texture hooks while tracking is locked.
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);
}

void mb_tracking_lock(void) { pthread_once(&mutex_once, initialize_mutex); pthread_mutex_lock(&mutex); }
void mb_tracking_unlock(void) { pthread_mutex_unlock(&mutex); }

static void check_allowed(void) { process_allowed = mb_is_surface_flinger() && mb_supported(mb_sdk()); }
bool mb_tracking_allowed(void) { pthread_once(&allowed_once, check_allowed); return process_allowed; }

static size_t position(const MbBufferTable *table, uintptr_t first, uintptr_t second) {
    size_t low = 0, high = table->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        const MbBufferEntry *entry = &table->items[middle];
        if (entry->first < first || (entry->first == first && entry->second < second)) low = middle + 1;
        else high = middle;
    }
    return low;
}

MbBufferEntry *mb_table_find(MbBufferTable *table, uintptr_t first, uintptr_t second) {
    size_t at = position(table, first, second);
    return at < table->count && table->items[at].first == first && table->items[at].second == second
        ? &table->items[at] : NULL;
}

void mb_table_clear(MbBufferTable *table) {
    free(table->items);
    *table = (MbBufferTable){0};
}

void mb_table_remember(MbBufferTable *table, uintptr_t first, uintptr_t second, const AHardwareBuffer_Desc *descriptor) {
    size_t at = position(table, first, second);
    if (at < table->count && table->items[at].first == first && table->items[at].second == second) {
        table->items[at].descriptor = *descriptor;
        return;
    }
    // Bound memory when callbacks arrive without matching destruction.
    if (table->count >= 4097) return;
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 16;
        if (capacity > 4097) capacity = 4097;
        MbBufferEntry *items = realloc(table->items, capacity * sizeof(*items));
        if (!items) return;
        table->items = items;
        table->capacity = capacity;
    }
    memmove(table->items + at + 1, table->items + at, (table->count - at) * sizeof(*table->items));
    table->items[at] = (MbBufferEntry){ first, second, *descriptor };
    ++table->count;
}

void mb_table_erase(MbBufferTable *table, uintptr_t first, uintptr_t second) {
    size_t at = position(table, first, second);
    if (at == table->count || table->items[at].first != first || table->items[at].second != second) return;
    memmove(table->items + at, table->items + at + 1, (table->count - at - 1) * sizeof(*table->items));
    --table->count;
}

MbRenderer *mb_tracking_renderer(EGLContext context, bool create) {
    for (size_t i = 0; i < mb_tracking.context_count; ++i)
        if (mb_tracking.contexts[i].context == context) return &mb_tracking.contexts[i].renderer;
    if (!create || mb_tracking.context_count == 16) return NULL;
    mb_tracking.contexts[mb_tracking.context_count] = (MbRenderContext){ .context = context };
    return &mb_tracking.contexts[mb_tracking.context_count++].renderer;
}

void mb_tracking_remove_context(EGLContext context) {
    for (size_t i = 0; i < mb_tracking.context_count; ++i) {
        if (mb_tracking.contexts[i].context != context) continue;
        // Context-local GL objects require this context for deletion.
        if (eglGetCurrentContext() == context) mb_renderer_destroy(&mb_tracking.contexts[i].renderer);
        memmove(mb_tracking.contexts + i, mb_tracking.contexts + i + 1, (--mb_tracking.context_count - i) * sizeof(*mb_tracking.contexts));
        break;
    }
    for (size_t i = mb_tracking.textures.count; i-- > 0;)
        if (mb_tracking.textures.items[i].first == (uintptr_t)context)
            mb_table_erase(&mb_tracking.textures, mb_tracking.textures.items[i].first, mb_tracking.textures.items[i].second);
}
