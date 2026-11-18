#define MB_INV (1.0 / mbExtent)

vec4 mbCur(vec2 p)  { return textureLod(currentFrame,  clamp(p, 0.5 * MB_INV, 1.0 - 0.5 * MB_INV), 0.0); }
vec4 mbPrev(vec2 p) { return textureLod(previousFrame, clamp(p, 0.5 * MB_INV, 1.0 - 0.5 * MB_INV), 0.0); }

float mbDifference(vec3 a, vec3 b) { return dot(abs(a - b), vec3(0.25, 0.5, 0.25)); }
bool mbBlurred = false;

vec4 mbEncode(vec2 motion, float confidence) {
    // Scaling by four preserves quarter-pixel motion within the RGBA8 range.
    return vec4((motion * confidence * 4.0 + 128.0) / 255.0, confidence, 0.0);
}

vec2 mbGuidePoint(int i) {
    return vec2(fract((float(i) + 0.5) * 0.61803398875), (float(i) + 0.5) / 64.0);
}

vec2 mbGuidePts[64];
vec4 mbGuideRef[64];
int mbGuideN = 0;
float mbGuideWeight = 1.0;

float mbGuideCost(vec2 candidate) {
    float error = 0.0;
    vec2 shift = candidate * MB_INV;
    for (int i = 0; i < mbGuideN; ++i)
        error += mbDifference(mbGuideRef[i].rgb, mbPrev(mbGuidePts[i] + shift).rgb) * mbGuideRef[i].a;
    return error / mbGuideWeight;
}

vec4 mbGuide() {
    float weight = 0.0, zero = 0.0;
    mbGuideN = 0;
    for (int i = 0; i < 64; ++i) {
        vec2 p = mbGuidePoint(i);
        vec3 color = mbCur(p).rgb;
        float change = mbDifference(color, mbPrev(p).rgb);
        float vote = smoothstep(0.005, 0.05, change);
        if (vote < 0.001) continue;
        mbGuidePts[mbGuideN] = p;
        mbGuideRef[mbGuideN] = vec4(color, vote);
        ++mbGuideN;
        weight += vote;
        zero += change * vote;
    }
    if (weight < 3.0) return vec4(vec2(128.0 / 255.0), 0.0, 0.0);
    mbGuideWeight = weight;
    zero /= weight;

    vec2 best = vec2(0);
    float cost = zero, score = zero;

    for (int axis = 0; axis < 2; ++axis) {
        vec2 dir = axis == 0 ? vec2(1, 0) : vec2(0, 1);
        for (int d = -31; d <= 31; ++d) {
            if (d == 0) continue;
            vec2 candidate = dir * float(d);
            float error = mbGuideCost(candidate);
            float ranked = error + 0.00002 * abs(float(d));
            if (ranked < score) { best = candidate; cost = error; score = ranked; }
        }
    }
    // Search perpendicular to the winning axis to recover diagonal motion.
    if (any(notEqual(best, vec2(0)))) {
        vec2 center = best;
        vec2 perpendicular = best.x != 0.0 ? vec2(0, 1) : vec2(1, 0);
        for (int d = -31; d <= 31; ++d) {
            if (d == 0) continue;
            vec2 candidate = clamp(center + perpendicular * float(d), vec2(-31), vec2(31));
            float error = mbGuideCost(candidate);
            float ranked = error + 0.00002 * length(candidate);
            if (ranked < score) { best = candidate; cost = error; score = ranked; }
        }
    }
    if (any(notEqual(best, vec2(0)))) {
        vec2 dir = best.x != 0.0 ? vec2(1, 0) : vec2(0, 1);
        vec2 center = best;
        for (int k = 0; k < 2; ++k) {
            vec2 candidate = clamp(center + dir * (k == 0 ? -0.5 : 0.5), vec2(-31), vec2(31));
            float error = mbGuideCost(candidate);
            float ranked = error + 0.00002 * length(candidate);
            if (ranked < score) { best = candidate; cost = error; score = ranked; }
        }
    }

    float improvement = max(0.0, zero - cost) / max(zero, 0.001);
    float confidence = (1.0 - smoothstep(0.04, 0.18, cost)) * smoothstep(0.10, 0.40, improvement);
    return vec4((best * 4.0 + 128.0) / 255.0, confidence, 0.0);
}

const vec2 mbTaps[9] = vec2[](vec2(0), vec2(-8, 0), vec2(8, 0), vec2(0, -8), vec2(0, 8), vec2(-8, -8), vec2(8, -8), vec2(-8, 8), vec2(8, 8));

float mbCost(vec2 p, vec2 offset, vec3 reference[9]) {
    float cost = 0.0;
    for (int i = 0; i < 9; ++i)
        cost += mbDifference(mbPrev(p + (mbTaps[i] + offset) * MB_INV).rgb, reference[i]);
    return cost / 9.0;
}

