// Build with -Wl,--wrap=realloc to exercise allocation failures.
#include "../src/runtime/hooks.c"
#include <assert.h>

bool mb_repaint_available(void) { return false; }
void mb_request_repaint(uint64_t session) { (void)session; }

static bool allocation_failure;
void *__real_realloc(void *pointer, size_t size);
void *__wrap_realloc(void *pointer, size_t size) {
    return allocation_failure ? NULL : __real_realloc(pointer, size);
}

int main(void) {
    MbBufferTable table = {0};
    AHardwareBuffer_Desc descriptor = { .width = 720, .height = 1600, .layers = 1 };
    allocation_failure = true;
    mb_table_remember(&table, 1, 1, &descriptor);
    assert(!table.count && !table.items);
    allocation_failure = false;
    for (uintptr_t i = 4097; i > 0; --i) {
        descriptor.width = (uint32_t)i;
        mb_table_remember(&table, i, i % 7, &descriptor);
    }
    assert(table.count == 4097);
    descriptor.width = 9999;
    mb_table_remember(&table, 5000, 0, &descriptor);
    assert(table.count == 4097 && !mb_table_find(&table, 5000, 0));
    for (uintptr_t i = 1; i <= 4097; ++i) {
        MbBufferEntry *entry = mb_table_find(&table, i, i % 7);
        assert(entry && entry->descriptor.width == i);
        assert(!mb_table_find(&table, i, 8));
    }
    mb_table_remember(&table, 2000, 2000 % 7, &descriptor);
    assert(table.count == 4097 && mb_table_find(&table, 2000, 2000 % 7)->descriptor.width == 9999);
    mb_table_erase(&table, 1, 1);
    mb_table_erase(&table, 2000, 2000 % 7);
    mb_table_erase(&table, 4097, 4097 % 7);
    assert(table.count == 4094);
    assert(!mb_table_find(&table, 1, 1) && !mb_table_find(&table, 2000, 2000 % 7) && !mb_table_find(&table, 4097, 4097 % 7));
    assert(mb_table_find(&table, 1999, 1999 % 7) && mb_table_find(&table, 2001, 2001 % 7));
    mb_table_clear(&table);
    assert(!table.count && !table.capacity && !table.items);
    EGLint normal[] = {EGL_NONE};
    EGLint protected_attributes[] = {EGL_WIDTH, 720, EGL_PROTECTED_CONTENT_EXT, EGL_TRUE, EGL_NONE};
    EGLAttrib wide[] = {EGL_PROTECTED_CONTENT_EXT, EGL_TRUE, EGL_NONE};
    assert(!protected_image(NULL, false) && !protected_image(normal, false));
    assert(protected_image(protected_attributes, false) && protected_image(wide, true));
    mb_tracking_lock();
    mb_tracking_lock();
    MbRenderer *display = mb_tracking_renderer((EGLContext)1, 1440, 3120, true);
    MbRenderer *recording = mb_tracking_renderer((EGLContext)1, 720, 1560, true);
    MbRenderer *other = mb_tracking_renderer((EGLContext)2, 1440, 3120, true);
    assert(display && recording && other && display != recording && display != other);
    display->previous = 123;
    display->repaint_needed = true;
    display->ready = true;
    display->memory_bytes = 12 * 1024 * 1024;
    recording->previous = 456;
    recording->repaint_needed = true;
    recording->ready = true;
    recording->memory_bytes = 4 * 1024 * 1024 + 1;
    other->previous = 789;
    other->ready = true;
    other->memory_bytes = 8 * 1024 * 1024;
    for (unsigned i = 0; i < 8; ++i) {
        assert(mb_tracking_renderer((EGLContext)1, 1440, 3120, true) == display);
        assert(mb_tracking_renderer((EGLContext)1, 720, 1560, true) == recording);
        assert(display->previous == 123 && recording->previous == 456);
    }
    assert(mb_tracking_memory_limit((EGLContext)1, display, 48) == 43);
    assert(mb_tracking_memory_limit((EGLContext)1, recording, 48) == 36);
    assert(mb_tracking_memory_limit((EGLContext)2, other, 48) == 48);
    assert(mb_tracking_memory_limit((EGLContext)1, recording, 12) == 0);
    assert(mb_tracking_memory_limit((EGLContext)1, recording, 8) == 0);
    recording->ready = false;
    assert(mb_tracking_memory_limit((EGLContext)1, display, 48) == 48);
    mb_tracking_reset_context((EGLContext)1);
    assert(!display->previous && !recording->previous && other->previous == 789);
    assert(!display->repaint_needed && !recording->repaint_needed);
    mb_tracking_remove_context((EGLContext)1);
    assert(mb_tracking.output_count == 1);
    assert(!mb_tracking_renderer((EGLContext)1, 1440, 3120, false));
    assert(!mb_tracking_renderer((EGLContext)1, 720, 1560, false));
    assert(mb_tracking_renderer((EGLContext)2, 1440, 3120, false)->previous == 789);
    mb_tracking_remove_context((EGLContext)2);
    assert(!mb_tracking.output_count);
    assert(!mb_tracking_renderer((EGLContext)1, 0, 3120, true));
    for (uintptr_t i = 1; i <= 16; ++i) assert(mb_tracking_renderer((EGLContext)i, 720, 1560, true));
    assert(!mb_tracking_renderer((EGLContext)17, 720, 1560, true));
    assert(!mb_tracking_renderer((EGLContext)1, 1440, 3120, true));
    assert(mb_tracking_renderer((EGLContext)1, 720, 1560, false) && !mb_tracking_renderer((EGLContext)18, 720, 1560, false));
    mb_tracking_unlock();
    mb_tracking_unlock();
    puts("PASS: bounded tracking, output histories, shared memory budget, context reset and removal, protected attributes, recursive locking");
}
