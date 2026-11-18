#include "renderer.h"
#include "policy.h"
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned width = 128, height = 128, scale = 2;
static bool benchmark;
static GLuint sampler, texture, target;
static MbRenderer renderer;
static MbConfig *config;

#ifdef MB_TEST_CLOCK
static bool renderer_clock;
static uint64_t render_time_ms = 1000;
static unsigned frame_interval_ms = 16;
int __real_clock_gettime(clockid_t clock, struct timespec *value);
int __wrap_clock_gettime(clockid_t clock, struct timespec *value) {
    if (renderer_clock && clock == CLOCK_MONOTONIC) {
        value->tv_sec = (time_t)(render_time_ms / 1000);
        value->tv_nsec = (long)(render_time_ms % 1000) * 1000000;
        return 0;
    }
    return __real_clock_gettime(clock, value);
}
#endif

static void require(bool valid, const char *text) {
    if (!valid) { fprintf(stderr, "FAIL: %s; egl=0x%x gl=0x%x\n", text, eglGetError(), glGetError()); exit(1); }
}

static size_t frame_size(void) { return (size_t)width * height * 4; }

static unsigned char *allocate_frame(void) {
    unsigned char *result = malloc(frame_size());
    require(result != NULL, "Frame allocation");
    return result;
}

static unsigned char *pixels(int shift) {
    unsigned char *result = allocate_frame();
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        unsigned seed = ((x - shift + width) % width) * 173 + y * 311;
        seed = (seed ^ (seed << 13)) * 1274126177u;
        unsigned at = (y * width + x) * 4;
        result[at] = seed & 255;
        result[at + 1] = (seed >> 8) & 255;
        result[at + 2] = (seed >> 16) & 255;
        result[at + 3] = 255;
    }
    return result;
}

#ifndef MB_TEST_COMPLEX_DIAGONAL
static unsigned grid_color(unsigned x, unsigned y) {
    unsigned seed = x * 173 + y * 311;
    return (seed ^ (seed << 13)) * 1274126177u;
}
#endif

static unsigned char *diagonal(int dx, int dy) {
    unsigned char *result = allocate_frame();
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
#ifdef MB_TEST_COMPLEX_DIAGONAL
        // Dense random textures can produce ambiguous diagonal matches.
        unsigned seed = ((x - dx + width) % width) * 173 + ((y - dy + height) % height) * 311;
        seed = (seed ^ (seed << 13)) * 1274126177u;
        unsigned at = (y * width + x) * 4;
        result[at] = seed & 255;
        result[at + 1] = (seed >> 8) & 255;
        result[at + 2] = (seed >> 16) & 255;
#else
        unsigned sx = (x - dx + width) % width, sy = (y - dy + height) % height;
        unsigned fx = sx % 32, fy = sy % 32;
        unsigned colors[] = {grid_color(sx / 32, sy / 32), grid_color(sx / 32 + 1, sy / 32),
                             grid_color(sx / 32, sy / 32 + 1), grid_color(sx / 32 + 1, sy / 32 + 1)};
        unsigned at = (y * width + x) * 4;
        for (unsigned channel = 0; channel < 3; ++channel) {
            unsigned a = (colors[0] >> (channel * 8)) & 255, b = (colors[1] >> (channel * 8)) & 255;
            unsigned c = (colors[2] >> (channel * 8)) & 255, d = (colors[3] >> (channel * 8)) & 255;
            result[at + channel] = (unsigned char)((a * (32 - fx) * (32 - fy) + b * fx * (32 - fy) +
                                                  c * (32 - fx) * fy + d * fx * fy) / 1024);
        }
#endif
        result[at + 3] = 255;
    }
    return result;
}

static unsigned char *terminal(int shift) {
    unsigned char *result = allocate_frame();
    unsigned zoom = width / 360 > 2 ? width / 360 : 2, bottom = height * 2 / 5;
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        unsigned kx = x / zoom, ky = y / zoom;
        unsigned key_bits = ((kx / 8 + ky / 12 * 7) % 31 + 1) * 2654435761u;
        bool key_ink = kx % 8 < 5 && ky % 12 < 7 && ((key_bits >> ((kx % 8 + ky % 12 * 5) % 31)) & 1);
        unsigned value = key_ink ? 32 : 235;
        if (y >= bottom) {
            unsigned sx = x / zoom, sy = (y - bottom + shift) / zoom;
            unsigned glyph = (sx / 8 + (sy / 12) * 17) % 31 + 1;
            unsigned bits = glyph * 2654435761u;
            bool ink = sx % 8 < 5 && sy % 12 < 7 && ((bits >> ((sx % 8 + sy % 12 * 5) % 31)) & 1);
            value = ink ? 235 : 0;
        }
        unsigned at = (y * width + x) * 4;
        result[at] = result[at + 1] = result[at + 2] = value;
        result[at + 3] = 255;
    }
    return result;
}

