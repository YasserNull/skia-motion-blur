#include "gl_state.h"
#include <stddef.h>

static const GLenum flags[MB_GL_STATE_FLAGS] = {GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
    GL_CULL_FACE, GL_DITHER, GL_RASTERIZER_DISCARD, GL_SAMPLE_COVERAGE, GL_SAMPLE_ALPHA_TO_COVERAGE};

// Save caller state before configuring unclipped, full-screen shader passes.
void mb_gl_save_state(MbGlState *state) {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state->draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state->read);
    glGetIntegerv(GL_VIEWPORT, state->viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &state->program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state->active);
    glGetBooleanv(GL_COLOR_WRITEMASK, state->mask);
    for (size_t i = 0; i < MB_GL_STATE_FLAGS; ++i) {
        state->enabled[i] = glIsEnabled(flags[i]);
        glDisable(flags[i]);
    }
    for (int i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->textures[i]);
        glGetIntegerv(GL_SAMPLER_BINDING, &state->samplers[i]);
        glBindSampler(i, 0);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void mb_gl_restore_state(const MbGlState *state) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state->draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, state->read);
    glViewport(state->viewport[0], state->viewport[1], state->viewport[2], state->viewport[3]);
    glUseProgram(state->program);
    glBindVertexArray(state->vao);
    glColorMask(state->mask[0], state->mask[1], state->mask[2], state->mask[3]);
    for (size_t i = 0; i < MB_GL_STATE_FLAGS; ++i)
        if (state->enabled[i]) glEnable(flags[i]);
    for (int i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, state->textures[i]);
        glBindSampler(i, state->samplers[i]);
    }
    glActiveTexture(state->active);
}
