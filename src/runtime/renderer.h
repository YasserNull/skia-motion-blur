#pragma once
#include <GLES3/gl3.h>
#include <renderengine/skia_motion_blur_config.h>

typedef struct {
    GLuint frames[2], framebuffers[2], flow, flow_framebuffer;
    GLuint original, original_framebuffer;
    GLuint motion_program, resolve_program, vao;
    GLuint guide, guide_framebuffer, guide_program;
    GLuint motion_query;
    GLint guide_uniforms[9], motion_uniforms[9], resolve_uniforms[9];
    unsigned width, height, current, history_width, history_height, scale;
    uint64_t previous, generation, session;
    uint64_t log_last, log_calls, log_no_history, log_clean, log_draw;
    bool ready, failed, repaint_needed;
} MbRenderer;

bool mb_renderer_initialize(MbRenderer *renderer, unsigned width, unsigned height, unsigned history_scale, bool logging);
bool mb_renderer_apply(MbRenderer *renderer, GLuint target, unsigned width, unsigned height,
                       const MbConfig *config, uint64_t active);
void mb_renderer_reset(MbRenderer *renderer);
void mb_renderer_destroy(MbRenderer *renderer);
