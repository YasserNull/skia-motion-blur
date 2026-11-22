#define _POSIX_C_SOURCE 200809L
#include "policy.h"
#include "tracking.h"
#include "filter.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include "log.h"
#include <dlfcn.h>
#include <pthread.h>
#include <string.h>

#define MB_EXPORT __attribute__((visibility("default")))

typedef __eglMustCastToProperFunctionPointerType (*GetProc)(const char *);
static GetProc get_proc;
static pthread_once_t get_proc_once = PTHREAD_ONCE_INIT;
static void load_get_proc(void) { get_proc = (GetProc)dlsym(RTLD_NEXT, "eglGetProcAddress"); }
static GetProc original_get_proc(void) { pthread_once(&get_proc_once, load_get_proc); return get_proc; }

static void *original(const char *name) {
    // RTLD_NEXT skips this library when looking up the wrapped implementation.
    void *symbol = dlsym(RTLD_NEXT, name);
    GetProc fallback = original_get_proc();
    if (!symbol && fallback) symbol = (void *)fallback(name);
    return symbol;
}

#define ORIGINAL(type, name) \
    static type real_##name; \
    static pthread_once_t once_##name = PTHREAD_ONCE_INIT; \
    static void load_##name(void) { real_##name = (type)original(#name); }
#define RESOLVE(name) pthread_once(&once_##name, load_##name)

ORIGINAL(PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC, eglGetNativeClientBufferANDROID)
ORIGINAL(PFNEGLCREATEIMAGEKHRPROC, eglCreateImageKHR)
ORIGINAL(__typeof__(&eglCreateImage), eglCreateImage)
ORIGINAL(PFNEGLDESTROYIMAGEKHRPROC, eglDestroyImageKHR)
ORIGINAL(__typeof__(&eglDestroyImage), eglDestroyImage)
ORIGINAL(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC, glEGLImageTargetTexture2DOES)
ORIGINAL(PFNGLDELETETEXTURESPROC, glDeleteTextures)
ORIGINAL(PFNEGLCREATESYNCKHRPROC, eglCreateSyncKHR)
ORIGINAL(__typeof__(&eglCreateSync), eglCreateSync)
ORIGINAL(__typeof__(&eglDestroyContext), eglDestroyContext)

static bool protected_image(const void *attributes, bool wide) {
    if (!attributes) return false;
    for (int i = 0; i < 128; i += 2) {
        EGLAttrib key = wide ? ((const EGLAttrib *)attributes)[i] : ((const EGLint *)attributes)[i];
        if (key == EGL_NONE) break;
        EGLAttrib value = wide ? ((const EGLAttrib *)attributes)[i + 1] : ((const EGLint *)attributes)[i + 1];
        if (key == EGL_PROTECTED_CONTENT_EXT && value == EGL_TRUE) return true;
    }
    return false;
}

static void remember_image(EGLDisplay display, EGLImageKHR image, EGLenum target,
                           EGLClientBuffer buffer, const void *attributes, bool wide) {
    if (!mb_tracking_allowed() || !image || target != EGL_NATIVE_BUFFER_ANDROID || protected_image(attributes, wide)) return;
    mb_tracking_lock();
    MbBufferEntry *found = mb_table_find(&mb_tracking.buffers, (uintptr_t)buffer, 0);
    if (found) {
        if (mb_tracking.images.count > 4096) mb_table_clear(&mb_tracking.images);
        mb_table_remember(&mb_tracking.images, (uintptr_t)display, (uintptr_t)image, &found->descriptor);
    }
    mb_tracking_unlock();
}

MB_EXPORT EGLClientBuffer EGLAPIENTRY eglGetNativeClientBufferANDROID(const AHardwareBuffer *buffer) {
    RESOLVE(eglGetNativeClientBufferANDROID);
    EGLClientBuffer result = real_eglGetNativeClientBufferANDROID ? real_eglGetNativeClientBufferANDROID(buffer) : NULL;
    if (mb_tracking_allowed() && result && buffer) {
        AHardwareBuffer_Desc descriptor = {0};
        AHardwareBuffer_describe(buffer, &descriptor);
        mb_tracking_lock();
        if (mb_tracking.buffers.count > 4096) mb_table_clear(&mb_tracking.buffers);
        mb_table_remember(&mb_tracking.buffers, (uintptr_t)result, 0, &descriptor);
        mb_tracking_unlock();
    }
    return result;
}

MB_EXPORT EGLImageKHR EGLAPIENTRY eglCreateImageKHR(EGLDisplay display, EGLContext context, EGLenum target,
                                                   EGLClientBuffer buffer, const EGLint *attributes) {
    RESOLVE(eglCreateImageKHR);
    EGLImageKHR result = real_eglCreateImageKHR ? real_eglCreateImageKHR(display, context, target, buffer, attributes) : EGL_NO_IMAGE_KHR;
    remember_image(display, result, target, buffer, attributes, false);
    return result;
}

MB_EXPORT EGLImage EGLAPIENTRY eglCreateImage(EGLDisplay display, EGLContext context, EGLenum target,
                                              EGLClientBuffer buffer, const EGLAttrib *attributes) {
    RESOLVE(eglCreateImage);
    EGLImage result = real_eglCreateImage ? real_eglCreateImage(display, context, target, buffer, attributes) : EGL_NO_IMAGE;
    if (mb_sdk() >= 34) remember_image(display, result, target, buffer, attributes, true);
    return result;
}

