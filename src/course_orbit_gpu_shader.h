#ifndef KDC_COURSE_ORBIT_GPU_SHADER_H
#define KDC_COURSE_ORBIT_GPU_SHADER_H

#define KDC_GLSL(...) #__VA_ARGS__

/* Order-preserving bijection from float depth into normal floats in (0, 0.5)
 * across |d| in [2^-14, 2^16] so GL_DEPTH_COMPONENT32F with GL_GEQUAL matches
 * raw float comparison bit-for-bit for world bodies against terrain and each
 * other, while 0.0 stays strictly less for uncovered background pixels. */
static const char kOrbitDepthHelper[] = KDC_GLSL(
    float mapDepth(float d) {
        const uint kBase = 0x30000000u; /* 2^-31 */
        const uint kMax  = 0x4f000000u; /* 2^31  */
        const uint kHalf = 0x1f800000u;
        if (d > 0.0) {
            uint u = floatBitsToUint(d);
            if (u >= kBase) return uintBitsToFloat(kHalf + 0x00800000u + (min(u, kMax) - kBase));
            return uintBitsToFloat(kHalf + 0x00400001u + (u >> 8));
        }
        if (d < 0.0) {
            uint u = floatBitsToUint(-d);
            if (u >= kBase) return uintBitsToFloat(kHalf - (min(u, kMax) - kBase));
            return uintBitsToFloat(kHalf + 0x00400000u - 1u - (u >> 8));
        }
        return uintBitsToFloat(kHalf + 0x00400000u);
    }
);

static const char kOrbitFullscreenVertex[] = KDC_GLSL(
    void main() {
        vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
        gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    }
);

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
        uint i00 = texelFetch(terrain, cell, 0).r;
        uint i10 = texelFetch(terrain, cell + ivec2(1, 0), 0).r;
        uint i01 = texelFetch(terrain, cell + ivec2(0, 1), 0).r;
        uint i11 = texelFetch(terrain, cell + ivec2(1, 1), 0).r;
        if (i00 == i10 && i00 == i01 && i00 == i11)
            return i00 == 0u ? fallback : rgb(texelFetch(palette, ivec2(i00, 0), 0).r);
        precise double fx = (a - cell.x - 0.5lf) * coefficient(base, 18u) + 0.5lf;
        precise double fy = (b - cell.y - 0.5lf) * coefficient(base, 19u) + 0.5lf;
        uint wx = uint(clamp(fx, 0.0lf, 1.0lf) * 256.0lf + 0.5lf);
        uint wy = uint(clamp(fy, 0.0lf, 1.0lf) * 256.0lf + 0.5lf);
        uvec3 c00 = i00 == 0u ? fallback : rgb(texelFetch(palette, ivec2(i00, 0), 0).r);
        uvec3 c10 = i10 == 0u ? fallback : rgb(texelFetch(palette, ivec2(i10, 0), 0).r);
        uvec3 c01 = i01 == 0u ? fallback : rgb(texelFetch(palette, ivec2(i01, 0), 0).r);
        uvec3 c11 = i11 == 0u ? fallback : rgb(texelFetch(palette, ivec2(i11, 0), 0).r);
        uvec3 top = mixRgb(c00, c10, wx);
        uvec3 bottom = mixRgb(c01, c11, wx);
        return mixRgb(top, bottom, wy);
    }
    void main() {
        ivec2 p = ivec2(gl_FragCoord.xy);
        uint bin = uint((p.y >> 3) * columns + (p.x >> 4));
        uint start = readWord(binData, bin), end = readWord(binData, bin + 1u);
        if (start == end) {
            depth = uintBitsToFloat(0xff7fffffu);
            gl_FragDepth = 0.0;
            color = vec4(0.0);
            return;
        }
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
        if (!covered) {
            gl_FragDepth = 0.0;
            color = vec4(0.0);
            return;
        }
        gl_FragDepth = mapDepth(bestDepth);
        uvec3 fallback = rgb(word(hit + 5u));
        color = vec4(vec3(word(hit + 6u) == 0u ? fallback :
                              texturedColor(hit, p, hitLeft, fallback)) / 255.0, 1.0);
    }
);

