#include "renderer.h"
#include "shaders.h"
#include "log.h"
#include <utils.h>
#include <string.h>
#include <GLES2/gl2ext.h>

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

static GLuint link_program(GLuint stage, GLuint vertex, bool logging) {
    GLuint result = glCreateProgram();
    if (vertex) glAttachShader(result, vertex);
    glAttachShader(result, stage);
    glLinkProgram(result);
    if (vertex) glDeleteShader(vertex);
    glDeleteShader(stage);
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

static GLuint program(const char *fragment, bool logging) {
    GLuint vertex = shader(GL_VERTEX_SHADER, vertexShader, logging);
    if (!vertex) return 0;
    GLuint pixel = shader(GL_FRAGMENT_SHADER, fragment, logging);
    if (!pixel) { glDeleteShader(vertex); return 0; }
    return link_program(pixel, vertex, logging);
}

static bool extension(const char *name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        if (!strcmp((const char *)glGetStringi(GL_EXTENSIONS, i), name)) return true;
    }
    return false;
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

static void locations(GLuint program, GLint result[11]) {
    static const char *const names[] = {"previousFrame", "currentFrame", "flowFrame",
        "mbExtent", "mbFlowScale", "mbStrength", "mbDebug", "guideFrame", "currentHistoryFrame", "mbRadiusScale", "mbInteraction"};
    for (size_t i = 0; i < ARRAY_SIZE(names); ++i) result[i] = glGetUniformLocation(program, names[i]);
}

void mb_renderer_reset(MbRenderer *renderer) {
    renderer->previous = 0;
    renderer->motion_until = 0;
    renderer->repaint_needed = false;
}

void mb_renderer_destroy(MbRenderer *r) {
    glDeleteQueries(2, r->motion_query);
    r->motion_query[0] = r->motion_query[1] = 0;
    r->motion_pending[0] = r->motion_pending[1] = false;
    r->next_motion_query = 0;
    r->repaint_needed = false;
    glDeleteFramebuffers(1, &r->single_target);
    r->single_target = 0;
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
    glDeleteTextures(1, &r->blur);
    glDeleteFramebuffers(1, &r->blur_framebuffer);
    if (r->composite_program) glDeleteProgram(r->composite_program);
    r->blur = r->blur_framebuffer = r->composite_program = 0;
    if (r->motion_program) glDeleteProgram(r->motion_program);
    if (r->resolve_program) glDeleteProgram(r->resolve_program);
    if (r->vao) glDeleteVertexArrays(1, &r->vao);
    r->frames[0] = r->frames[1] = r->framebuffers[0] = r->framebuffers[1] = 0;
    r->flow = r->flow_framebuffer = r->motion_program = r->resolve_program = r->vao = 0;
    r->original = r->original_framebuffer = 0;
    r->ready = false;
    r->previous = 0;
}

bool mb_renderer_initialize(MbRenderer *r, unsigned w, unsigned h, const MbConfig *config, bool multisampled) {
    mb_renderer_destroy(r);
    r->width = w;
    r->height = h;
    r->scale = config->history_scale;
    r->multisampled = multisampled;
    r->memory_limit = config->memory_limit_mb;
    r->failed = false;
    r->parallel_guide = false;
    r->composite_path = r->scale == 1 ? "direct" : "copy";
    r->history_width = mb_div_ceil(w, r->scale);
    r->history_height = mb_div_ceil(h, r->scale);
    unsigned hw = r->history_width, hh = r->history_height;
    unsigned fw = mb_div_ceil(hw, 16), fh = mb_div_ceil(hh, 16);
    bool logging = config->log;

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major > 3 || (major == 3 && minor >= 1)) {
        const char *source = r->scale == 1 ? fullGuideComputeShader :
            r->scale == 3 ? filteredGuideComputeShader : guideComputeShader;
        GLuint compute = shader(GL_COMPUTE_SHADER, source, logging);
        if (compute) r->guide_program = link_program(compute, 0, logging);
        r->parallel_guide = r->guide_program != 0;
    }
    if (!r->guide_program) r->guide_program = program(r->scale == 1 ? fullGuideShader : guideShader, logging);
    r->motion_program = program(motionShader, logging);
    r->resolve_program = program(r->scale > 1 ? blurShader : fullResolveShader, logging);
    if (!r->guide_program || !r->motion_program || !r->resolve_program) return false;
    locations(r->guide_program, r->guide_uniforms);
    locations(r->motion_program, r->motion_uniforms);
    locations(r->resolve_program, r->resolve_uniforms);

    bool copy = false;
    if (r->scale > 1) {
        if (!multisampled) {
            if (extension("GL_EXT_shader_framebuffer_fetch")) {
                r->composite_program = program(compositeExtShader, logging);
                if (r->composite_program) r->composite_path = "fetch-ext";
            }
            if (!r->composite_program && extension("GL_ARM_shader_framebuffer_fetch")) {
                r->composite_program = program(compositeArmShader, logging);
                if (r->composite_program) {
                    r->composite_path = "fetch-arm";
                    GLboolean mrt = GL_FALSE;
                    glGetBooleanv(GL_FRAGMENT_SHADER_FRAMEBUFFER_FETCH_MRT_ARM, &mrt);
                    if (!mrt) {
                        glGenFramebuffers(1, &r->single_target);
                        if (!r->single_target) {
                            glDeleteProgram(r->composite_program);
                            r->composite_program = 0;
                            r->composite_path = "copy";
                        }
                    }
                }
            }
        }
        copy = !r->composite_program;
        if (copy) r->composite_program = program(compositeShader, logging);
        if (!r->composite_program) return false;
        locations(r->composite_program, r->composite_uniforms);
    }
    uint64_t pixels = (uint64_t)hw * hh;
    r->memory_bytes = 4 + pixels * 8 + (uint64_t)fw * fh * 4;
    if (r->scale > 1) r->memory_bytes += pixels * 4;
    if (copy) r->memory_bytes += (uint64_t)w * h * 4;
    if (r->memory_bytes > (uint64_t)r->memory_limit * 1024 * 1024) {
        MB_LOG(logging, ANDROID_LOG_WARN, "Memory limit: need %llu bytes, limit %u MiB",
               (unsigned long long)r->memory_bytes, r->memory_limit);
        return false;
    }
    glGenVertexArrays(1, &r->vao);
    glGenQueries(2, r->motion_query);
    if (!texture(&r->guide, &r->guide_framebuffer, 1, 1) ||
        !texture(&r->frames[0], &r->framebuffers[0], hw, hh) ||
        !texture(&r->frames[1], &r->framebuffers[1], hw, hh) ||
        !texture(&r->flow, &r->flow_framebuffer, fw, fh) ||
        (r->scale > 1 && !texture(&r->blur, &r->blur_framebuffer, hw, hh)) ||
        (copy && !texture(&r->original, &r->original_framebuffer, w, h))) return false;
    r->current = 0;
    r->ready = true;
    MB_LOG(logging, ANDROID_LOG_INFO, "Renderer guide=%s composite=%s history=%ux%u memory_bytes=%llu",
           mb_renderer_guide_name(r), r->composite_path,
           hw, hh, (unsigned long long)r->memory_bytes);
    return true;
}
