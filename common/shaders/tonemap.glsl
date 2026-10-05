vec3 tonemapFilmic(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

const mat3 kSrgbToXyz = mat3(0.4124560, 0.2126730, 0.0193339, 0.3575760, 0.7151520, 0.1191920,
        0.1804380, 0.0721750, 0.9503040);
const mat3 kXyzToSrgb = mat3(3.2404542, -0.9692660, 0.0556434, -1.5371385, 1.8760108, -0.2040259,
        -0.4985314, 0.0415560, 1.0572252);
const mat3 kRec2020ToXyz = mat3(0.6369530, 0.2626983, 0.0000000, 0.1446169, 0.6780088, 0.0280731,
        0.1688558, 0.0592929, 1.0608272);
const mat3 kXyzToRec2020 = mat3(1.7166634, -0.6666738, 0.0176425, -0.3556733, 1.6164557,
        -0.0427770, -0.2533681, 0.0157683, 0.9422433);
const mat3 kAp1ToXyz = mat3(0.6624541811, 0.2722287168, -0.0055746495, 0.1340042065, 0.6740817658,
        0.0040607335, 0.1561876870, 0.0536895174, 1.0103391003);
const mat3 kXyzToAp1 = mat3(1.6410233797, -0.6636628587, 0.0117218943, -0.3248032942,
        1.6153315917, -0.0082844420, -0.2364246952, 0.0167563477, 0.9883948585);
const mat3 kAp1ToAp0 = mat3(0.6954522414, 0.0447945634, -0.0055258826, 0.1406786965, 0.8596711185,
        0.0040252103, 0.1638690622, 0.0955343182, 1.0015006723);
const mat3 kAp0ToAp1 = mat3(1.4514393161, -0.0765537734, 0.0083161484, -0.2365107469,
        1.1762296998, -0.0060324498, -0.2149285693, -0.0996759264, 0.9977163014);
const mat3 kSrgbToRec2020 = kXyzToRec2020 * kSrgbToXyz;
const mat3 kRec2020ToSrgb = kXyzToSrgb * kRec2020ToXyz;
const mat3 kRec2020ToAp0 = kAp1ToAp0 * kXyzToAp1 * kRec2020ToXyz;
const mat3 kAp1ToRec2020 = kXyzToRec2020 * kAp1ToXyz;
const vec3 kLuminanceAp1 = vec3(0.272229, 0.674082, 0.0536895);

float acesSaturation(vec3 rgb)
{
    float mi = min(rgb.x, min(rgb.y, rgb.z));
    float ma = max(rgb.x, max(rgb.y, rgb.z));
    return (max(ma, 1e-5) - max(mi, 1e-5)) / max(ma, 1e-2);
}

float acesYc(vec3 rgb)
{
    float chroma = sqrt(max(rgb.z * (rgb.z - rgb.y) + rgb.y * (rgb.y - rgb.x) +
                                    rgb.x * (rgb.x - rgb.z), 0.0));
    return (rgb.z + rgb.y + rgb.x + 1.75 * chroma) / 3.0;
}

float acesSigmoid(float x)
{
    float t = max(1.0 - abs(x / 2.0), 0.0);
    return (1.0 + sign(x) * (1.0 - t * t)) / 2.0;
}

float acesGlow(float ycIn, float glowGainIn, float glowMid)
{
    if (ycIn <= 2.0 / 3.0 * glowMid) return glowGainIn;
    if (ycIn >= 2.0 * glowMid) return 0.0;
    return glowGainIn * (glowMid / ycIn - 1.0 / 2.0);
}

float acesHue(vec3 rgb)
{
    float hue = 0.0;
    if (!(rgb.x == rgb.y && rgb.y == rgb.z))
        hue = degrees(atan(sqrt(3.0) * (rgb.y - rgb.z), 2.0 * rgb.x - rgb.y - rgb.z));
    return hue < 0.0 ? hue + 360.0 : hue;
}

float acesCenterHue(float hue, float centerH)
{
    float centered = hue - centerH;
    if (centered < -180.0) return centered + 360.0;
    if (centered > 180.0) return centered - 360.0;
    return centered;
}

vec3 acesDarkToDim(vec3 linearCv)
{
    vec3 xyz = kAp1ToXyz * linearCv;
    vec2 xy = xyz.xy / max(xyz.x + xyz.y + xyz.z, 1e-5);
    float luma = pow(clamp(xyz.y, 0.0, 65504.0), 0.9811);
    float a = luma / max(xy.y, 1e-5);
    return kXyzToAp1 * vec3(xy.x * a, luma, (1.0 - xy.x - xy.y) * a);
}

vec3 acesToneMap(vec3 color, float brightness)
{
    vec3 ap0 = kRec2020ToAp0 * color;

    float saturation = acesSaturation(ap0);
    float ycIn = acesYc(ap0);
    float s = acesSigmoid((saturation - 0.4) / 0.2);
    ap0 *= 1.0 + acesGlow(ycIn, 0.05 * s, 0.08);

    float centeredHue = acesCenterHue(acesHue(ap0), 0.0);
    float hueWeight = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centeredHue / 135.0));
    hueWeight *= hueWeight;
    ap0.r += hueWeight * saturation * (0.03 - ap0.r) * (1.0 - 0.82);

    vec3 ap1 = clamp(kAp0ToAp1 * ap0, 0.0, 65504.0);
    ap1 = mix(vec3(dot(ap1, kLuminanceAp1)), ap1, 0.96);
    ap1 *= brightness;

    vec3 rgbPost = (ap1 * (2.785085 * ap1 + 0.107772)) / (ap1 * (2.936045 * ap1 + 0.887122) + 0.806889);
    vec3 linearCv = acesDarkToDim(rgbPost);
    linearCv = mix(vec3(dot(linearCv, kLuminanceAp1)), linearCv, 0.93);
    return kAp1ToRec2020 * linearCv;
}

vec3 tonemapAcesLegacy(vec3 color)
{
    return clamp(kRec2020ToSrgb * acesToneMap(kSrgbToRec2020 * color, 1.0 / 0.6), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 x)
{
    return mix(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, x * 12.92, lessThanEqual(x, vec3(0.0031308)));
}