static unsigned char *submit(const unsigned char *input, uint64_t session) {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(3, sampler);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, input);
    glViewport(2, 3, 40, 50);
    glEnable(GL_SCISSOR_TEST);
    glScissor(1, 1, 10, 10);
    glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    if (benchmark) glFinish();
    struct timespec start, finish;
    clock_gettime(CLOCK_MONOTONIC, &start);
#ifdef MB_TEST_CLOCK
    render_time_ms += frame_interval_ms;
    renderer_clock = true;
#endif
    bool applied = mb_renderer_apply(&renderer, target, width, height, config, session);
#ifdef MB_TEST_CLOCK
    renderer_clock = false;
#endif
    require(applied, "Renderer applied");
    if (benchmark) {
        glFinish();
        clock_gettime(CLOCK_MONOTONIC, &finish);
        double elapsed = (finish.tv_sec - start.tv_sec) * 1000.0 + (finish.tv_nsec - start.tv_nsec) / 1000000.0;
        printf("BENCH: %.3f ms; %ux%u; history_scale=%u; strength=%.1f; session=%llu\n",
               elapsed, width, height, scale, config->strength, (unsigned long long)session);
        fflush(stdout);
    }
    GLint viewport[4] = {0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLboolean mask[4] = {0};
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    require(viewport[0] == 2 && viewport[1] == 3 && viewport[2] == 40 && viewport[3] == 50, "Viewport restored");
    require(glIsEnabled(GL_SCISSOR_TEST) && mask[0] && !mask[1] && mask[2] && !mask[3], "GL state restored");
    GLint binding = 0, sampling = 0;
    glActiveTexture(GL_TEXTURE3);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    glGetIntegerv(GL_SAMPLER_BINDING, &sampling);
    require(binding == (GLint)texture && sampling == (GLint)sampler, "Guide texture unit restored");
    glActiveTexture(GL_TEXTURE0);
    unsigned char *output = allocate_frame();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, output);
    require(glGetError() == GL_NO_ERROR, "No GL errors");
    if (benchmark) {
        // Compare outputs outside the timed region.
        uint64_t hash = 14695981039346656037ull;
        for (size_t i = 0; i < frame_size(); ++i) { hash ^= output[i]; hash *= 1099511628211ull; }
        printf("OUTPUT: %016llx\n", (unsigned long long)hash);
    }
    return output;
}

static unsigned char *submit_generated(unsigned char *input, uint64_t session) {
    unsigned char *output = submit(input, session);
    free(input);
    return output;
}

static void expect_equal(unsigned char *output, const unsigned char *expected, const char *message) {
    require(!memcmp(output, expected, frame_size()), message);
    free(output);
}

