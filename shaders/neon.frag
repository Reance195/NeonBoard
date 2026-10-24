#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    vec2 iResolution;
    vec2 iMouse;
    float iPulse;
    vec4 uBase;
    vec4 uPrimary;
    vec4 uSecondary;
    vec4 uAccent;
    float uQuality;
    float iState;   // 0..1 进行中负载：越满网格流速越快
    float iFocus;   // 0..1 专注强度：压暗背景、只留一束追光
    float iBurst;   // 0..1 完成脉冲：一次性能量环由中心扩散
};

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 px = uv * iResolution;
    vec2 p = (uv - 0.5) * vec2(iResolution.x / max(iResolution.y, 1.0), 1.0);

    // 画质档位：0 低 / 1 中 / 2 高（由主题引擎下发）
    float q = clamp(uQuality, 0.0, 2.0);

    // 场景通道：负载 / 专注 / 完成脉冲
    float state = clamp(iState, 0.0, 1.0);
    float focus = clamp(iFocus, 0.0, 1.0);
    float burst = clamp(iBurst, 0.0, 1.0);

    vec3 primary = uPrimary.rgb;
    vec3 secondary = uSecondary.rgb;
    vec3 accent = uAccent.rgb;

    vec3 col = uBase.rgb * 0.42;

    // 进行中越满，背景噪声流得越快
    float flow = 0.03 + state * 0.055;
    float n1 = noise(p * 2.0 + vec2(iTime * flow, 0.0));
    float n2 = noise(p * 3.5 - vec2(0.0, iTime * (0.025 + state * 0.04)));
    col += primary * 0.115 * smoothstep(0.20, 0.90, n1);
    col += secondary * 0.105 * smoothstep(0.35, 1.00, n2);

    if (q >= 1.0) {
        vec2 starGrid = floor(px / 3.0);
        float star = hash21(starGrid);
        float twinkle = 0.5 + 0.5 * sin(iTime * 2.0 + star * 6.2831);
        if (star > 0.985) {
            float d = length(fract(px / 3.0) - 0.5);
            col += mix(primary, vec3(1.0), 0.55) * smoothstep(0.35, 0.0, d)
                 * (0.25 + 0.75 * twinkle) * (1.0 - 0.85 * focus);
        }
    }

    // 网格：流速与亮度随进行中负载提升
    float horizon = 0.58;
    if (uv.y > horizon) {
        float depth = (uv.y - horizon) / (1.0 - horizon);
        float z = 1.0 / max(depth, 0.02);
        float speed = 0.55 + state * 1.15;
        vec2 g = vec2((uv.x - 0.5) * z * 1.6, z * 1.2 + iTime * speed);
        vec2 f = abs(fract(g) - 0.5);
        float line = smoothstep(0.46, 0.50, max(f.x, f.y));
        float fade = pow(1.0 - depth, 0.35);
        float gain = (0.55 + 0.45 * state) * (1.0 - 0.7 * focus);
        col += primary * 0.70 * line * fade * gain;
        col += accent * 0.45 * line * fade * 0.18 * gain;
    }

    if (q >= 1.0) {
        float scan = 0.5 + 0.5 * sin(px.y * 1.5 + iTime * 3.0);
        col *= 0.93 + 0.07 * scan * (1.0 - 0.9 * focus);
    }

    // 专注：整体压暗，只留一束跟随鼠标的追光
    if (focus > 0.001) {
        col *= mix(1.0, 0.34, focus);
        float beamX = mix(iMouse.x, 0.5, focus);
        float beam = exp(-pow((uv.x - beamX) * mix(5.0, 11.0, focus), 2.0));
        beam *= smoothstep(0.0, 0.25, uv.y) * smoothstep(1.0, 0.55, uv.y);
        col += mix(primary, vec3(1.0), 0.42) * beam * focus * 0.30;
        col += primary * 0.10 * focus;
    }

    float glow = smoothstep(0.55, 0.0, length(uv - iMouse));
    col += primary * 0.22 * glow * 0.35 * (1.0 - 0.55 * focus);

    // 完成脉冲：能量环由中心扩散，核心闪一下
    if (burst > 0.001) {
        float aspect = iResolution.x / max(iResolution.y, 1.0);
        float d = length((uv - 0.5) * vec2(aspect, 1.0));
        float radius = (1.0 - burst) * 1.05;
        float ring = smoothstep(0.10, 0.0, abs(d - radius)) * burst;
        float core = smoothstep(0.55, 0.0, d) * burst * burst;
        col += mix(primary, accent, 0.35) * ring * 1.20;
        col += mix(primary, vec3(1.0), 0.55) * core * 0.32;
    }

    col += primary * 0.45 * iPulse * 0.12 * (1.0 - 0.8 * focus);

    float vig = smoothstep(1.15, 0.25, length((uv - 0.5) * vec2(1.0, 1.15)));
    col *= vig;

    if (q >= 2.0) {
        float grain = hash21(px + fract(iTime) * 100.0) - 0.5;
        col += grain * 0.006;
    }

    fragColor = vec4(max(col, 0.0), 1.0) * qt_Opacity;
}
