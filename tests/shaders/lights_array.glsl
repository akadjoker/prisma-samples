const int kArrayLights = 16;

layout(set = 0, binding = 3, std140) uniform Lights
{
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
    vec4 uLightCounts;
    LightData uLights[kArrayLights];
};

vec3 evaluateLights(PbrSurface surface, vec3 n, vec3 v, vec3 worldPosition)
{
    vec3 color = vec3(0.0);
    if (uSunColorIntensity.w > 0.0)
        color += surfaceShading(surface, directionalLight(uSunDirection, uSunColorIntensity), n, v);
    int count = int(uLightCounts.x);
    for (int i = 0; i < count; ++i)
        color += surfaceShading(surface, punctualLight(uLights[i], worldPosition), n, v);
    return color;
}