int main(int argc, char **argv) {
    config = mb_config_create();
    require(config != NULL, "Configuration allocation");
    benchmark = argc == 5 && !strcmp(argv[1], "--benchmark");
    if (benchmark) {
        width = (unsigned)strtoul(argv[2], NULL, 10);
        height = (unsigned)strtoul(argv[3], NULL, 10);
        scale = (unsigned)strtoul(argv[4], NULL, 10);
        require(width >= 64 && height >= 64 && width <= 4096 && height <= 4096 && scale >= 1 && scale <= 4,
                "Benchmark dimensions and history scale");
    } else require(argc == 1, "Usage: gles_test [--benchmark width height history_scale]");
    for (int version = 1; version < 50; ++version)
        require(mb_supported(version) == (version >= 31 && version <= 36), "SDK bounds");
    require(!strcmp(mb_strategy(33), "egl-khr"), "Android 13 strategy");
    require(!strcmp(mb_strategy(34), "egl-khr-core"), "Android 14 strategy");
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    require(eglInitialize(display, NULL, NULL), "EGL initialization");
    EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig selected = NULL;
    EGLint count = 0;
    require(eglChooseConfig(display, attributes, &selected, 1, &count) && count == 1, "ES3 config");
    EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, selected, EGL_NO_CONTEXT, context_attributes);
    EGLint surface_attributes[] = {EGL_WIDTH, (EGLint)width, EGL_HEIGHT, (EGLint)height, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, selected, surface_attributes);
    require(eglMakeCurrent(display, surface, surface, context), "ES3 context");
    printf("GPU: %s\n", glGetString(GL_RENDERER));
    glGenSamplers(1, &sampler);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    glGenFramebuffers(1, &target);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Output framebuffer");
    config->enabled = true;
    config->strength = 1;
    config->history_timeout_ms = benchmark ? 10000 : 250;
    config->history_scale = scale;
    unsigned char *frame = pixels(0);
    expect_equal(submit(frame, 1), frame, "First frame preserved");
    require(!renderer.repaint_needed, "First frame does not schedule cleanup");
    expect_equal(submit(frame, 1), frame, "Static frame preserved");
    require(!renderer.repaint_needed, "Static frame does not schedule cleanup");
    unsigned char *shifted = pixels(8);
    unsigned char *blurred = submit(shifted, 1);
    size_t changed = 0;
    for (size_t i = 0; i < frame_size(); i += 4) if (blurred[i] != shifted[i]) ++changed;
    require(changed > width * height / 8, "Motion changes output pixels");
    require(renderer.repaint_needed, "Blurred output requests a cleanup frame");
    expect_equal(submit(shifted, 1), shifted, "First unchanged frame immediately clears blur");
    require(!renderer.repaint_needed, "Clean frame stops cleanup requests");
    config->strength = 3;
    expect_equal(submit(frame, 7), frame, "Default strength preserves first frame");
    unsigned char *default_blur = submit(shifted, 7);
    require(memcmp(default_blur, blurred, frame_size()) && memcmp(default_blur, shifted, frame_size()),
            "Strength three increases moving-image blur");
    require(renderer.repaint_needed, "Default strength requests cleanup");
    expect_equal(submit(shifted, 7), shifted, "Default strength clears on first still frame");
    require(!renderer.repaint_needed, "Default strength stops cleanup at rest");
    unsigned char *fast = pixels(24);
    free(submit(frame, 8));
    unsigned char *fast_default = submit(fast, 8);
    config->strength = 8;
    expect_equal(submit(frame, 9), frame, "Maximum strength preserves first frame");
    unsigned char *strongest = submit(fast, 9);
    require(memcmp(strongest, fast_default, frame_size()) && memcmp(strongest, fast, frame_size()),
            "Strength eight increases blur beyond strength three");
    require(renderer.repaint_needed, "Maximum strength requests cleanup");
    expect_equal(submit(fast, 9), fast, "Maximum strength clears on first still frame");
    require(!renderer.repaint_needed, "Maximum strength stops cleanup at rest");
    config->strength = 1;
    expect_equal(submit(shifted, 2), shifted, "Foreground session clears history");
    config->strength = 0;
    expect_equal(submit(frame, 2), frame, "Zero strength preserves output");
    require(!renderer.repaint_needed, "Zero strength does not schedule cleanup");
    config->strength = 1;
    config->history_scale = scale == 4 ? 2 : 4;
    expect_equal(submit(shifted, 2), shifted, "History scale change clears history");
    expect_equal(submit(shifted, 2), shifted, "Static frame remains sharp after scale change");
    config->history_scale = scale;
    GLuint foreign_query = 0;
    glGenQueries(1, &foreign_query);
    glBeginQuery(GL_ANY_SAMPLES_PASSED, foreign_query);
    GLint caller_query = 0;
    glGetQueryiv(GL_ANY_SAMPLES_PASSED, GL_CURRENT_QUERY, &caller_query);
    require(caller_query != 0, "Caller query is active before renderer");
    expect_equal(submit(frame, 2), frame, "Existing query bypasses blur safely");
    GLint active_query = 0;
    glGetQueryiv(GL_ANY_SAMPLES_PASSED, GL_CURRENT_QUERY, &active_query);
    require(active_query == caller_query, "Caller query remains active");
    glEndQuery(GL_ANY_SAMPLES_PASSED);
    glDeleteQueries(1, &foreign_query);
    if (width >= 256 && height >= 256) {
        config->debug = true;
        ++config->generation;
        free(submit_generated(terminal(0), 4));
        for (int direction = 1; direction >= -1; direction -= 2) {
            unsigned char *motion = submit_generated(terminal(direction > 0 ? 20 : 0), 4);
            unsigned moving = 0, aligned = 0;
            for (unsigned y = height / 2; y < height * 9 / 10; ++y) for (unsigned x = width / 10; x < width * 4 / 10; ++x) {
                unsigned at = (y * width + x) * 4;
                if (motion[at + 2] > 128) {
                    ++moving;
                    if (abs((int)motion[at] - 128) < 24 && (direction > 0 ? motion[at + 1] > 128 : motion[at + 1] < 128)) ++aligned;
                }
            }
            printf("SCROLL: %u/%u confident pixels aligned vertically\n", aligned, moving);
            require(moving > width * height / 100, "Scrolling text detected above stationary keyboard");
            require(aligned > moving * 8 / 10, "Scrolling text has a consistent direction");
            free(motion);
        }
        config->debug = false;
        ++config->generation;
        unsigned char *text = terminal(20);
        free(submit_generated(terminal(0), 5));
        unsigned char *text_blurred = submit(text, 5);
        require(!memcmp(text, text_blurred, width * (height * 2 / 5 - 8) * 4), "Stationary keyboard preserved while text scrolls");
        expect_equal(submit(text, 5), text, "Raw history leaves stopped text sharp");
        free(text_blurred);
        free(text);
        free(submit_generated(terminal(0), 6));
        unsigned char *pressed = terminal(20), *source = allocate_frame();
        memcpy(source, pressed, frame_size());
        for (unsigned y = 0; y < height * 2 / 5; ++y) for (unsigned x = 0; x < width; ++x)
            for (unsigned channel = 0; channel < 4; ++channel)
                pressed[(y * width + x) * 4 + channel] = source[(y * width + (x + 8) % width) * 4 + channel];
        unsigned char *blurred_keys = submit(pressed, 6);
        unsigned changed_keys = 0;
        for (unsigned i = 0; i < width * (height * 2 / 5) * 4; i += 4)
            if (pressed[i] != blurred_keys[i]) ++changed_keys;
        require(changed_keys > width * (height * 2 / 5) / 100, "Moving keyboard glyphs receive blur");
        require(renderer.repaint_needed, "Keyboard blur requests cleanup");
        expect_equal(submit(pressed, 6), pressed, "First still keyboard frame is exact");
        require(!renderer.repaint_needed, "Still keyboard stops cleanup requests");
        free(blurred_keys);
        free(source);
        free(pressed);
        config->debug = true;
        ++config->generation;
        free(submit_generated(diagonal(0, 0), 14));
        for (int direction = -1; direction <= 1; direction += 2) {
            unsigned char *motion = submit_generated(diagonal(direction < 0 ? 8 : 0, direction < 0 ? 12 : 0), 14);
            unsigned moving = 0, aligned = 0;
            for (unsigned y = height / 10; y < height * 9 / 10; ++y)
                for (unsigned x = width / 10; x < width * 4 / 10; ++x) {
                    unsigned at = (y * width + x) * 4;
                    if (motion[at + 2] > 128) {
                        ++moving;
                        if (direction < 0 ? motion[at] < 128 && motion[at + 1] < 128 :
                                            motion[at] > 128 && motion[at + 1] > 128) ++aligned;
                    }
                }
            printf("DIAGONAL: %u/%u confident pixels aligned\n", aligned, moving);
            require(moving > width * height / 100, "Diagonal motion detected");
            require(aligned > moving * 8 / 10, "Diagonal motion has consistent signs on both axes");
            free(motion);
        }
        config->debug = false;
        ++config->generation;
    }