vec4 mbMotion(vec2 p) {
    vec3 reference[9];
    for (int i = 0; i < 9; ++i) reference[i] = mbCur(p + mbTaps[i] * MB_INV).rgb;

    vec2 best = vec2(0);
    float zero = mbCost(p, best, reference), cost = zero, score = zero;
    if (zero < 0.0005) return vec4(vec2(128.0 / 255.0), 0.0, 0.0);

    vec4 guide = texelFetch(guideFrame, ivec2(0), 0);
    vec2 seed = (guide.xy * 255.0 - 128.0) / 4.0;
    float guidedCost = mbCost(p, seed, reference);
    bool guided = guide.z > 0.5 && guidedCost < zero * 0.95;
    if (guided) { best = seed; cost = guidedCost; score = guidedCost + 0.00002 * length(seed); }

    int levels = guided ? 1 : 5;
    for (int level = 0; level < levels; ++level) {
        if (cost < 0.0005) break;
        float stepSize = guided ? 1.0 : exp2(float(4 - level));
        vec2 center = best;
        bool zeroOrigin = !guided && any(notEqual(center, vec2(0)));
        for (int origin = 0; origin < 2; ++origin) {
            if (origin == 1 && !zeroOrigin) continue;
            vec2 from = origin == 0 ? center : vec2(0);
            for (int i = 1; i < 9; ++i) {
                vec2 candidate = clamp(from + mbTaps[i] * 0.125 * stepSize, vec2(-31), vec2(31));
                float error = mbCost(p, candidate, reference);
                float candidateScore = error + 0.00002 * length(candidate);
                if (candidateScore < score) { cost = error; score = candidateScore; best = candidate; }
            }
        }
    }

    float improvement = max(0.0, zero - cost) / max(zero, 0.001);
    float confidence = (1.0 - smoothstep(0.05, 0.20, cost)) * smoothstep(0.03, 0.25, improvement);
    return mbEncode(best, confidence);
}

vec3 mbVelocity(vec2 p) {
    vec4 flow = textureLod(flowFrame, p, 0.0);
    vec2 velocity = (flow.xy * 255.0 - 128.0) / (4.0 * max(flow.z, 1.0 / 255.0));
    return vec3(velocity * mbFlowScale, flow.z);
}

vec4 mbResolve(vec2 p) {
    vec4 original = texelFetch(currentFrame, ivec2(gl_FragCoord.xy), 0);
    vec3 flow = mbVelocity(p);
    if (mbDebug > 0.5) {
        if (abs(p.x - 0.5) < 1.0 / mbExtent.x) return vec4(vec3(1), original.a);
        if (p.x < 0.5) return vec4(clamp(vec3(0.5 + flow.xy / 62.0, flow.z), 0.0, 1.0), original.a);
    }
    if (mbStrength * flow.z < 0.01 || length(flow.xy) < 0.75) return original;

    float change = mbDifference(textureLod(currentHistoryFrame, p, 0.0).rgb,
                                textureLod(previousFrame, p, 0.0).rgb);
    // Suppress low-confidence estimates to reduce smearing on disocclusion.
    float reliability = smoothstep(0.20, 0.65, flow.z);
    float amount = mbStrength * flow.z * reliability * smoothstep(0.0005, 0.008, change);
    if (amount < 0.01) return original;

    vec2 radius = flow.xy * amount * 0.5;
    float maxRadius = 8.0 * clamp(mbStrength, 1.0, 8.0);
    radius *= min(1.0, maxRadius / max(length(radius), 0.001));
    int taps = clamp(int(ceil(length(radius) / 2.0)), 2, 32);

    // Interleaved gradient noise: breaks up tap banding at ~2px spacing.
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    vec3 sum = original.rgb;
    float wsum = 1.0;
    for (int i = 1; i <= 32; ++i) {
        if (i > taps) break;
        float t = (float(i) - jitter) / float(taps);
        float w = 1.0 - t;
        vec2 offset = radius * t * MB_INV;
        sum += (mbCur(p + offset).rgb + mbCur(p - offset).rgb) * w;
        wsum += 2.0 * w;
    }
    // Edge contrast preserves fine detail without identifying text or layers.
    vec2 pixel = MB_INV;
    vec3 dx = mbCur(p + vec2(pixel.x, 0.0)).rgb - mbCur(p - vec2(pixel.x, 0.0)).rgb;
    vec3 dy = mbCur(p + vec2(0.0, pixel.y)).rgb - mbCur(p - vec2(0.0, pixel.y)).rgb;
    float edge = max(mbDifference(dx, vec3(0.0)), mbDifference(dy, vec3(0.0)));
    float preserve = 0.75 * smoothstep(0.08, 0.35, edge);
    mbBlurred = true;
    return vec4(mix(sum / wsum, original.rgb, preserve), original.a);
}
