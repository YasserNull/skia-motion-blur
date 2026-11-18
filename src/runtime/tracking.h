#pragma once

#include "renderer.h"
#include <EGL/egl.h>
#include <android/hardware_buffer.h>

typedef struct {
    uintptr_t first, second;
    AHardwareBuffer_Desc descriptor;
} MbBufferEntry;

typedef struct {
    MbBufferEntry *items;
    size_t count, capacity;
} MbBufferTable;

typedef struct {
    EGLContext context;
    MbRenderer renderer;
} MbRenderContext;

typedef struct {
    MbBufferTable buffers, images, textures;
    MbRenderContext contexts[16];
    size_t context_count;
} MbTracking;

// Hold the tracking lock while accessing tables, contexts or returned pointers.
extern MbTracking mb_tracking;

bool mb_tracking_allowed(void);
void mb_tracking_lock(void);
void mb_tracking_unlock(void);
MbBufferEntry *mb_table_find(MbBufferTable *table, uintptr_t first, uintptr_t second);
void mb_table_clear(MbBufferTable *table);
void mb_table_remember(MbBufferTable *table, uintptr_t first, uintptr_t second, const AHardwareBuffer_Desc *descriptor);
void mb_table_erase(MbBufferTable *table, uintptr_t first, uintptr_t second);
MbRenderer *mb_tracking_renderer(EGLContext context, bool create);
void mb_tracking_remove_context(EGLContext context);