static const char kOrbitBgVertex[] = KDC_GLSL(
    void main() {
        vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
        gl_Position = vec4(pos * 2.0 - 1.0, -1.0, 1.0);
    }
);

static const char kOrbitBgFragment[] = KDC_GLSL(
    uniform usampler2D sceneryTex;
    uniform uint sceneryColors[32];
    uniform uint backdrop;
    uniform int scale, extra, hScroll, vScroll, hasScenery, sceneryHd;
    out vec4 color;

    int tileCoord(int pixel) {
        return pixel >= 0 ? pixel / 8 : -((-pixel + 7) / 8);
    }
    uint reflectedColumn(int screenX, int scrollX, out bool flip) {
        int fine = scrollX & 7;
        int cols = fine != 0 ? 33 : 32;
        int phase = tileCoord(screenX + fine) % (2 * cols);
        if (phase < 0) phase += 2 * cols;
        flip = phase >= cols;
        return uint((scrollX >> 3) + (flip ? 2 * cols - 1 - phase : phase));
    }
    void main() {
        uint outPixel = backdrop;
        if (hasScenery != 0) {
            ivec2 p = ivec2(gl_FragCoord.xy);
            int row = p.y / scale, sy = p.y - row * scale;
            int nx = p.x / scale, sx = p.x - nx * scale;
            int screenX = nx - extra;
            int v = (row + vScroll + 1) & 255;
            bool flip;
            uint col = reflectedColumn(screenX, hScroll, flip) & 31u;
            int tileX = (screenX + (hScroll & 7)) & 7;
            uint idx;
            if (sceneryHd != 0 && scale > 1) {
                int tySub = scale == 4 ? sy : (2 * sy + 1) * 4 / (2 * scale);
                int txSub = scale == 4 ? sx : (2 * sx + 1) * 4 / (2 * scale);
                int tx = tileX * 4 + txSub;
                if (flip) tx = 31 - tx;
                idx = texelFetch(sceneryTex, ivec2(int(col) * 32 + tx, v * 4 + tySub), 0).r;
            } else {
                int tx = flip ? 7 - tileX : tileX;
                idx = texelFetch(sceneryTex, ivec2(int(col) * 8 + tx, v), 0).r;
            }
            if (idx != 0u) outPixel = sceneryColors[idx];
        }
        color = vec4(vec3((uvec3(outPixel) >> uvec3(16, 8, 0)) & 255u) / 255.0, 1.0);
    }
);

static const char kOrbitPieceVertex[] = KDC_GLSL(
    uniform usampler2D pieceData;
    uniform ivec2 outputSize;
    uniform uint baseInstance;
    flat out uint pieceBase;

    uint readWord(uint index) {
        return texelFetch(pieceData, ivec2(index & 1023u, index >> 10), 0).r;
    }
    void main() {
        uint base = (baseInstance + uint(gl_InstanceID)) * 64u;
        pieceBase = base;
        int x0 = int(readWord(base));
        int x1 = int(readWord(base + 1u));
        int y0 = int(readWord(base + 2u));
        int y1 = int(readWord(base + 3u));
        ivec2 corner = ivec2((gl_VertexID & 1) != 0 ? x1 : x0,
                             (gl_VertexID & 2) != 0 ? y1 : y0);
        gl_Position = vec4(vec2(corner) / vec2(outputSize) * 2.0 - 1.0, 0.0, 1.0);
    }
);

