#pragma once

#include <GLES3/gl3.h>

#define MB_GL_STATE_FLAGS 9

typedef struct {
    GLint draw, read, viewport[4], program, vao, active;
    GLint textures[4], samplers[4];
    GLboolean mask[4], enabled[MB_GL_STATE_FLAGS];
} MbGlState;

void mb_gl_save_state(MbGlState *state);
void mb_gl_restore_state(const MbGlState *state);
