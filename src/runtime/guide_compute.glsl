layout(local_size_x = 64) in;
layout(binding = 0, rgba8) writeonly uniform highp image2D guideOutput;

// At one-third resolution fine repeating strokes can alias into a false scroll direction.
#ifdef MB_FILTER_GUIDE
const int guideSamples = 64;
vec3 guideColor(sampler2D frame, vec2 p) {
    vec2 d = 0.75 * MB_INV;
    return 0.25 * (textureLod(frame, p + d, 0.0).rgb +
                   textureLod(frame, p - d, 0.0).rgb +
                   textureLod(frame, p + vec2(d.x, -d.y), 0.0).rgb +
                   textureLod(frame, p + vec2(-d.x, d.y), 0.0).rgb);
}
#else
const int guideSamples = 16;
vec3 guideColor(sampler2D frame, vec2 p) { return mbSample(frame, p).rgb; }
#endif

shared vec2 points[guideSamples];
shared vec4 reference[guideSamples];
shared float changes[guideSamples];
shared float totalWeight, zeroCost;
shared vec2 costs[63];
shared vec2 axisWinner;
shared float bestCost, bestScore;

float candidateCost(vec2 candidate) {
    float error = 0.0;
    vec2 shift = candidate * MB_INV;
    for (int i = 0; i < guideSamples; ++i) {
        if (reference[i].a > 0.0)
            error += mbDifference(reference[i].rgb, guideColor(previousFrame, points[i] + shift)) * reference[i].a;
    }
    return error / totalWeight;
}

void consider(vec2 candidate, float error, inout vec2 best, inout float cost, inout float score) {
    float ranked = error + 0.00002 * length(candidate);
    if (ranked < score) { best = candidate; cost = error; score = ranked; }
}

void main() {
    int lane = int(gl_LocalInvocationID.x);
    if (lane < guideSamples) {
        vec2 p = mbGuidePoint(lane, guideSamples);
        vec3 c = guideColor(currentFrame, p);
        float change = mbDifference(c, guideColor(previousFrame, p));
        float vote = smoothstep(0.005, 0.05, change);
        points[lane] = p;
        reference[lane] = vec4(c, vote < 0.001 ? 0.0 : vote);
        changes[lane] = change * reference[lane].a;
    }
    barrier();
    if (lane == 0) {
        float weight = 0.0, error = 0.0;
        for (int i = 0; i < guideSamples; ++i) {
            weight += reference[i].a;
            error += changes[i];
        }
        totalWeight = weight;
        zeroCost = error / max(weight, 0.000001);
    }
    barrier();
    // Each lane prices one displacement. Lane zero preserves the reference tie order.
    bool activeLane = lane < 63 && lane != 31 && totalWeight >= 0.75;
    float d = float(lane - 31);
    if (activeLane) costs[lane] = vec2(candidateCost(vec2(d, 0)), candidateCost(vec2(0, d)));
    barrier();
    if (lane == 0) {
        vec2 best = vec2(0);
        float cost = zeroCost, score = zeroCost;
        if (totalWeight >= 0.75) {
            for (int axis = 0; axis < 2; ++axis) {
                vec2 dir = axis == 0 ? vec2(1, 0) : vec2(0, 1);
                for (int i = 0; i < 63; ++i) {
                    if (i != 31) consider(dir * float(i - 31), costs[i][axis], best, cost, score);
                }
            }
        }
        axisWinner = best;
        bestCost = cost;
        bestScore = score;
    }
    barrier();
    vec2 perpendicular = axisWinner.x != 0.0 ? vec2(0, 1) : vec2(1, 0);
    bool refine = any(notEqual(axisWinner, vec2(0)));
    if (activeLane && refine) costs[lane].x = candidateCost(axisWinner + perpendicular * d);
    barrier();
    if (lane == 0) {
        vec2 best = axisWinner;
        float cost = bestCost, score = bestScore;
        if (refine) {
            for (int i = 0; i < 63; ++i) {
                if (i != 31) consider(axisWinner + perpendicular * float(i - 31), costs[i].x, best, cost, score);
            }
            vec2 center = best;
            vec2 dir = best.x != 0.0 ? vec2(1, 0) : vec2(0, 1);
            for (int k = 0; k < 2; ++k) {
                vec2 c = clamp(center + dir * (k == 0 ? -0.5 : 0.5), vec2(-31), vec2(31));
                consider(c, candidateCost(c), best, cost, score);
            }
        }
        imageStore(guideOutput, ivec2(0), mbGuideResult(best, zeroCost, cost));
    }
}
