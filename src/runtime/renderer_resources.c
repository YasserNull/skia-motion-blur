#include "renderer.h"
#include "shaders.h"
#include "log.h"
#include <utils.h>

#define ARRAY_SIZE(value) (sizeof(value) / sizeof((value)[0]))

static GLuint shader(GLenum kind, const char *source, bool logging) {
    GLuint result = glCreateShader(kind);
    glShaderSource(result, 1, &source, NULL);
    glCompileShader(result);
    GLint valid = 0;
    glGetShaderiv(result, GL_COMPILE_STATUS, &valid);
    if (!valid) {
        char log[1024] = {0};
        glGetShaderInfoLog(result, sizeof(log), NULL, log);
        MB_LOG(logging, ANDROID_LOG_WARN, "Shader compile failed: %s", log);
        glDeleteShader(result); return 0;
    }
    return result;
}

static GLuint program(const char *fragment, bool logging) {
    GLuint vertex = shader(GL_VERTEX_SHADER, vertexShader, logging), pixel = shader(GL_FRAGMENT_SHADER, fragment, logging);
    if (!vertex || !pixel) { glDeleteShader(vertex); glDeleteShader(pixel); return 0; }
    GLuint result = glCreateProgram();
    glAttachShader(result, vertex);
    glAttachShader(result, pixel);
    glLinkProgram(result);
    glDeleteShader(vertex);
    glDeleteShader(pixel);
    GLint valid = 0;
    glGetProgramiv(result, GL_LINK_STATUS, &valid);
    if (!valid) {
        char log[1024] = {0};
        glGetProgramInfoLog(result, sizeof(log), NULL, log);
        MB_LOG(logging, ANDROID_LOG_WARN, "Shader link failed: %s", log);
        glDeleteProgram(result); return 0;
    }
    return result;
}

static bool texture(GLuint *image, GLuint *framebuffer, unsigned width, unsigned height) {
    glGenTextures(1, image);
    glBindTexture(GL_TEXTURE_2D, *image);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, *framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *image, 0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static void locations(GLuint program, GLint result[9]) {
    static const char *const names[] = {"previousFrame", "currentFrame", "flowFrame",
        "mbExtent", "mbFlowScale", "mbStrength", "mbDebug", "guideFrame", "currentHistoryFrame"};
    for (size_t i = 0; i < ARRAY_SIZE(names); ++i) result[i] = glGetUniformLocation(program, names[i]);
}

void mb_renderer_reset(MbRenderer *renderer) {
    renderer->previous = 0;
    renderer->repaint_needed = false;
}

void mb_renderer_destroy(MbRenderer *r) {
    if (r->motion_query) glDeleteQueries(1, &r->motion_query);
    r->motion_query = 0;
    r->repaint_needed = false;
    glDeleteTextures(2, r->frames);
    glDeleteFramebuffers(2, r->framebuffers);
    glDeleteTextures(1, &r->guide);
    glDeleteFramebuffers(1, &r->guide_framebuffer);
    if (r->guide_program) glDeleteProgram(r->guide_program);
    r->guide = r->guide_framebuffer = r->guide_program = 0;
    glDeleteTextures(1, &r->flow);
    glDeleteFramebuffers(1, &r->flow_framebuffer);
    glDeleteTextures(1, &r->original);
    glDeleteFramebuffers(1, &r->original_framebuffer);
    if (r->motion_program) glDeleteProgram(r->motion_program);
    if (r->resolve_program) glDeleteProgram(r->resolve_program);
    if (r->vao) glDeleteVertexArrays(1, &r->vao);
    r->frames[0] = r->frames[1] = r->framebuffers[0] = r->framebuffers[1] = 0;
    r->flow = r->flow_framebuffer = r->motion_program = r->resolve_program = r->vao = 0;
    r->original = r->original_framebuffer = 0;
    r->ready = false;
    r->previous = 0;
}

bool mb_renderer_initialize(MbRenderer *r, unsigned w, unsigned h, unsigned history_scale, bool logging) {
    mb_renderer_destroy(r);
    r->history_width = mb_div_ceil(w, history_scale);
    r->history_height = mb_div_ceil(h, history_scale);
    r->guide_program = program(guideShader, logging);
    r->motion_program = program(motionShader, logging);
    r->resolve_program = program(resolveShader, logging);
    if (!r->guide_program || !r->motion_program || !r->resolve_program) return false;
    locations(r->guide_program, r->guide_uniforms);
    locations(r->motion_program, r->motion_uniforms);
    locations(r->resolve_program, r->resolve_uniforms);
    glGenVertexArrays(1, &r->vao);
    glGenQueries(1, &r->motion_query);
    // Downscaled history needs a full-resolution raw copy for sharp output.
    if (!texture(&r->guide, &r->guide_framebuffer, 1, 1) ||
        !texture(&r->frames[0], &r->framebuffers[0], r->history_width, r->history_height) ||
        !texture(&r->frames[1], &r->framebuffers[1], r->history_width, r->history_height) ||
        !texture(&r->flow, &r->flow_framebuffer, mb_div_ceil(r->history_width, 16), mb_div_ceil(r->history_height, 16)) ||
        (history_scale > 1 && !texture(&r->original, &r->original_framebuffer, w, h))) return false;
    r->width = w;
    r->height = h;
    r->scale = history_scale;
    r->current = 0;
    r->ready = true;
    return true;
}