static const char kOrbitPieceCommon[] = KDC_GLSL(
    uniform usampler2D pieceData, pieceCells, spriteAtlas, terrainMask;
    uniform ivec2 terrainOrigin;
    uniform int hasTerrainMask;
    flat in uint pieceBase;
    out vec4 color;

    uint readWord(usampler2D data, uint index) {
        return texelFetch(data, ivec2(index & 1023u, index >> 10), 0).r;
    }
    uint pword(uint index) { return readWord(pieceData, index); }
    double pcoeff(uint base, uint i) {
        uint index = base + 10u + i * 2u;
        return packDouble2x32(uvec2(pword(index), pword(index + 1u)));
    }
    uint pieceColor(uint base, double sourceX, double sourceY) {
        int size = int(pword(base + 4u));
        if (!(sourceX >= 0.0lf && sourceX < double(size) &&
              sourceY >= 0.0lf && sourceY < double(size)))
            return 0u;
        int sx = int(sourceX), sy = int(sourceY);
        uint attr = pword(base + 6u);
        if (pword(base + 7u) != 0u && hasTerrainMask != 0) {
            uint prio = (attr >> 12) & 3u;
            if (prio < 3u) {
                double mx = pcoeff(base, 0u) + pcoeff(base, 2u) + double(sx);
                double my = pcoeff(base, 1u) + pcoeff(base, 3u) + double(sy);
                if (mx >= 0.0lf && my >= 0.0lf) {
                    int px = int(mx) - terrainOrigin.x;
                    int py = int(my) - terrainOrigin.y;
                    ivec2 msize = textureSize(terrainMask, 0);
                    if (px >= 0 && py >= 0 && px < msize.x && py < msize.y) {
                        uint m = texelFetch(terrainMask, ivec2(px, py), 0).r;
                        if ((m & (prio < 2u ? 1u : 2u)) != 0u) return 0u;
                    }
                }
            }
        }
        bool hflip = (attr & 0x4000u) != 0u;
        bool vflip = (attr & 0x8000u) != 0u;
        if (hflip) sx = size - 1 - sx;
        if (vflip) sy = size - 1 - sy;
        uint cellIdx = pword(base + 8u) + uint((sy >> 3) * (size >> 3) + (sx >> 3));
        uint cell = readWord(pieceCells, cellIdx);
        if (cell == 0u) return 0u;
        uint slot = (cell >> 1) - 1u;
        ivec2 tile = ivec2(slot & 63u, slot >> 6) * 32;
        if ((cell & 1u) != 0u)
            return texelFetch(spriteAtlas, tile + ivec2(sx & 7, sy & 7), 0).r;
        uint tx = uint(sourceX * 4.0lf) & 31u;
        uint ty = uint(sourceY * 4.0lf) & 31u;
        uint fx = hflip ? (31u - tx) : tx;
        uint fy = vflip ? (31u - ty) : ty;
        return texelFetch(spriteAtlas, tile + ivec2(int(fx), int(fy)), 0).r;
    }
    uint filtered(uint base, double x, double y, double step) {
        int s = int(pword(base + 5u));
        if (s == 1) return pieceColor(base, x, y);
        uint r = 0u, g = 0u, b = 0u, opaque = 0u;
        for (int j = 0; j < s; ++j) {
            for (int i = 0; i < s; ++i) {
                uint c = pieceColor(base,
                    x + (double(i) + 0.5lf - double(s) * 0.5lf) * step,
                    y + (double(j) + 0.5lf - double(s) * 0.5lf) * step);
                if (c == 0u) continue;
                r += (c >> 16) & 255u; g += (c >> 8) & 255u; b += c & 255u; ++opaque;
            }
        }
        if (opaque * 2u < uint(s * s)) return 0u;
        return 0xff000000u | ((r / opaque) << 16) | ((g / opaque) << 8) | (b / opaque);
    }
    vec4 unpackBgra(uint c) {
        return vec4(vec3((uvec3(c) >> uvec3(16, 8, 0)) & 255u), float(c >> 24)) / 255.0;
    }
);

