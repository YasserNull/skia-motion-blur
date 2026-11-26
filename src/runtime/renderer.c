#include "renderer.h"
#include "policy.h"
#include "gl_state.h"
#include "log.h"
#include <string.h>

static void uniforms(GLuint program, const GLint at[11], unsigned width, unsigned height,
                     float strength, bool debug, float scale_x, float scale_y, float radius_scale,
                     float interaction) {
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
    glUniform1f(at[9], radius_scale);
    glUniform1f(at[10], interaction);
}

static void run_guide(MbRenderer *r) {
    if (!r->parallel_guide) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->guide_framebuffer);
        glViewport(0, 0, 1, 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        return;
    }
    GLint name, level, layered, layer, access, format;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0, &name);
    glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, 0, &level);
    glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, 0, &layered);
    glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, 0, &layer);
    glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, 0, &access);
    glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, 0, &format);
    glBindImageTexture(0, r->guide, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    // Previous draws may still sample the guide when the next group overwrites it.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    glBindImageTexture(0, name, level, layered, layer, access, format);
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
    MB_LOG(config->log, ANDROID_LOG_INFO, "Renderer event=%s test=%s guide=%s composite=%s output=%ux%u history=%ux%u memory_bytes=%llu elapsed_ms=%llu exposure_ms=%.2f time_scale=%.2f strength=%.2f cleanup=%d calls=%llu draw=%llu clean=%llu no_history=%llu",
        event, mb_test_name(r->test), mb_renderer_guide_name(r), r->composite_path, w, h, hw, hh, (unsigned long long)memory, (unsigned long long)elapsed,
        config->exposure_ms, time_scale, config->strength, r->repaint_needed,
        (unsigned long long)r->log_calls, (unsigned long long)r->log_draw,
        (unsigned long long)r->log_clean,
        (unsigned long long)r->log_no_history);
    r->log_last = now;
    r->log_calls = r->log_draw = r->log_clean = r->log_no_history = 0;
}

