#include "renderer.h"
#include "policy.h"
#include "gl_state.h"
#include "log.h"
#include <string.h>

static void uniforms(GLuint program, const GLint at[9], unsigned width, unsigned height,
                     float strength, bool debug, float scale_x, float scale_y) {
    glUseProgram(program);
    glUniform1i(at[0], 0);
    glUniform1i(at[1], 1);
    glUniform1i(at[2], 2);
    glUniform1i(at[7], 3);
    glUniform1i(at[8], 3);
    glUniform2f(at[3], width, height);
    glUniform2f(at[4], scale_x, scale_y);
    glUniform1f(at[5], strength);
    glUniform1f(at[6], debug ? 1 : 0);
}

static void trace(MbRenderer *r, const MbConfig *config, unsigned w, unsigned h,
                  unsigned hw, unsigned hh, uint64_t memory, uint64_t elapsed,
                  float time_scale, const char *event) {
    if (!config->log) return;
    ++r->log_calls;
    if (!strcmp(event, "no-history")) ++r->log_no_history;
    else if (!strcmp(event, "clean")) ++r->log_clean;
    else if (!strcmp(event, "draw")) ++r->log_draw;
    uint64_t now = mb_millis();
    if (r->log_last && now - r->log_last < 5000) return;
    MB_LOG(config->log, ANDROID_LOG_INFO, "Renderer event=%s output=%ux%u history=%ux%u memory_bytes=%llu elapsed_ms=%llu exposure_ms=%.2f time_scale=%.2f strength=%.2f cleanup=%d calls=%llu draw=%llu clean=%llu no_history=%llu",
        event, w, h, hw, hh, (unsigned long long)memory, (unsigned long long)elapsed,
        config->exposure_ms, time_scale, config->strength, r->repaint_needed,
        (unsigned long long)r->log_calls, (unsigned long long)r->log_draw,
        (unsigned long long)r->log_clean,
        (unsigned long long)r->log_no_history);
    r->log_last = now;
    r->log_calls = r->log_draw = r->log_clean = r->log_no_history = 0;
}

bool mb_renderer_apply(MbRenderer *r, GLuint target, unsigned w, unsigned h,
                       const MbConfig *config, uint64_t active) {
    r->repaint_needed = false;
    if (r->failed || !target || !w || !h) return false;
    GLint callerQuery = 0;
    glGetQueryiv(GL_ANY_SAMPLES_PASSED, GL_CURRENT_QUERY, &callerQuery);
    // Skip this frame to leave a caller-owned occlusion query untouched.
    if (callerQuery) { mb_renderer_reset(r); return true; }
    unsigned history_scale = config->history_scale;
    if (history_scale < 1 || history_scale > 4) return false;
    unsigned hw = mb_div_ceil(w, history_scale), hh = mb_div_ceil(h, history_scale);
    uint64_t memory = 4 + (uint64_t)hw * hh * 8 + (uint64_t)mb_div_ceil(hw, 16) * mb_div_ceil(hh, 16) * 4;
    if (history_scale > 1) memory += (uint64_t)(w) * h * 4;
    if (memory > (uint64_t)(config->memory_limit_mb) * 1024 * 1024) {
        static uint64_t last_warning;
        uint64_t now = mb_millis();
        if (config->log && (!last_warning || now - last_warning >= 5000)) {
            MB_LOG(config->log, ANDROID_LOG_WARN, "Memory limit: need %llu bytes, limit %u MiB",
                                (unsigned long long)memory, config->memory_limit_mb);
            last_warning = now;
        }
        mb_renderer_reset(r); return false;
    }
    MbGlState state;
    mb_gl_save_state(&state);
    if ((!r->ready || r->width != w || r->height != h || r->scale != history_scale) && !mb_renderer_initialize(r, w, h, history_scale, config->log)) {
        mb_renderer_destroy(r);
        r->failed = true;
        MB_LOG(config->log, ANDROID_LOG_WARN, "Shader or framebuffer initialization failed; disabled for context");
        mb_gl_restore_state(&state);
        return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target);
    if (r->original) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->original_framebuffer);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, r->original_framebuffer);
    }
    // Keep raw input as history so blur never feeds back into motion estimation.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->framebuffers[r->current]);
    glBlitFramebuffer(0, 0, w, h, 0, 0, hw, hh, GL_COLOR_BUFFER_BIT, history_scale > 1 ? GL_LINEAR : GL_NEAREST);
    uint64_t now = mb_millis();
    bool history = r->previous && now - r->previous <= config->history_timeout_ms &&
                   r->generation == config->generation && r->session == active;
    uint64_t elapsed = history ? now - r->previous : 0;
    // Scale the observed displacement to the requested exposure duration.
    float time_scale = elapsed ? config->exposure_ms / (float)elapsed : 1.0f;
    if (time_scale < 0.1f) time_scale = 0.1f;
    if (time_scale > 2.0f) time_scale = 2.0f;
    r->previous = now;
    r->generation = config->generation;
    r->session = active;
    if (history) {
        glBindVertexArray(r->vao);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r->frames[1 - r->current]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->frames[r->current]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->guide_framebuffer);
        glViewport(0, 0, 1, 1);
        uniforms(r->guide_program, r->guide_uniforms, hw, hh, config->strength, config->debug, 1, 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, r->guide);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->flow_framebuffer);
        glViewport(0, 0, mb_div_ceil(hw, 16), mb_div_ceil(hh, 16));
        uniforms(r->motion_program, r->motion_uniforms, hw, hh, config->strength, config->debug, 1, 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, r->flow);
        // The guide pass is complete; reuse its unit for current raw history.
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, r->frames[r->current]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->original ? r->original : r->frames[r->current]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
        glViewport(0, 0, w, h);
        uniforms(r->resolve_program, r->resolve_uniforms, w, h, config->strength * time_scale, config->debug, (float)w / hw, (float)h / hh);
        glBeginQuery(GL_ANY_SAMPLES_PASSED, r->motion_query);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        GLuint changed = 0;
        // A previous frame's query can miss motion in the current frame.
        glGetQueryObjectuiv(r->motion_query, GL_QUERY_RESULT, &changed);
        r->repaint_needed = !config->debug && changed != 0;
    }
    trace(r, config, w, h, hw, hh, memory, elapsed, time_scale,
               !history ? "no-history" : r->repaint_needed ? "draw" : "clean");
    r->current = 1 - r->current;
    mb_gl_restore_state(&state);
    return true;
}