static const char kOrbitDecalFragment[] = KDC_GLSL(
    uniform sampler2D depthTex;
    void main() {
        ivec2 p = ivec2(gl_FragCoord.xy);
        int x0 = int(pword(pieceBase));
        float d = texelFetch(depthTex, p, 0).r;
        double py = double(p.y) + 0.5lf;
        double x0c = double(x0) + 0.5lf;
        double ox = pcoeff(pieceBase, 0u) + pcoeff(pieceBase, 2u);
        double oy = pcoeff(pieceBase, 1u) + pcoeff(pieceBase, 3u);
        double sx, sy;
        if (floatBitsToUint(d) == 0xff7fffffu) {
            precise double psx = pcoeff(pieceBase, 17u) + pcoeff(pieceBase, 19u) * x0c +
                                 pcoeff(pieceBase, 21u) * py - ox;
            precise double psy = pcoeff(pieceBase, 18u) + pcoeff(pieceBase, 20u) * x0c +
                                 pcoeff(pieceBase, 22u) * py - oy;
            double dsx = pcoeff(pieceBase, 19u), dsy = pcoeff(pieceBase, 20u);
            for (int x = x0; x < p.x; ++x) { psx += dsx; psy += dsy; }
            sx = psx; sy = psy;
        } else {
            precise double iz = pcoeff(pieceBase, 7u) + pcoeff(pieceBase, 10u) * x0c +
                                pcoeff(pieceBase, 13u) * py;
            precise double ix = pcoeff(pieceBase, 5u) + pcoeff(pieceBase, 8u) * x0c +
                                pcoeff(pieceBase, 11u) * py - ox;
            precise double iy = pcoeff(pieceBase, 6u) + pcoeff(pieceBase, 9u) * x0c +
                                pcoeff(pieceBase, 12u) * py - oy;
            double dz = pcoeff(pieceBase, 10u), dx = pcoeff(pieceBase, 8u), dy = pcoeff(pieceBase, 9u);
            for (int x = x0; x < p.x; ++x) { iz += dz; ix += dx; iy += dy; }
            double z = iz + pcoeff(pieceBase, 16u) * double(d);
            if (abs(z - pcoeff(pieceBase, 4u)) > 1.5lf) discard;
            sx = ix + pcoeff(pieceBase, 14u) * double(d);
            sy = iy + pcoeff(pieceBase, 15u) * double(d);
        }
        uint c = filtered(pieceBase, sx, sy, pcoeff(pieceBase, 23u));
        if (c == 0u) discard;
        color = unpackBgra(c);
    }
);

static const char kOrbitWorldBodyFragment[] = KDC_GLSL(
    void main() {
        ivec2 p = ivec2(gl_FragCoord.xy);
        int x0 = int(pword(pieceBase));
        double invScale = pcoeff(pieceBase, 9u);
        double sySrc = (double(p.y) + 0.5lf - pcoeff(pieceBase, 8u)) * invScale;
        precise double sxSrc = (double(x0) + 0.5lf - pcoeff(pieceBase, 7u)) * invScale;
        for (int x = x0; x < p.x; ++x) sxSrc += invScale;
        uint c = filtered(pieceBase, sxSrc, sySrc, pcoeff(pieceBase, 10u));
        if (c == 0u) discard;
        double below = (double(p.y) + 0.5lf - pcoeff(pieceBase, 5u)) * invScale;
        double d = pcoeff(pieceBase, 6u) + (below >= 0.0lf ? 2.0lf * below : -0.5lf * below) +
                   0.25lf + double(pword(pieceBase + 4u)) * 0.5lf;
        gl_FragDepth = mapDepth(float(d));
        color = unpackBgra(c);
    }
);

static const char kOrbitScreenBodyFragment[] = KDC_GLSL(
    void main() {
        ivec2 p = ivec2(gl_FragCoord.xy);
        int x0 = int(pword(pieceBase));
        double invScale = pcoeff(pieceBase, 9u);
        double sySrc = (double(p.y) + 0.5lf - pcoeff(pieceBase, 8u)) * invScale;
        precise double sxSrc = (double(x0) + 0.5lf - pcoeff(pieceBase, 7u)) * invScale;
        for (int x = x0; x < p.x; ++x) sxSrc += invScale;
        uint c = filtered(pieceBase, sxSrc, sySrc, pcoeff(pieceBase, 10u));
        if ((c >> 24) == 0u) discard;
        color = unpackBgra(c);
    }
);

