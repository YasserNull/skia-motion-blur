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
    unsigned width, height;
    MbRenderer renderer;
} MbRenderOutput;

typedef struct {
    MbBufferTable buffers, images, textures;
    MbRenderOutput outputs[16];
    size_t output_count;
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
MbRenderer *mb_tracking_renderer(EGLContext context, unsigned width, unsigned height, bool create);
unsigned mb_tracking_memory_limit(EGLContext context, const MbRenderer *renderer, unsigned limit_mb);
void mb_tracking_reset_context(EGLContext context);
void mb_tracking_remove_context(EGLContext context);
