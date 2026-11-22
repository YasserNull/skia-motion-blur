#define MB_INV (1.0 / mbExtent)

vec4 mbSample(sampler2D frame, vec2 p) {
    return textureLod(frame, clamp(p, 0.5 * MB_INV, 1.0 - 0.5 * MB_INV), 0.0);
}
vec4 mbCur(vec2 p)  { return mbSample(currentFrame, p); }
vec4 mbPrev(vec2 p) { return mbSample(previousFrame, p); }

float mbDifference(vec3 a, vec3 b) { return dot(abs(a - b), vec3(0.25, 0.5, 0.25)); }
vec2 mbGuidePoint(int i, int count) {
    return vec2(fract((float(i) + 0.5) * 0.61803398875), (float(i) + 0.5) / float(count));
}

vec4 mbGuideResult(vec2 best, float zero, float cost) {
    float improvement = max(0.0, zero - cost) / max(zero, 0.001);
    float confidence = (1.0 - smoothstep(0.04, 0.18, cost)) * smoothstep(0.10, 0.40, improvement);
    return vec4((best * 4.0 + 128.0) / 255.0, confidence, 0.0);
}
