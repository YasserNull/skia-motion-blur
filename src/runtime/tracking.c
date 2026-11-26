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

MbRenderer *mb_tracking_renderer(EGLContext context, unsigned width, unsigned height, bool create) {
    if (!width || !height) return NULL;
    for (size_t i = 0; i < mb_tracking.output_count; ++i) {
        MbRenderOutput *output = &mb_tracking.outputs[i];
        if (output->context == context && output->width == width && output->height == height)
            return &output->renderer;
    }
    if (!create || mb_tracking.output_count == 16) return NULL;
    mb_tracking.outputs[mb_tracking.output_count] = (MbRenderOutput){
        .context = context, .width = width, .height = height
    };
    return &mb_tracking.outputs[mb_tracking.output_count++].renderer;
}

unsigned mb_tracking_memory_limit(EGLContext context, const MbRenderer *renderer, unsigned limit_mb) {
    uint64_t remaining = (uint64_t)limit_mb * 1024 * 1024;
    for (size_t i = 0; i < mb_tracking.output_count; ++i) {
        const MbRenderOutput *output = &mb_tracking.outputs[i];
        if (output->context != context || &output->renderer == renderer || !output->renderer.ready)
            continue;
        if (output->renderer.memory_bytes >= remaining) return 0;
        remaining -= output->renderer.memory_bytes;
    }
    return (unsigned)(remaining / (1024 * 1024));
}

void mb_tracking_reset_context(EGLContext context) {
    for (size_t i = 0; i < mb_tracking.output_count; ++i)
        if (mb_tracking.outputs[i].context == context) mb_renderer_reset(&mb_tracking.outputs[i].renderer);
}

void mb_tracking_remove_context(EGLContext context) {
    for (size_t i = mb_tracking.output_count; i-- > 0;) {
        if (mb_tracking.outputs[i].context != context) continue;
        // Context-local GL objects require this context for deletion.
        if (eglGetCurrentContext() == context) mb_renderer_destroy(&mb_tracking.outputs[i].renderer);
        memmove(mb_tracking.outputs + i, mb_tracking.outputs + i + 1,
                (--mb_tracking.output_count - i) * sizeof(*mb_tracking.outputs));
    }
    for (size_t i = mb_tracking.textures.count; i-- > 0;)
        if (mb_tracking.textures.items[i].first == (uintptr_t)context)
            mb_table_erase(&mb_tracking.textures, mb_tracking.textures.items[i].first, mb_tracking.textures.items[i].second);
}