#ifdef MB_TEST_CLOCK
    config->strength = 2;
    config->exposure_ms = 8;
    ++config->generation;
    free(submit(frame, 10));
    unsigned char *short_exposure = submit(shifted, 10);
    config->exposure_ms = 32;
    ++config->generation;
    free(submit(frame, 11));
    unsigned char *long_exposure = submit(shifted, 11);
    require(memcmp(short_exposure, long_exposure, frame_size()), "Exposure duration changes moving-image blur");
    expect_equal(submit(shifted, 11), shifted, "Exposure change still clears the first stationary frame");
    free(short_exposure);
    free(long_exposure);
    config->exposure_ms = 16;
    ++config->generation;
    free(submit(frame, 12));
    unsigned char *normal_rate = submit(shifted, 12);
    config->exposure_ms = 32;
    frame_interval_ms = 32;
    ++config->generation;
    free(submit(frame, 13));
    unsigned char *half_rate = submit(shifted, 13);
    require(!memcmp(normal_rate, half_rate, frame_size()), "Equal exposure-to-frame ratios produce identical blur");
    free(normal_rate);
    free(half_rate);
    config->exposure_ms = 16.667f;
    config->strength = 1;
    frame_interval_ms = 16;
    ++config->generation;
#endif
    if (benchmark) {
        free(submit(frame, 3));
        for (int i = 1; i <= 32; ++i) free(submit_generated(pixels(i * 8), 3));
    }
    free(strongest);
    free(fast_default);
    free(fast);
    free(default_blur);
    free(blurred);
    free(shifted);
    free(frame);
    mb_renderer_destroy(&renderer);
    mb_config_free(config);
    glDeleteSamplers(1, &sampler);
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &target);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    printf("PASS: version routing, static frame, motion (%zu pixels), state, session, strength, history scale\n", changed);
}
