#ifndef KDC_COURSE_ORBIT_GPU_SHADER_H
#define KDC_COURSE_ORBIT_GPU_SHADER_H

#define KDC_GLSL(...) #__VA_ARGS__
static const char kOrbitTerrainFragment[] = KDC_GLSL(
    uniform usampler2D faceData, binData, terrain, palette;
    uniform int columns, samples;
    uniform bool smoothTexture;
    layout(location = 0) out vec4 color;
    layout(location = 1) out float depth;

    uint readWord(usampler2D data, uint index) {
        return texelFetch(data, ivec2(index & 1023u, index >> 10), 0).r;
    }
    uint word(uint index) { return readWord(faceData, index); }
    double coefficient(uint base, uint i) {
        uint index = base + 8u + i * 2u;
        return packDouble2x32(uvec2(word(index), word(index + 1u)));
    }
    bool span(uint base, ivec2 p, out int left) {
        int x0 = int(word(base)), x1 = int(word(base + 1u));
        if (p.x < x0 || p.x > x1 || p.y < int(word(base + 2u)) ||
            p.y > int(word(base + 3u))) return false;
        uint signs = word(base + 4u);
        double low = double(x0), high = double(x1);
        for (uint k = 0u; k < 3u; ++k) {
            precise double term = coefficient(base, k) * p.y + coefficient(base, k + 3u) + 0.000001lf;
            uint sign = (signs >> (2u * k)) & 3u;
            if (sign == 0u) {
                if (term < 0.0lf) return false;
            } else {
                double edge = term * coefficient(base, k + 6u);
                if (sign == 1u) low = max(low, edge);
                else high = min(high, edge);
            }
        }
        if (!(low <= high + 1.0lf)) return false;
        left = max(x0, int(ceil(low - 1e-9lf)));
        return p.x >= left && p.x <= min(x1, int(floor(high + 1e-9lf)));
    }
    /* Match the CPU's row origin and order of additions at shared edges. */
    double along(uint base, uint i, ivec2 p, int left) {
        double step = coefficient(base, i);
        precise double value = step * left + coefficient(base, i + 1u) * p.y +
                               coefficient(base, i + 2u);
        for (int x = left; x < p.x; ++x) value += step;
        return value;
    }
    int texelFloor(double value) { return int(value + 1048576.0lf) - 1048576; }
    uvec3 rgb(uint pixel) { return (uvec3(pixel) >> uvec3(16, 8, 0)) & 255u; }
    uvec3 lookup(ivec2 pos, uvec3 fallback) {
        if (any(lessThan(pos, ivec2(0))) || any(greaterThanEqual(pos, textureSize(terrain, 0))))
            return fallback;
        uint index = texelFetch(terrain, pos, 0).r;
        return index == 0u ? fallback : rgb(texelFetch(palette, ivec2(index, 0), 0).r);
    }
    uvec3 nearest(double u, double v, uvec3 fallback) {
        return lookup(ivec2(texelFloor(u), texelFloor(v)), fallback);
    }
    uvec3 mixRgb(uvec3 a, uvec3 b, uint w) { return (a * (256u - w) + b * w) >> 8; }

    uvec3 texturedColor(uint base, ivec2 p, int left, uvec3 fallback) {
        double u = along(base, 12u, p, left), v = along(base, 15u, p, left);
        if (samples > 1) {
            uvec3 total = uvec3(0);
            for (int j = 0; j < samples; ++j) {
                for (int k = 0; k < samples; ++k) {
                    double dx = (k + 0.5lf) / samples - 0.5lf;
                    double dy = (j + 0.5lf) / samples - 0.5lf;
                    precise double a = u + dx * coefficient(base, 12u) + dy * coefficient(base, 13u);
                    precise double b = v + dx * coefficient(base, 15u) + dy * coefficient(base, 16u);
                    total += nearest(a, b, fallback);
                }
            }
            return total / uint(samples * samples);
        }
        if (!smoothTexture) return nearest(u, v, fallback);
        double a = u - 0.5lf, b = v - 0.5lf;
        ivec2 cell = ivec2(texelFloor(a), texelFloor(b));
        if (any(lessThan(cell, ivec2(0))) ||
            any(greaterThanEqual(cell + 1, textureSize(terrain, 0))))
            return nearest(u, v, fallback);
        precise double fx = (a - cell.x - 0.5lf) * coefficient(base, 18u) + 0.5lf;
        precise double fy = (b - cell.y - 0.5lf) * coefficient(base, 19u) + 0.5lf;
        uint wx = uint(clamp(fx, 0.0lf, 1.0lf) * 256.0lf + 0.5lf);
        uint wy = uint(clamp(fy, 0.0lf, 1.0lf) * 256.0lf + 0.5lf);
        uvec3 top = mixRgb(lookup(cell, fallback), lookup(cell + ivec2(1, 0), fallback), wx);
        uvec3 bottom = mixRgb(lookup(cell + ivec2(0, 1), fallback),
                             lookup(cell + ivec2(1, 1), fallback), wx);
        return mixRgb(top, bottom, wy);
    }
    void main() {
        ivec2 p = ivec2(gl_FragCoord.xy);
        uint bin = uint((p.y / 16) * columns + p.x / 32);
        uint start = readWord(binData, bin), end = readWord(binData, bin + 1u);
        float bestDepth = uintBitsToFloat(0xff7fffffu);
        uint hit = 0u;
        int hitLeft = 0;
        bool covered = false;
        for (uint i = start; i < end; ++i) {
            uint base = readWord(binData, i);
            int left;
            if (!span(base, p, left)) continue;
            double candidate = along(base, 9u, p, left);
            if (candidate < double(bestDepth)) continue;
            bestDepth = float(candidate);
            hit = base;
            hitLeft = left;
            covered = true;
        }
        depth = bestDepth;
        color = vec4(0);
        if (covered) {
            uvec3 fallback = rgb(word(hit + 5u));
            color = vec4(vec3(word(hit + 6u) == 0u ? fallback :
                                  texturedColor(hit, p, hitLeft, fallback)) / 255.0, 1.0);
        }
    }
);
#undef KDC_GLSL

#endif
