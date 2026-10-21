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

    vec3 primary = uPrimary.rgb;
    vec3 secondary = uSecondary.rgb;
    vec3 accent = uAccent.rgb;

    vec3 col = uBase.rgb * 0.42;

    float n1 = noise(p * 2.0 + vec2(iTime * 0.03, 0.0));
    float n2 = noise(p * 3.5 - vec2(0.0, iTime * 0.025));
    col += primary * 0.115 * smoothstep(0.20, 0.90, n1);
    col += secondary * 0.105 * smoothstep(0.35, 1.00, n2);

    if (q >= 1.0) {
        vec2 starGrid = floor(px / 3.0);
        float star = hash21(starGrid);
        float twinkle = 0.5 + 0.5 * sin(iTime * 2.0 + star * 6.2831);
        if (star > 0.985) {
            float d = length(fract(px / 3.0) - 0.5);
            col += mix(primary, vec3(1.0), 0.55) * smoothstep(0.35, 0.0, d) * (0.25 + 0.75 * twinkle);
        }
    }

    float horizon = 0.58;
    if (uv.y > horizon) {
        float depth = (uv.y - horizon) / (1.0 - horizon);
        float z = 1.0 / max(depth, 0.02);
        vec2 g = vec2((uv.x - 0.5) * z * 1.6, z * 1.2 + iTime * 0.55);
        vec2 f = abs(fract(g) - 0.5);
        float line = smoothstep(0.46, 0.50, max(f.x, f.y));
        float fade = pow(1.0 - depth, 0.35);
        col += primary * 0.70 * line * fade * 0.55;
        col += accent * 0.45 * line * fade * 0.18;
    }

    if (q >= 1.0) {
        float scan = 0.5 + 0.5 * sin(px.y * 1.5 + iTime * 3.0);
        col *= 0.93 + 0.07 * scan;
    }

    float glow = smoothstep(0.55, 0.0, length(uv - iMouse));
    col += primary * 0.22 * glow * 0.35;

    col += primary * 0.45 * iPulse * 0.12;

    float vig = smoothstep(1.15, 0.25, length((uv - 0.5) * vec2(1.0, 1.15)));
    col *= vig;

    if (q >= 2.0) {
        float grain = hash21(px + fract(iTime) * 100.0) - 0.5;
        col += grain * 0.006;
    }

    fragColor = vec4(max(col, 0.0), 1.0) * qt_Opacity;
}
