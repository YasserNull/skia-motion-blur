#pragma once
#include <GLES3/gl31.h>
#include <renderengine/skia_motion_blur_config.h>

typedef struct {
    GLuint frames[2], framebuffers[2], flow, flow_framebuffer;
    GLuint original, original_framebuffer;
    GLuint blur, blur_framebuffer, composite_program, single_target;
    MbTestMode test;
    const char *composite_path;
    uint64_t memory_bytes;
    unsigned memory_limit;
    bool parallel_guide, multisampled;
    GLuint motion_program, resolve_program, vao;
    GLuint guide, guide_framebuffer, guide_program;
    GLuint motion_query[2];
    bool motion_pending[2];
    unsigned next_motion_query;
    GLint guide_uniforms[11], motion_uniforms[11], resolve_uniforms[11], composite_uniforms[11];
    unsigned width, height, current, history_width, history_height, scale;
    uint64_t previous, generation, session, motion_until;
    uint64_t log_last, log_calls, log_no_history, log_clean, log_draw;
    bool ready, failed, repaint_needed;
} MbRenderer;

static inline const char *mb_renderer_guide_name(const MbRenderer *r) {
    if (!r->parallel_guide) return "fragment";
    return r->scale == 3 ? "compute-filtered" : "compute";
}

bool mb_renderer_initialize(MbRenderer *renderer, unsigned width, unsigned height, const MbConfig *config, bool multisampled);
bool mb_renderer_apply(MbRenderer *renderer, GLuint target, GLuint target_texture, unsigned width, unsigned height,
                       const MbConfig *config, uint64_t active, uint64_t touch_until);
void mb_renderer_reset(MbRenderer *renderer);
void mb_renderer_destroy(MbRenderer *renderer);