bool mb_renderer_apply(MbRenderer *r, GLuint target, GLuint target_texture, unsigned w, unsigned h,
                       const MbConfig *config, uint64_t active, uint64_t touch_until) {
    bool cleanup_requested = r->repaint_needed;
    r->repaint_needed = false;
    if (!target || !target_texture || !w || !h) return false;
    if (!config->enabled || config->strength <= 0) {
        mb_renderer_reset(r);
        return true;
    }
    GLint callerQuery = 0;
    glGetQueryiv(GL_ANY_SAMPLES_PASSED, GL_CURRENT_QUERY, &callerQuery);
    // Skip this frame to leave a caller-owned occlusion query untouched.
    if (callerQuery) { mb_renderer_reset(r); return true; }
    unsigned history_scale = config->history_scale;
    if (history_scale < 1 || history_scale > 4) return false;
    unsigned hw = mb_div_ceil(w, history_scale), hh = mb_div_ceil(h, history_scale);
    GLint samples = 0;
    if (history_scale > 1 && (config->test == MB_TEST_BALANCED || config->test == MB_TEST_FETCH))
        glGetIntegerv(GL_SAMPLE_BUFFERS, &samples);
    bool multisampled = samples != 0;
    bool same_layout = r->width == w && r->height == h && r->scale == history_scale &&
                       r->test == config->test && r->multisampled == multisampled;
    if (r->failed && same_layout && r->memory_limit == config->memory_limit_mb) return false;
    MbGlState state;
    mb_gl_save_state(&state);
    if ((!r->ready || !same_layout || r->memory_bytes > (uint64_t)config->memory_limit_mb * 1024 * 1024) &&
        !mb_renderer_initialize(r, w, h, config, multisampled)) {
        mb_renderer_destroy(r);
        r->failed = true;
        MB_LOG(config->log, ANDROID_LOG_WARN, "Renderer unavailable for test=%s scale=%u limit=%u MiB",
               mb_test_name(config->test), history_scale, config->memory_limit_mb);
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
    if (!history) r->motion_until = 0;
    uint64_t elapsed = history ? now - r->previous : 0;
    // Scale the observed displacement to the requested exposure duration.
    float time_scale = elapsed ? config->exposure_ms / (float)elapsed : 1.0f;
    if (time_scale < 0.1f) time_scale = 0.1f;
    if (time_scale > 2.0f) time_scale = 2.0f;
    uint64_t interaction_until = touch_until > r->motion_until ? touch_until : r->motion_until;
    float interaction = interaction_until > now ? (float)(interaction_until - now) / 250.0f : 0.0f;
    if (interaction > 1.0f) interaction = 1.0f;
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
        uniforms(r->guide_program, r->guide_uniforms, hw, hh, config->strength, config->debug, 1, 1, 1, interaction);
        run_guide(r);
        glBindTexture(GL_TEXTURE_2D, r->guide);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->flow_framebuffer);
        glViewport(0, 0, mb_div_ceil(hw, 16), mb_div_ceil(hh, 16));
        uniforms(r->motion_program, r->motion_uniforms, hw, hh, config->strength, config->debug, 1, 1, 1, interaction);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, r->flow);
        // The guide pass is complete; reuse its unit for current raw history.
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, r->frames[r->current]);
        if (r->blur) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->blur_framebuffer);
            glViewport(0, 0, hw, hh);
            uniforms(r->resolve_program, r->resolve_uniforms, hw, hh,
                     config->strength * time_scale, config->debug, 1, 1, (float)hw / w, interaction);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, r->original);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, r->blur);
            uniforms(r->composite_program, r->composite_uniforms, w, h,
                     config->strength, config->debug, 1, 1, 1, interaction);
        } else {
            uniforms(r->resolve_program, r->resolve_uniforms, w, h,
                     config->strength * time_scale, config->debug, 1, 1, 1, interaction);
        }
        if (r->single_target) {
            // ARM drivers without MRT fetch require attachment zero to be the only image.
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->single_target);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);
        } else {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
        }
        glViewport(0, 0, w, h);
        // Reading a query result before it is ready stalls SurfaceFlinger on the GPU.
        bool observed = false, changed = false;
        for (unsigned i = 0; i < 2; ++i) {
            if (!r->motion_pending[i]) continue;
            GLuint available = 0;
            glGetQueryObjectuiv(r->motion_query[i], GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available) continue;
            GLuint samples = 0;
            glGetQueryObjectuiv(r->motion_query[i], GL_QUERY_RESULT, &samples);
            r->motion_pending[i] = false;
            observed = true;
            changed |= samples != 0;
        }
        if (changed) r->motion_until = mb_millis() + 250;
        // A cleanup frame needs no blur pass once the prior query found no motion.
        if (!(cleanup_requested && observed && !changed &&
              !r->motion_pending[0] && !r->motion_pending[1])) {
            unsigned query = r->next_motion_query;
            if (r->motion_pending[query]) query = 1 - query;
            if (!r->motion_pending[query] && r->motion_query[query]) {
                glBeginQuery(GL_ANY_SAMPLES_PASSED, r->motion_query[query]);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glEndQuery(GL_ANY_SAMPLES_PASSED);
                r->motion_pending[query] = true;
                r->next_motion_query = 1 - query;
                if (!observed) {
                    GLuint available = 0;
                    glGetQueryObjectuiv(r->motion_query[query], GL_QUERY_RESULT_AVAILABLE, &available);
                    if (available) {
                        GLuint samples = 0;
                        glGetQueryObjectuiv(r->motion_query[query], GL_QUERY_RESULT, &samples);
                        r->motion_pending[query] = false;
                        observed = true;
                        changed |= samples != 0;
                    }
                }
            } else {
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            r->repaint_needed = !config->debug &&
                (changed || (r->motion_pending[0] || r->motion_pending[1]));
        }
        if (r->single_target)
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }
    trace(r, config, w, h, hw, hh, r->memory_bytes, elapsed, time_scale,
               !history ? "no-history" : r->repaint_needed ? "draw" : "clean");
    r->current = 1 - r->current;
    mb_gl_restore_state(&state);
    return true;
}