MB_EXPORT EGLBoolean EGLAPIENTRY eglDestroyImageKHR(EGLDisplay display, EGLImageKHR image) {
    RESOLVE(eglDestroyImageKHR);
    mb_tracking_lock(); mb_table_erase(&mb_tracking.images, (uintptr_t)display, (uintptr_t)image); mb_tracking_unlock();
    return real_eglDestroyImageKHR ? real_eglDestroyImageKHR(display, image) : EGL_FALSE;
}

MB_EXPORT EGLBoolean EGLAPIENTRY eglDestroyImage(EGLDisplay display, EGLImage image) {
    RESOLVE(eglDestroyImage);
    mb_tracking_lock(); mb_table_erase(&mb_tracking.images, (uintptr_t)display, (uintptr_t)image); mb_tracking_unlock();
    return real_eglDestroyImage ? real_eglDestroyImage(display, image) : EGL_FALSE;
}

MB_EXPORT void GL_APIENTRY glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image) {
    RESOLVE(glEGLImageTargetTexture2DOES);
    if (real_glEGLImageTargetTexture2DOES) real_glEGLImageTargetTexture2DOES(target, image);
    if (!mb_tracking_allowed() || target != GL_TEXTURE_2D) return;
    GLint texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (!texture) return;
    mb_tracking_lock();
    uintptr_t context = (uintptr_t)eglGetCurrentContext();
    mb_table_erase(&mb_tracking.textures, context, (GLuint)texture);
    MbBufferEntry *found = mb_table_find(&mb_tracking.images, (uintptr_t)eglGetCurrentDisplay(), (uintptr_t)image);
    if (found && mb_tracking.textures.count < 4096) mb_table_remember(&mb_tracking.textures, context, (GLuint)texture, &found->descriptor);
    mb_tracking_unlock();
}

MB_EXPORT void GL_APIENTRY glDeleteTextures(GLsizei count, const GLuint *names) {
    RESOLVE(glDeleteTextures);
    if (mb_tracking_allowed() && names && count > 0) {
        mb_tracking_lock();
        uintptr_t context = (uintptr_t)eglGetCurrentContext();
        for (int i = 0; i < count; ++i) mb_table_erase(&mb_tracking.textures, context, names[i]);
        mb_tracking_unlock();
    }
    if (real_glDeleteTextures) real_glDeleteTextures(count, names);
}

MB_EXPORT EGLSyncKHR EGLAPIENTRY eglCreateSyncKHR(EGLDisplay display, EGLenum type, const EGLint *attributes) {
    RESOLVE(eglCreateSyncKHR);
    // Blur must precede the fence that consumers use to wait for this output.
    if (type == EGL_SYNC_NATIVE_FENCE_ANDROID && (!attributes || attributes[0] == EGL_NONE)) mb_filter_output();
    return real_eglCreateSyncKHR ? real_eglCreateSyncKHR(display, type, attributes) : EGL_NO_SYNC_KHR;
}

MB_EXPORT EGLSync EGLAPIENTRY eglCreateSync(EGLDisplay display, EGLenum type, const EGLAttrib *attributes) {
    RESOLVE(eglCreateSync);
    if (mb_sdk() >= 34 && type == EGL_SYNC_NATIVE_FENCE_ANDROID && (!attributes || attributes[0] == EGL_NONE)) mb_filter_output();
    return real_eglCreateSync ? real_eglCreateSync(display, type, attributes) : EGL_NO_SYNC;
}

MB_EXPORT EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay display, EGLContext context) {
    RESOLVE(eglDestroyContext);
    if (mb_tracking_allowed()) {
        mb_tracking_lock();
        mb_tracking_remove_context(context);
        mb_tracking_unlock();
    }
    return real_eglDestroyContext ? real_eglDestroyContext(display, context) : EGL_FALSE;
}

MB_EXPORT __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *name) {
    if (!name) return NULL;
#define MB_FUNCTION(function) if (!strcmp(name, #function)) return (__eglMustCastToProperFunctionPointerType)function
    if (mb_tracking_allowed()) {
        MB_FUNCTION(eglGetNativeClientBufferANDROID);
        MB_FUNCTION(eglCreateImageKHR);
        MB_FUNCTION(eglDestroyImageKHR);
        MB_FUNCTION(glEGLImageTargetTexture2DOES);
        MB_FUNCTION(glDeleteTextures);
        MB_FUNCTION(eglCreateSyncKHR);
        MB_FUNCTION(eglDestroyContext);
        if (mb_sdk() >= 34) {
            MB_FUNCTION(eglCreateImage);
            MB_FUNCTION(eglDestroyImage);
            MB_FUNCTION(eglCreateSync);
        }
    }
#undef MB_FUNCTION
    GetProc fallback = original_get_proc();
    return fallback ? fallback(name) : NULL;
}

__attribute__((constructor)) static void loaded(void) {
    if (mb_tracking_allowed()) MB_LOG(mb_log_enabled(), ANDROID_LOG_INFO, "Loaded in SurfaceFlinger; sdk=%d; strategy=%s; waiting for eligible GLES output", mb_sdk(), mb_strategy(mb_sdk()));
}
