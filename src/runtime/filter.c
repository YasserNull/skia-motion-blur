#include "filter.h"
#include "tracking.h"
#include "policy.h"
#include "repaint.h"
#include "log.h"
#include <string.h>

void mb_filter_output(void) {
    if (!mb_tracking_allowed()) return;
    EGLContext context = eglGetCurrentContext();
    if (context == EGL_NO_CONTEXT) return;
    mb_tracking_lock();
    const MbConfig *settings = mb_config_acquire();
    if (!settings) { mb_tracking_unlock(); return; }
    const char *reason;
    do {
        uint64_t active = mb_lease();
        // A live controller lease and cleanup worker are required before blur.
        if (!active || !settings->enabled || settings->strength <= 0 || !mb_repaint_available()) {
            reason = !active ? "no-active-session" : !settings->enabled ? "disabled" : settings->strength <= 0 ? "zero-strength" : "repaint-unavailable";
            MbRenderer *existing = mb_tracking_renderer(context, false);
            if (existing) mb_renderer_reset(existing);
            break;
        }
        const char *version = (const char *)glGetString(GL_VERSION);
        if (!version || !strstr(version, "OpenGL ES 3")) { reason = "gles-version"; break; }
        GLint framebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        if (!framebuffer) { reason = "default-framebuffer"; break; }
        GLint type = 0, texture = 0, level = 0;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                             GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
        if (type != GL_TEXTURE) { reason = "attachment-not-texture"; break; }
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                             GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &texture);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                             GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &level);
        MbBufferEntry *found = mb_table_find(&mb_tracking.textures, (uintptr_t)context, (GLuint)texture);
        if (!found || level != 0) { reason = "untracked-texture-or-level"; break; }
        AHardwareBuffer_Desc descriptor = found->descriptor;
        // Protected buffers and unsupported formats must never enter shader passes.
        if (descriptor.layers != 1 || descriptor.width < 256 || descriptor.height < 256 ||
            (descriptor.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT) ||
            !(descriptor.usage & AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT) ||
            (descriptor.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM &&
             descriptor.format != AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM)) { reason = "ineligible-buffer"; break; }
        GLint encoding = 0;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                             GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
        if (encoding != GL_LINEAR || glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { reason = "framebuffer-encoding-or-incomplete"; break; }
        MbRenderer *current = mb_tracking_renderer(context, true);
        if (!current || !mb_renderer_apply(current, framebuffer, descriptor.width, descriptor.height, settings, active)) { reason = "renderer-unavailable"; break; }
        reason = "eligible-render";
        if (current->repaint_needed) mb_request_repaint(active);
        static uint64_t reported;
        if (mb_millis() - reported > 400) {
            reported = mb_millis();
            FILE *file = fopen("/data/misc/skia-motion-blur/skia-motion-blur.gles", "we");
            if (file) { fprintf(file, "%llu\n", (unsigned long long)reported); fclose(file); }
        }
        static bool logged;
        if (settings->log && !logged) {
            MB_LOG(settings->log, ANDROID_LOG_INFO, "Eligible GLES output found: %ux%u; strategy=%s",
                                descriptor.width, descriptor.height, mb_strategy(mb_sdk()));
            logged = true;
        }
    } while (false);
    if (settings->log) {
        static uint64_t last_log;
        uint64_t now = mb_millis();
        if (!last_log || now - last_log >= 5000) {
            MB_LOG(settings->log, ANDROID_LOG_INFO, "Hook result=%s buffers=%zu images=%zu textures=%zu contexts=%zu generation=%llu",
                reason, mb_tracking.buffers.count, mb_tracking.images.count, mb_tracking.textures.count, mb_tracking.context_count,
                (unsigned long long)settings->generation);
            last_log = now;
        }
    }
    mb_config_release(settings);
    mb_tracking_unlock();
}