static const char kOrbitRectVertex[] = KDC_GLSL(
    uniform usampler2D rectData;
    uniform ivec2 outputSize;

    uint readWord(uint index) {
        return texelFetch(rectData, ivec2(index & 1023u, index >> 10), 0).r;
    }
    void main() {
        uint base = uint(gl_InstanceID) * 4u;
        int x0 = int(readWord(base));
        int x1 = int(readWord(base + 1u));
        int y0 = int(readWord(base + 2u));
        int y1 = int(readWord(base + 3u));
        ivec2 corner = ivec2((gl_VertexID & 1) != 0 ? x1 : x0,
                             (gl_VertexID & 2) != 0 ? y1 : y0);
        gl_Position = vec4(vec2(corner) / vec2(outputSize) * 2.0 - 1.0, 0.0, 1.0);
    }
);

static const char kOrbitRectFragment[] = KDC_GLSL(
    uniform uint rectColor;
    out vec4 color;
    void main() {
        color = vec4(vec3((uvec3(rectColor) >> uvec3(16, 8, 0)) & 255u) / 255.0, 1.0);
    }
);

static const char kOrbitHudVertex[] = KDC_GLSL(
    uniform ivec2 outputSize;
    uniform ivec2 yRange;
    void main() {
        ivec2 corner = ivec2((gl_VertexID & 1) != 0 ? outputSize.x : 0,
                             (gl_VertexID & 2) != 0 ? yRange.y : yRange.x);
        gl_Position = vec4(vec2(corner) / vec2(outputSize) * 2.0 - 1.0, 0.0, 1.0);
    }
);

static const char kOrbitHudFragment[] = KDC_GLSL(
    uniform usampler2D hudMap, hudMask, spriteAtlas;
    uniform int scale, rowOffset, hudMode;
    out vec4 color;

    void main() {
        ivec2 p = ivec2(gl_FragCoord.xy);
        ivec2 cell = ivec2(p.x / scale, p.y / scale - rowOffset);
        if (hudMode == 1) {
            if (texelFetch(hudMask, cell, 0).r == 0u) discard;
            uint c = texelFetch(hudMap, cell, 0).r;
            color = vec4(vec3((uvec3(c) >> uvec3(16, 8, 0)) & 255u), float(c >> 24)) / 255.0;
            return;
        }
        uint entry = texelFetch(hudMap, cell, 0).r;
        if (entry == 0u) discard;
        if (hudMode == 0 || (entry & 0x80000000u) == 0u) {
            color = vec4(vec3((uvec3(entry) >> uvec3(16, 8, 0)) & 255u) / 255.0, 1.0);
            return;
        }
        uint slot = ((entry >> 8) & 0x7fffffu) - 1u;
        bool hflip = (entry & 64u) != 0u;
        bool vflip = (entry & 128u) != 0u;
        int px = int(entry & 7u), py = int((entry >> 3) & 7u);
        int col = hflip ? (7 - px) : px;
        int r = vflip ? (7 - py) : py;
        int sx = p.x - (p.x / scale) * scale;
        int sy = p.y - (p.y / scale) * scale;
        int subX = hflip ? (scale - 1 - sx) : sx;
        int subY = vflip ? (scale - 1 - sy) : sy;
        int tx = (scale == 4) ? (col * 4 + subX) : ((col * scale + subX) * 4 / scale);
        int ty = (scale == 4) ? (r * 4 + subY) : ((r * scale + subY) * 4 / scale);
        ivec2 tile = ivec2(slot & 63u, slot >> 6) * 32;
        uint c = texelFetch(spriteAtlas, tile + ivec2(tx, ty), 0).r;
        uint a = c >> 24;
        if (a == 0u) discard;
        color = vec4(vec3((uvec3(c) >> uvec3(16, 8, 0)) & 255u), float(a)) / 255.0;
    }
);

#undef KDC_GLSL

#endif
