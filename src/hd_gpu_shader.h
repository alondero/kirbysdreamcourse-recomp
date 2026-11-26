#ifndef KDC_HD_GPU_SHADER_H
#define KDC_HD_GPU_SHADER_H

/* Uint palette, atlas and records avoid hardware filtering, normalized
 * intermediate colours, and rounding differences in SNES colour math. */
#define KDC_GLSL(...) #__VA_ARGS__
static const char kHdGpuVertex[] = "#version 330 core\n" KDC_GLSL(
    void main() {
        vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
        gl_Position = vec4(pos * 2.0 - 1.0, 0, 1);
    }
);

static const char kHdGpuFragment[] = "#version 330 core\n" KDC_GLSL(
    uniform usampler2D nativeImage, offsets, records, palettes, atlas;
    uniform int scale;
    out vec4 outputColor;

    uint word(uint i) {
        return texelFetch(records, ivec2(i & 1023u, i >> 10), 0).r;
    }
    uint palette(int y, uint index) {
        return texelFetch(palettes, ivec2(index, y), 0).r;
    }
    uint layer(uint z) { return (z >> 8) & 15u; }
    uint color15(int y, uint z, uint objectColor) {
        if ((objectColor & 32768u) != 0u && (layer(z) == 4u || layer(z) == 6u))
            return objectColor & 32767u;
        return palette(y, z & 255u);
    }
    uvec3 channels(uint rgb) { return (uvec3(rgb) >> uvec3(16, 8, 0)) & 255u; }
    uvec3 unpack15(uint bgr) { return (uvec3(bgr) >> uvec3(0, 5, 10)) & 31u; }
    uvec3 expand(uvec3 c) { return (c << 3) | (c >> 2); }

    uint sampleTile(uint info, ivec2 sub, bool rgbaPath) {
        uint slot = (info >> 9) - 1u;
        ivec2 tile = ivec2(slot & 31u, slot >> 5) * 32;
        ivec2 cell = ivec2(info & 7u, (info >> 3) & 7u);
        if ((info & 64u) != 0u) sub.x = scale - 1 - sub.x;
        if ((info & 128u) != 0u) sub.y = scale - 1 - sub.y;
        ivec2 at = cell * scale + sub;
        /* The CPU indexed resolver can reach a same-mask RGBA candidate
         * when another indexed source changes the pixel. It reads image
         * bytes in this case. Preserve that interaction in the differential. */
        if (!rgbaPath && (info & 256u) != 0u) {
            uint side = uint(scale * 8);
            uint offset = uint(at.y) * side + uint(at.x);
            uint raw = offset / 4u;
            return (texelFetch(atlas, tile + ivec2(raw % side, raw / side), 0).r
                    >> ((offset & 3u) * 8u)) & 255u;
        }
        return texelFetch(atlas, tile + at, 0).r;
    }
    uint resolveScreen(uint start, uint count, ivec2 sub) {
        uint best = 1280u;
        for (uint k = 0u; k < count; ++k) {
            uint data = word(start + 2u*k), info = word(start + 2u*k + 1u);
            uint index = info == 0u ? data >> 16 : sampleTile(info, sub, false);
            if (index != 0u) best = max(best, (data & 65535u) + index);
        }
        return best;
    }

    bool mathEnabled(uint reg, uint flags, uint id) {
        return (flags & 262144u) != 0u && (reg & (1u << id)) != 0u;
    }
    uvec3 mathColor(uvec3 color, uvec3 other, uint id, uint reg, uint flags,
                    bool hasSubPixel, int limit) {
        if ((flags & 131072u) != 0u) color = uvec3(0);
        if (!mathEnabled(reg, flags, id)) return color;
        ivec3 sum = ivec3(color);
        if ((reg & 16384u) != 0u) sum -= ivec3(other);
        else sum += ivec3(other);
        if ((reg & 8192u) != 0u && ((reg & 4096u) == 0u || hasSubPixel)) sum /= 2;
        return uvec3(clamp(sum, ivec3(0), ivec3(limit)));
    }
    uvec3 paintScreen(uint start, uint count, ivec2 sub, int y, uint flags, uint reg,
                      bool mainScreen, uvec3 result, uvec3 other, inout bool hasSubPixel) {
        uint priority[5], inks[5], used = 0u;
        for (uint k = 0u; k < count; ++k) {
            uint data = word(start + 2u*k), info = word(start + 2u*k + 1u);
            uint z = data & 65535u;
            uint sampleValue = info == 0u ? data >> 16 : sampleTile(info, sub, true);
            uint ink;
            if ((info & 256u) != 0u) {
                ink = sampleValue;
                if ((ink >> 24) == 0u) continue;
                ++z;
            } else {
                if (sampleValue == 0u) continue;
                z += sampleValue;
                uvec3 rgb = expand(unpack15(color15(y, z, flags)));
                ink = 0xff000000u | (rgb.r << 16) | (rgb.g << 8) | rgb.b;
            }
            uint j = used++;
            while (j != 0u && priority[j - 1u] > z) {
                priority[j] = priority[j - 1u];
                inks[j] = inks[j - 1u];
                --j;
            }
            priority[j] = z;
            inks[j] = ink;
        }
        if (mainScreen) result = mathColor(result, other, 5u, reg, flags, hasSubPixel, 255);
        else hasSubPixel = used != 0u;
        for (uint k = 0u; k < used; ++k) {
            uint alpha = inks[k] >> 24;
            uvec3 rgb = channels(inks[k]);
            if (mainScreen)
                rgb = mathColor(rgb, other, layer(priority[k]), reg, flags, hasSubPixel, 255);
            uvec3 blended = rgb * alpha + result * (255u - alpha) + 128u;
            result = (blended + (blended >> 8)) >> 8;
        }
        return result;
    }

    void emitColor(uvec3 rgb, uint alpha) {
        outputColor = vec4(vec3(rgb), float(alpha)) / 255.0;
    }
    void main() {
        ivec2 pixel = ivec2(gl_FragCoord.xy);
        ivec2 pos = pixel / scale, sub = pixel - pos * scale;
        uint nativePixel = texelFetch(nativeImage, pos, 0).r;
        uint start = texelFetch(offsets, pos, 0).r;
        if (start == 0u) {
            emitColor(channels(nativePixel), nativePixel >> 24);
            return;
        }
        uint pair = word(start), flags = word(start + 1u), first = start + 2u;
        uint numMain = (flags >> 19) & 15u, numSub = (flags >> 23) & 15u;
        uint second = first + 2u*numMain;
        uint reg = palette(pos.y, 257u);
        uint brightness = (reg >> 8) & 15u;
        uvec3 fixedColor = unpack15(palette(pos.y, 256u));
        uvec3 rgb;
        if ((flags & 65536u) != 0u) {
            bool hasSubPixel = false;
            uvec3 fixedRgb = expand(fixedColor);
            uvec3 other = paintScreen(second, numSub, sub, pos.y, flags, reg, false,
                                      fixedRgb, uvec3(0), hasSubPixel);
            uvec3 backdrop = expand(unpack15(palette(pos.y, 0u)));
            rgb = paintScreen(first, numMain, sub, pos.y, flags, reg, true,
                              backdrop, other, hasSubPixel);
        } else {
            uint m = resolveScreen(first, numMain, sub), b = resolveScreen(second, numSub, sub);
            if ((m | (b << 16)) == pair) {
                emitColor(channels(nativePixel), nativePixel >> 24);
                return;
            }
            bool hasSubPixel = (reg & 4096u) != 0u && (b & 255u) != 0u;
            uvec3 other = hasSubPixel ? unpack15(color15(pos.y, b, flags)) : fixedColor;
            rgb = expand(mathColor(unpack15(color15(pos.y, m, flags)), other, layer(m),
                                   reg, flags, hasSubPixel, 31));
        }
        emitColor(rgb * brightness / 15u, 0u);
    }
);
#undef KDC_GLSL

#endif
