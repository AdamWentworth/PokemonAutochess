#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/opengl/OpenGLRenderBackendShaderUtils.h"

#include <algorithm>
#include <cstddef>

#include <glad/glad.h>

void OpenGLRenderBackend::ensureWorldPipeline() {
    if (worldProgram_ != 0 && worldVao_ != 0 && worldVbo_ != 0 && worldIbo_ != 0 &&
        worldViewProjLoc_ >= 0 && worldModelLoc_ >= 0 &&
        worldUseTextureLoc_ >= 0 && worldTextureSamplerLoc_ >= 0 &&
        worldWrapSLoc_ >= 0 && worldWrapTLoc_ >= 0 && worldAlphaModeLoc_ >= 0 && worldAlphaCutoffLoc_ >= 0 &&
        worldCameraPosLoc_ >= 0 && worldCameraForwardLoc_ >= 0 &&
        worldMaterialModeLoc_ >= 0 && worldMaterialTimeLoc_ >= 0 && worldMaterialFlagsLoc_ >= 0 &&
        worldMaterialAtlasSizeLoc_ >= 0 && worldMaterialRect0Loc_ >= 0 && worldMaterialRect1Loc_ >= 0 &&
        worldMaterialFlipbook0Loc_ >= 0 && worldMaterialFlipbook1Loc_ >= 0 &&
        worldSkinningEnabledLoc_ >= 0 && worldSkinMatrixCountLoc_ >= 0 && worldSkinMatricesLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) return;

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in vec4 aColor;
        layout (location = 3) in vec3 aNormal;
        layout (location = 4) in vec4 aJoints;
        layout (location = 5) in vec4 aWeights;
        uniform mat4 uViewProj;
        uniform mat4 uModel;
        uniform float uSkinningEnabled;
        uniform int uSkinMatrixCount;
        const int kMaxSkinMatrices = 64;
        uniform mat4 uSkinMatrices[kMaxSkinMatrices];
        out vec2 vUv;
        out vec4 vColor;
        out vec3 vWorldPos;
        out vec3 vWorldNormal;
        vec3 applySkinningPos(vec3 localPos) {
            vec4 blended = vec4(0.0);
            float totalWeight = 0.0;

            int j0 = int(aJoints.x + 0.5);
            int j1 = int(aJoints.y + 0.5);
            int j2 = int(aJoints.z + 0.5);
            int j3 = int(aJoints.w + 0.5);
            float w0 = aWeights.x;
            float w1 = aWeights.y;
            float w2 = aWeights.z;
            float w3 = aWeights.w;

            if (w0 > 0.00001 && j0 >= 0 && j0 < uSkinMatrixCount && j0 < kMaxSkinMatrices) {
                blended += (uSkinMatrices[j0] * vec4(localPos, 1.0)) * w0;
                totalWeight += w0;
            }
            if (w1 > 0.00001 && j1 >= 0 && j1 < uSkinMatrixCount && j1 < kMaxSkinMatrices) {
                blended += (uSkinMatrices[j1] * vec4(localPos, 1.0)) * w1;
                totalWeight += w1;
            }
            if (w2 > 0.00001 && j2 >= 0 && j2 < uSkinMatrixCount && j2 < kMaxSkinMatrices) {
                blended += (uSkinMatrices[j2] * vec4(localPos, 1.0)) * w2;
                totalWeight += w2;
            }
            if (w3 > 0.00001 && j3 >= 0 && j3 < uSkinMatrixCount && j3 < kMaxSkinMatrices) {
                blended += (uSkinMatrices[j3] * vec4(localPos, 1.0)) * w3;
                totalWeight += w3;
            }

            if (totalWeight <= 0.00001) return localPos;
            if (totalWeight < 0.999) {
                blended += vec4(localPos, 1.0) * (1.0 - totalWeight);
            }
            return blended.xyz;
        }
        vec3 applySkinningNormal(vec3 localNormal) {
            vec3 blended = vec3(0.0);
            float totalWeight = 0.0;

            int j0 = int(aJoints.x + 0.5);
            int j1 = int(aJoints.y + 0.5);
            int j2 = int(aJoints.z + 0.5);
            int j3 = int(aJoints.w + 0.5);
            float w0 = aWeights.x;
            float w1 = aWeights.y;
            float w2 = aWeights.z;
            float w3 = aWeights.w;

            if (w0 > 0.00001 && j0 >= 0 && j0 < uSkinMatrixCount && j0 < kMaxSkinMatrices) {
                blended += (mat3(uSkinMatrices[j0]) * localNormal) * w0;
                totalWeight += w0;
            }
            if (w1 > 0.00001 && j1 >= 0 && j1 < uSkinMatrixCount && j1 < kMaxSkinMatrices) {
                blended += (mat3(uSkinMatrices[j1]) * localNormal) * w1;
                totalWeight += w1;
            }
            if (w2 > 0.00001 && j2 >= 0 && j2 < uSkinMatrixCount && j2 < kMaxSkinMatrices) {
                blended += (mat3(uSkinMatrices[j2]) * localNormal) * w2;
                totalWeight += w2;
            }
            if (w3 > 0.00001 && j3 >= 0 && j3 < uSkinMatrixCount && j3 < kMaxSkinMatrices) {
                blended += (mat3(uSkinMatrices[j3]) * localNormal) * w3;
                totalWeight += w3;
            }

            if (totalWeight <= 0.00001) return localNormal;
            if (totalWeight < 0.999) {
                blended += localNormal * (1.0 - totalWeight);
            }
            return normalize(blended);
        }
        void main() {
            vec3 localPos = aPos;
            vec3 localNormal = aNormal;
            if (uSkinningEnabled > 0.5) {
                localPos = applySkinningPos(localPos);
                localNormal = applySkinningNormal(localNormal);
            }
            vec4 worldPos = uModel * vec4(localPos, 1.0);
            gl_Position = uViewProj * worldPos;
            vUv = aUv;
            vColor = aColor;
            vWorldPos = worldPos.xyz;
            mat3 normalM = mat3(transpose(inverse(uModel)));
            vWorldNormal = normalize(normalM * localNormal);
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec2 vUv;
        in vec4 vColor;
        in vec3 vWorldPos;
        in vec3 vWorldNormal;
        uniform float uUseTexture;
        uniform float uWrapS;
        uniform float uWrapT;
        uniform float uAlphaMode;
        uniform float uAlphaCutoff;
        uniform vec3 uCameraPos;
        uniform vec3 uCameraForward;
        uniform vec3 uCameraTarget;
        uniform float uMaterialMode;
        uniform float uMaterialTimeSec;
        uniform float uMaterialFlags;
        uniform vec2  uMaterialAtlasSize;
        uniform vec4  uMaterialRect0;
        uniform vec4  uMaterialRect1;
        uniform vec4  uMaterialFlipbook0;
        uniform vec4  uMaterialFlipbook1;
        uniform sampler2D uTexture;
        uniform sampler2D uNormalTexture;
        uniform sampler2D uMetallicRoughnessTexture;
        uniform sampler2D uOcclusionTexture;
        uniform sampler2D uEmissiveTexture;
        uniform float uUseNormalTexture;
        uniform float uUseMetallicRoughnessTexture;
        uniform float uUseOcclusionTexture;
        uniform float uUseEmissiveTexture;
        uniform float uNormalScale;
        uniform float uMetallicFactor;
        uniform float uRoughnessFactor;
        uniform float uOcclusionStrength;
        uniform vec3 uEmissiveFactor;
        out vec4 FragColor;

        float applyWrap(float coord, float mode) {
            if (abs(mode - 33071.0) < 0.5) return clamp(coord, 0.0, 1.0);
            if (abs(mode - 33648.0) < 0.5) {
                float i = floor(coord);
                float f = fract(coord);
                float odd = mod(abs(i), 2.0);
                return (odd >= 1.0) ? (1.0 - f) : f;
            }
            return fract(coord);
        }
        vec2 clampWrappedUvToTexelCenter(vec2 uv) {
            vec2 texSize = max(vec2(textureSize(uTexture, 0)), vec2(1.0));
            vec2 halfTexel = vec2(0.5) / texSize;
            return clamp(uv, halfTexel, vec2(1.0) - halfTexel);
        }

        float hash11(float x) { return fract(sin(x * 12.9898) * 43758.5453); }
        float hash21(vec2 p) {
            float n = dot(p, vec2(127.1, 311.7));
            return fract(sin(n) * 43758.5453);
        }
        float valueNoise2D(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            vec2 u = f * f * (3.0 - 2.0 * f);
            float a = hash21(i);
            float b = hash21(i + vec2(1.0, 0.0));
            float c = hash21(i + vec2(0.0, 1.0));
            float d = hash21(i + vec2(1.0, 1.0));
            return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
        }
        float smoothFlicker(float t, float seed) {
            float x = t * 9.0 + seed * 97.0;
            float i = floor(x);
            float f = fract(x);
            f = f * f * (3.0 - 2.0 * f);
            return mix(hash11(i), hash11(i + 1.0), f);
        }
        float fbm2D(vec2 p) {
            float v = 0.0;
            float a = 0.5;
            for (int k = 0; k < 5; ++k) {
                v += a * valueNoise2D(p);
                p *= 2.02;
                a *= 0.5;
            }
            return v;
        }
        vec2 fbmGrad(vec2 p) {
            float e = 0.03;
            float nx = fbm2D(p + vec2(e, 0.0)) - fbm2D(p - vec2(e, 0.0));
            float ny = fbm2D(p + vec2(0.0, e)) - fbm2D(p - vec2(0.0, e));
            return vec2(nx, ny) / (2.0 * e);
        }
        vec2 curl2D(vec2 p) {
            vec2 g = fbmGrad(p);
            return vec2(g.y, -g.x);
        }
        vec2 advect(vec2 p, float flowY, float amount) {
            vec2 c1 = curl2D(p * 1.30 + vec2(0.0, -flowY * 0.10));
            vec2 c2 = curl2D(p * 2.70 + vec2(3.1, -flowY * 0.18));
            return p + (c1 * 0.65 + c2 * 0.35) * amount;
        }
        vec3 tonemapSoftLocal(vec3 c) {
            return c / (vec3(1.0) + c);
        }
        vec2 clampUvToRegionPixels(vec2 localUV01, vec4 rectUv) {
            vec2 atlasSize = max(uMaterialAtlasSize, vec2(1.0));
            vec2 rectPx = max(rectUv.zw * atlasSize, vec2(1.0));
            vec2 minPx = vec2(0.5) / atlasSize;
            vec2 maxPx = (rectPx - vec2(0.5)) / atlasSize;
            vec2 uv = clamp(localUV01, vec2(0.0), vec2(1.0));
            vec2 regionUv = rectUv.xy + uv * rectUv.zw;
            return rectUv.xy + clamp(regionUv - rectUv.xy, minPx, maxPx);
        }
        vec4 sampleAtlasCombined(vec4 rectUv, vec2 grid, float frames, float fps, vec2 localUV01, float seed, float t) {
            float speed = mix(0.85, 1.10, hash11(seed * 31.7 + 2.3));
            float f = floor(t * fps * speed + seed * frames);
            float frame = mod(f, max(1.0, frames));
            float cols = max(1.0, grid.x);
            float rows = max(1.0, grid.y);
            float col = mod(frame, cols);
            float rowFromTop = floor(frame / cols);
            float row = (rows - 1.0) - rowFromTop;
            vec2 cellUVLocal = (vec2(col, row) + localUV01) / vec2(cols, rows);
            vec2 cellUv = clampUvToRegionPixels(cellUVLocal, rectUv);
            return texture(uTexture, cellUv);
        }
        float lickBlobs(float x, float y, vec2 advP, float flowY, float seed) {
            float k = y * 6.6 + flowY * 0.55;
            float seg = floor(k);
            float f = fract(k);
            float cx1 = (hash11(seg + seed * 31.0) - 0.5) * 0.95 * (1.0 - y);
            float cx2 = (hash11(seg + seed * 73.0) - 0.5) * 0.95 * (1.0 - y);
            float w = mix(0.34, 0.085, y);
            vec2 q1 = vec2((x - cx1) / w,        (f - 0.30) / 0.70);
            vec2 q2 = vec2((x - cx2) / (w*0.85), (f - 0.45) / 0.65);
            float m1 = 1.0 - smoothstep(0.60, 1.00, length(q1 * vec2(1.0, 1.45)));
            float m2 = 1.0 - smoothstep(0.60, 1.00, length(q2 * vec2(1.0, 1.60)));
            float br = fbm2D(advP * vec2(7.0, 12.0) + seed * 17.0);
            float broken = smoothstep(0.25, 0.88, br);
            float gate = smoothstep(0.05, 0.22, y) * (1.0 - smoothstep(0.86, 1.0, y));
            float m = (m1 + 0.85 * m2) * broken * gate;
            return clamp(m, 0.0, 1.0);
        }

        vec4 evalFireTailExact() {
            float age = clamp(vColor.r, 0.0, 1.0);
            float vSeed = clamp(vColor.g, 0.0, 1.0);
            float t = uMaterialTimeSec;

            // Legacy fire_tail.frag flips gl_PointCoord.y; shared quads already provide the legacy-facing orientation.
            vec2 uv = vUv;

            vec2 cc = (uv - 0.5) * 2.0;
            float x = cc.x;
            float y = clamp(uv.y, 0.0, 1.0);
            float bottomFade = smoothstep(0.00, 0.11, y);

            float baseT = smoothstep(0.00, 0.22, y);
            float xScaleBase = mix(2.55, 1.90, baseT);
            float yScaleBase = mix(1.05, 0.75, baseT);
            float reBase = length(vec2(cc.x * xScaleBase, cc.y * yScaleBase));
            float radialMaskBase = 1.0 - smoothstep(0.98, 1.10, reBase);
            float tightMask      = 1.0 - smoothstep(0.62, 0.88, reBase);

            float reLoose = length(cc * vec2(0.55, 0.85));
            float radialMaskLoose = 1.0 - smoothstep(0.98, 1.20, reLoose);

            float fade = (1.0 - age);
            fade = pow(mix(fade, 1.0, 0.25), 0.75);

            vec2 wobble = vec2(
                smoothFlicker(t * 0.9, vSeed + 0.17),
                smoothFlicker(t * 1.1, vSeed + 0.73)
            ) - 0.5;
            vec2 local1 = uv + wobble * 0.010;
            vec2 local2 = uv + wobble * 0.002;

            vec4 fb1 = vec4(1.0);
            vec4 fb2 = vec4(1.0);
            int has1 = (uMaterialFlags >= 0.5) ? 1 : 0;
            int has2 = (uMaterialFlags >= 2.5) ? 1 : 0;
            if (has1 == 1) {
                fb1 = sampleAtlasCombined(uMaterialRect0, uMaterialFlipbook0.xy, uMaterialFlipbook0.z, uMaterialFlipbook0.w, local1, vSeed, t);
                if (has2 == 1) {
                    fb2 = sampleAtlasCombined(uMaterialRect1, uMaterialFlipbook1.xy, uMaterialFlipbook1.z, uMaterialFlipbook1.w, local2, vSeed, t);
                } else {
                    fb2 = fb1;
                }
            }

            float fb1A   = clamp(fb1.a, 0.0, 1.0);
            float fb1Lum = clamp(dot(fb1.rgb, vec3(0.3333)), 0.0, 1.0);

            float speed = mix(0.95, 1.10, hash11(vSeed * 19.31));
            float flow  = t * 1.55 * speed;
            float flowY = flow * mix(0.75, 1.55, y * y);
            float width = mix(0.30, 0.055, pow(y, 2.35));
            float fb1Thicken = 2.80;
            float widthHybrid = width * fb1Thicken;
            float yy = (y * 2.0 - 1.0);
            yy = yy * 1.45 + 0.38;
            yy /= 1.12;
            vec2 p = vec2(x / widthHybrid, yy);
            p *= 1.22;
            float sway = fbm2D(vec2(x * 1.7, y * 3.8) + vec2(0.0, -flowY * 0.65) + vSeed * 7.0);
            p.x += (sway - 0.5) * 0.015 * (1.0 - y);
            float d0 = length(p);
            vec2 advP = advect(p * vec2(1.20, 1.0) + vSeed * 6.0, flowY, 0.25);
            float n = fbm2D(advP * vec2(2.7, 4.5) + vSeed * 11.0);
            float d = d0 + (n - 0.5) * 0.18 * (1.0 - y);
            float core  = clamp(1.0 - smoothstep(0.00, 0.88, d), 0.0, 1.0);
            float outer = clamp(1.0 - smoothstep(0.30, 1.05, d), 0.0, 1.0);
            float blobs = lickBlobs(x, y, advP, flowY, vSeed);
            float body  = clamp(smoothstep(0.92, 0.12, d), 0.0, 1.0);

            float procAlpha = body * (0.60 + 0.55 * blobs);
            procAlpha *= (0.92 + 0.15 * smoothFlicker(t * 1.2, vSeed));
            procAlpha *= bottomFade;
            procAlpha *= fade;
            procAlpha = 1.0 - exp(-procAlpha * 1.85);
            procAlpha = clamp(procAlpha, 0.0, 0.96);

            vec3 yellow = vec3(1.70, 1.20, 0.28);
            vec3 red    = vec3(1.45, 0.18, 0.06);
            vec3 orange = vec3(1.60, 0.55, 0.12);
            float wave = 0.5 + 0.5 * sin((x * 1.8 + y * 8.5 - flowY * 4.9) + vSeed * 7.0);
            float baseBoundary = 0.34;
            float segCount = 6.0;
            float kk = y * segCount - flowY * 0.55;
            float seg = floor(kk);
            float segRand  = hash11(seg + vSeed * 71.3);
            float segRand2 = hash11(seg + vSeed * 19.7 + 5.0);
            float tri1 = abs(fract((x * 0.85 + y * 1.05 - flowY * 0.18) * 2.8 + vSeed * 7.0) - 0.5) * 2.0;
            float tri2 = abs(fract((x * 1.10 - y * 0.60 - flowY * 0.14) * 3.8 + vSeed * 3.0) - 0.5) * 2.0;
            float zig = mix(tri1, tri2, 0.50 + 0.50 * (segRand - 0.5));
            zig = smoothstep(0.15, 0.85, zig);
            float warp = fbm2D(advect(vec2(x * 0.85, y * 1.2) + vSeed * 6.0, flowY, 0.22) * vec2(4.5, 7.5)) - 0.5;
            float jag = 0.0;
            jag += (segRand  - 0.5) * 0.10;
            jag += (segRand2 - 0.5) * 0.05;
            jag += (zig      - 0.5) * 0.14;
            jag += warp * 0.06;
            jag *= (1.0 - 0.55 * smoothstep(0.65, 1.0, y));
            float boundary = clamp(baseBoundary + jag, 0.14, 0.62);
            float splitWidth = 0.11;
            float redMask = smoothstep(boundary, boundary + splitWidth, y);
            vec3 procRgb = mix(yellow, red, redMask);
            float band = smoothstep(boundary - 0.02, boundary + 0.02, y) *
                         (1.0 - smoothstep(boundary + 0.02, boundary + 0.10, y));
            procRgb = mix(procRgb, orange, 0.55 * band);
            float climb = core * (1.0 - smoothstep(0.55, 0.95, y)) * (0.35 + 0.65 * wave);
            procRgb = mix(procRgb, yellow, 0.18 * climb);
            procRgb *= (1.18 + 0.35 * outer);

            vec3 hybridRgb = procRgb;
            float hybridAlpha = procAlpha;
            if (has1 == 1) {
                float aMod = mix(0.55, 1.65, fb1A);
                float lMod = mix(0.85, 1.25, fb1Lum);
                hybridAlpha = clamp(hybridAlpha * aMod, 0.0, 0.96);
                hybridRgb *= lMod;
                hybridRgb *= mix(vec3(1.0), fb1.rgb * 1.35, 0.30);
            }

            vec3 fb2Rgb = fb2.rgb;
            float fb2Alpha = pow(clamp(fb2.a, 0.0, 1.0), 0.66);
            float hot = smoothstep(0.10, 0.55, 1.0 - y);
            vec3 tint = mix(red, yellow, hot);
            fb2Rgb *= tint * 1.30;
            fb2Alpha *= tightMask;
            fb2Alpha *= bottomFade;

            float hybridMaskedA = hybridAlpha * radialMaskLoose * bottomFade;
            float fb2MaskedA    = fb2Alpha    * radialMaskBase;
            float mixW = 0.50;
            vec3 rgb = mix(hybridRgb, fb2Rgb, mixW);
            float alpha = mix(hybridMaskedA, fb2MaskedA, mixW);
            alpha *= fade;
            alpha = clamp(alpha + 0.10 * outer * fade, 0.0, 0.985);
            float exposure = 2.60;
            rgb *= exposure;
            float emissive = (0.85 * outer + 0.45 * core) * fade;
            rgb *= (1.0 + 2.10 * emissive);
            rgb = tonemapSoftLocal(rgb);
            if (alpha < 0.003) discard;
            rgb *= alpha;
            return vec4(rgb, alpha);
        }
    )GLSL"
    R"GLSL(
        vec3 srgbToLinear(vec3 c) {
            c = clamp(c, 0.0, 1.0);
            vec3 lo = c / 12.92;
            vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
            return mix(lo, hi, step(vec3(0.04045), c));
        }

        vec3 linearToSrgb(vec3 c) {
            c = max(c, vec3(0.0));
            vec3 lo = c * 12.92;
            vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
            return mix(lo, hi, step(vec3(0.0031308), c));
        }

        vec3 safeNormalize(vec3 value, vec3 fallback) {
            float len2 = dot(value, value);
            if (len2 < 1e-8) return fallback;
            return value * inversesqrt(len2);
        }

        vec3 rrtAndOdtFit(vec3 v) {
            vec3 a = v * (v + 0.0245786) - 0.000090537;
            vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
            return a / b;
        }

        vec3 linearToneMapping(vec3 color, float toneMappingExposure) {
            return clamp(toneMappingExposure * color, 0.0, 1.0);
        }

        vec3 tonemapACESFilmic(vec3 color, float toneMappingExposure) {
            // Matches three.js ACESFilmicToneMapping (including matrix orientation and /0.6 scale).
            const mat3 ACESInputMat = mat3(
                vec3(0.59719, 0.07600, 0.02840), // transposed from source
                vec3(0.35458, 0.90834, 0.13383),
                vec3(0.04823, 0.01566, 0.83777)
            );
            const mat3 ACESOutputMat = mat3(
                vec3( 1.60475, -0.10208, -0.00327), // transposed from source
                vec3(-0.53108,  1.10813, -0.07276),
                vec3(-0.07367, -0.00605,  1.07602)
            );
            color *= toneMappingExposure / 0.6;
            color = ACESInputMat * color;
            color = rrtAndOdtFit(color);
            color = ACESOutputMat * color;
            return clamp(color, 0.0, 1.0);
        }

        vec3 applyViewerToneMapping(vec3 color, float toneMappingMode, float toneMappingExposure) {
            // Matches three-gltf-viewer controls:
            //   Linear => 0, ACES Filmic => 1.
            if (toneMappingMode < 0.5) {
                return linearToneMapping(color, toneMappingExposure);
            }
            return tonemapACESFilmic(color, toneMappingExposure);
        }

        float distributionGGX(float NdotH, float roughness) {
            float a = roughness * roughness;
            float a2 = a * a;
            float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
            return a2 / max(3.14159265 * d * d, 1e-5);
        }

        float geometrySchlickGGX(float NdotV, float roughness) {
            float r = roughness + 1.0;
            float k = (r * r) * 0.125; // (r^2)/8
            return NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
        }

        float geometrySmith(float NdotV, float NdotL, float roughness) {
            return geometrySchlickGGX(NdotV, roughness) *
                   geometrySchlickGGX(NdotL, roughness);
        }

        vec3 fresnelSchlick(float cosTheta, vec3 F0) {
            float m = clamp(1.0 - cosTheta, 0.0, 1.0);
            float m2 = m * m;
            float m5 = m2 * m2 * m;
            return F0 + (vec3(1.0) - F0) * m5;
        }

        vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
            float m = clamp(1.0 - cosTheta, 0.0, 1.0);
            float m2 = m * m;
            float m5 = m2 * m2 * m;
            vec3 F90 = max(vec3(1.0 - roughness), F0);
            return F0 + (F90 - F0) * m5;
        }

        vec2 DFGApprox(const in vec3 normal, const in vec3 viewDir, const in float roughness) {
            float dotNV = clamp(dot(normal, viewDir), 0.0, 1.0);
            const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
            const vec4 c1 = vec4( 1.0,  0.0425,  1.04, -0.04);
            vec4 r = roughness * c0 + c1;
            float a004 = min(r.x * r.x, exp2(-9.28 * dotNV)) * r.x + r.y;
            vec2 fab = vec2(-1.04, 1.04) * a004 + r.zw;
            return fab;
        }

        void computeMultiscattering(const in vec3 normal,
                                    const in vec3 viewDir,
                                    const in vec3 specularColor,
                                    const in float specularF90,
                                    const in float roughness,
                                    inout vec3 singleScatter,
                                    inout vec3 multiScatter) {
            vec2 fab = DFGApprox(normal, viewDir, roughness);
            vec3 FssEss = specularColor * fab.x + specularF90 * fab.y;
            float Ess = fab.x + fab.y;
            float Ems = 1.0 - Ess;
            vec3 Favg = specularColor + (1.0 - specularColor) * 0.047619; // 1/21
            vec3 Fms = FssEss * Favg / max(1.0 - Ems * Favg, vec3(1e-5));
            singleScatter += FssEss;
            multiScatter += Fms * Ems;
        }

        vec3 sampleNeutralEnvironment(vec3 dir, float roughness) {
            vec3 d = safeNormalize(dir, vec3(0.0, 1.0, 0.0));
            // RoomEnvironment light directions (from three.js examples/jsm/environments/RoomEnvironment.js).
            const vec3 roomDirs[7] = vec3[](
                normalize(vec3(-0.6976730, 0.6220875,  0.3553301)), // light1
                normalize(vec3(-0.6310653, 0.7059674, -0.3215068)), // light2
                normalize(vec3( 0.7703826, 0.6305104, -0.0946954)), // light3
                normalize(vec3(-0.0271260, 0.5219704,  0.8525321)), // light4
                normalize(vec3( 0.1868756, 0.6635094, -0.7244534)), // light5
                normalize(vec3( 0.0,       1.0,        0.0      )), // light6
                normalize(vec3( 0.0257911, 0.9994960,  0.0185103))  // main point light
            );
            // Relative weights derived from RoomEnvironment authored intensities.
            // Area lights: 50, 50, 17, 43, 20, 100 ; point light: 900 with distance falloff.
            const float roomW[7] = float[](
                0.50, 0.50, 0.17, 0.43, 0.20, 1.00, 3.30
            );
            float blur = clamp(roughness * roughness, 0.0, 1.0);
            float lobePower = mix(64.0, 2.0, blur);
            float accum = 0.0;
            float accumW = 0.0;
            for (int i = 0; i < 7; ++i) {
                float nd = max(dot(d, roomDirs[i]), 0.0);
                accum += roomW[i] * pow(nd, lobePower);
                accumW += roomW[i];
            }
            // Normalize lobe energy to avoid over-bright, washed-out albedo.
            float directional = (accumW > 1e-6) ? (accum / accumW) : 0.0;
            // Keep a small floor bounce and stronger top hemisphere, similar to RoomEnvironment PMREM behavior.
            float hemi = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);
            float bounce = mix(0.045, 0.10, hemi);
            // Slightly stronger specular-side lobe at low roughness, flatter at high roughness.
            float lobeGain = mix(0.55, 0.30, blur);
            float neutral = bounce + directional * lobeGain;
            neutral = clamp(neutral, 0.0, 1.5);
            return vec3(neutral);
        }

        vec3 evalDirectPbr(vec3 n,
                           vec3 v,
                           vec3 l,
                           vec3 radiance,
                           vec3 albedo,
                           vec3 F0,
                           float roughness,
                           float metallic) {
            float NdotL = max(dot(n, l), 0.0);
            float NdotV = max(dot(n, v), 0.0);
            if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);
            vec3 h = normalize(v + l);
            float NdotH = max(dot(n, h), 0.0);
            float VdotH = max(dot(v, h), 0.0);

            float D = distributionGGX(NdotH, roughness);
            float G = geometrySmith(NdotV, NdotL, roughness);
            vec3  F = fresnelSchlick(VdotH, F0);
            vec3  spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
            return (kD * albedo / 3.14159265 + spec) * radiance * NdotL;
        }

        vec3 perturbNormal2Arb(vec3 eyePos, vec3 surfNorm, vec3 mapN, vec2 uv, float faceDirection) {
            // Mirrors three.js perturbNormal2Arb derivative basis construction.
            vec3 q0 = dFdx(eyePos.xyz);
            vec3 q1 = dFdy(eyePos.xyz);
            vec2 st0 = dFdx(uv);
            vec2 st1 = dFdy(uv);

            vec3 N = surfNorm;
            vec3 q1perp = cross(q1, N);
            vec3 q0perp = cross(N, q0);
            vec3 T = q1perp * st0.x + q0perp * st1.x;
            vec3 B = q1perp * st0.y + q0perp * st1.y;

            float det = max(dot(T, T), dot(B, B));
            // Use winding-independent basis scale to avoid per-side normal inversion.
            float scale = (det <= 1e-10) ? 0.0 : inversesqrt(det);
            return normalize(T * (mapN.x * scale) + B * (mapN.y * scale) + N * mapN.z);
        }

        vec3 computeMappedNormal(vec2 sampleUv) {
            float faceDirection = gl_FrontFacing ? 1.0 : -1.0;
            vec3 dx = dFdx(vWorldPos);
            vec3 dy = dFdy(vWorldPos);
            vec3 geomN = normalize(cross(dx, dy)) * faceDirection;
            vec3 n = normalize(vWorldNormal);
            if (dot(n, n) < 1e-6) n = geomN;
            // Ensure shading normal stays consistent with actual face orientation.
            if (dot(n, geomN) < 0.0) n = -n;
            if (uUseNormalTexture < 0.5) return n;

            vec3 mapN = texture(uNormalTexture, sampleUv).xyz * 2.0 - 1.0;
            mapN.xy *= max(uNormalScale, 0.0);
            mapN = normalize(mapN);
            vec3 mapped = perturbNormal2Arb(vWorldPos, n, mapN, sampleUv, faceDirection);
            if (dot(mapped, geomN) < 0.0) mapped = -mapped;
            return mapped;
        }

        vec3 applyWorldLitModel(vec3 linearColor, vec3 n, vec2 sampleUv) {
            vec3 orm = vec3(1.0, 1.0, 1.0);
            if (uUseMetallicRoughnessTexture > 0.5) {
                orm = texture(uMetallicRoughnessTexture, sampleUv).rgb;
            }
            float roughness = clamp(orm.g * clamp(uRoughnessFactor, 0.0, 1.0), 0.04, 1.0);
            float metallic = clamp(orm.b * clamp(uMetallicFactor, 0.0, 1.0), 0.0, 1.0);
            float ao = 1.0;
            if (uUseOcclusionTexture > 0.5) {
                float occTex = texture(uOcclusionTexture, sampleUv).r;
                ao = mix(1.0, occTex, clamp(uOcclusionStrength, 0.0, 1.0));
            }

            vec3 albedo = clamp(linearColor, 0.0, 1.0);
            vec3 F0 = mix(vec3(0.04), albedo, metallic);
            vec3 diffuseColor = albedo * (1.0 - metallic);
            const float specularF90 = 1.0;
            vec3 camForward = safeNormalize(
                uCameraForward,
                normalize(vec3(0.0, -0.6139406, -0.7893522)));
            vec3 camRight = cross(camForward, vec3(0.0, 1.0, 0.0));
            if (dot(camRight, camRight) < 1e-6) {
                camRight = cross(camForward, vec3(0.0, 0.0, 1.0));
            }
            camRight = safeNormalize(camRight, vec3(1.0, 0.0, 0.0));
            vec3 camUp = safeNormalize(cross(camRight, camForward), vec3(0.0, 1.0, 0.0));
            vec3 v = safeNormalize(uCameraPos - vWorldPos, -camForward);
            // three-gltf-viewer defaults:
            // punctualLights=true, ambientIntensity=0.3, directIntensity=0.8*PI, directColor=white.
            const vec3 directColor = vec3(1.0);
            const float directIntensity = 0.8 * 3.14159265;
            const vec3 ambientColor = vec3(1.0);
            const float ambientIntensity = 0.3;

            // Match three-gltf-viewer: directional light parented to camera at local (0.5, 0, 0.866).
            vec3 lightPos = uCameraPos + camRight * 0.5 + camUp * 0.0 - camForward * 0.8660254;
            vec3 l0 = safeNormalize(lightPos - uCameraTarget, vec3(0.45, 0.86, 0.24));
            vec3 direct = evalDirectPbr(
                n, v, l0, directColor * directIntensity, albedo, F0, roughness, metallic);

            float NdotV = max(dot(n, v), 0.0);
            vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

            vec3 r = reflect(-v, n);
            r = normalize(mix(r, n, roughness * roughness));
            vec3 envIrradiance = 3.14159265 * sampleNeutralEnvironment(n, 1.0);
            vec3 envRadiance = sampleNeutralEnvironment(r, roughness);
            vec3 singleScattering = vec3(0.0);
            vec3 multiScattering = vec3(0.0);
            computeMultiscattering(n, v, F0, specularF90, roughness, singleScattering, multiScattering);
            vec3 cosineWeightedIrradiance = envIrradiance * (1.0 / 3.14159265);
            vec3 totalScattering = singleScattering + multiScattering;
            float energyComp = 1.0 - max(max(totalScattering.r, totalScattering.g), totalScattering.b);
            vec3 diffuseIBL = diffuseColor * max(energyComp, 0.0) * cosineWeightedIrradiance;
            vec3 specularIBL = envRadiance * singleScattering + multiScattering * cosineWeightedIrradiance;
            vec3 ibl = (diffuseIBL + specularIBL) * ao;

            vec3 ambientLight = kD * albedo * ambientColor * ambientIntensity;
            vec3 shaded = direct + ibl + ambientLight;

            vec3 emissiveTex = (uUseEmissiveTexture > 0.5)
                ? clamp(texture(uEmissiveTexture, sampleUv).rgb, 0.0, 1.0)
                : vec3(1.0);
            vec3 emissive = emissiveTex * max(uEmissiveFactor, vec3(0.0));
            return max(shaded + emissive, vec3(0.0));
        }

        void main() {
            if (uMaterialMode > 0.5 && uMaterialMode < 1.5) {
                FragColor = evalFireTailExact();
                return;
            }
            vec4 tex = vec4(1.0);
            vec3 outLinear = clamp(vColor.rgb, 0.0, 1.0);
            vec2 wrappedUv = vec2(applyWrap(vUv.x, uWrapS), applyWrap(vUv.y, uWrapT));
            // Preserve full repeat/mirror detail; only clamp to texel center for clamp-to-edge.
            bool clampS = abs(uWrapS - 33071.0) < 0.5;
            bool clampT = abs(uWrapT - 33071.0) < 0.5;
            if (clampS || clampT) {
                wrappedUv = clampWrappedUvToTexelCenter(wrappedUv);
            }
            if (uUseTexture > 0.5) {
                tex = texture(uTexture, wrappedUv);
                outLinear = clamp(tex.rgb, 0.0, 1.0) * outLinear;
            }
            float outA = clamp(vColor.a * tex.a, 0.0, 1.0);
            if (uAlphaMode < 0.5) {
                outA = clamp(vColor.a, 0.0, 1.0);
            } else if (uAlphaMode < 1.5) {
                if (outA < clamp(uAlphaCutoff, 0.0, 1.0)) discard;
                outA = clamp(vColor.a, 0.0, 1.0);
            }
            if (uMaterialMode >= 1.5) {
                vec3 n = computeMappedNormal(wrappedUv);
                outLinear = applyWorldLitModel(outLinear, n, wrappedUv);
            }
            // three-gltf-viewer semantics:
            //   toneMappingExposure = pow(2, exposure), exposure default = 0.
            const float toneMappingExposure = 1.0;
            // three-gltf-viewer: toneMapping = ACES Filmic.
            const float toneMappingMode = 1.0;
            vec3 mapped = applyViewerToneMapping(max(outLinear, vec3(0.0)), toneMappingMode, toneMappingExposure);
            vec3 outSrgb = linearToSrgb(mapped);
            FragColor = vec4(outSrgb, outA);
        }
    )GLSL";

    const unsigned int vs = opengl_backend_shader_utils::compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = opengl_backend_shader_utils::compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }

    worldProgram_ = opengl_backend_shader_utils::linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (worldProgram_ == 0) return;

    worldViewProjLoc_ = glGetUniformLocation(worldProgram_, "uViewProj");
    worldModelLoc_ = glGetUniformLocation(worldProgram_, "uModel");
    worldUseTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseTexture");
    worldTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uTexture");
    worldUseNormalTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseNormalTexture");
    worldUseMetallicRoughnessTextureLoc_ =
        glGetUniformLocation(worldProgram_, "uUseMetallicRoughnessTexture");
    worldUseOcclusionTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseOcclusionTexture");
    worldUseEmissiveTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseEmissiveTexture");
    worldNormalTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uNormalTexture");
    worldMetallicRoughnessTextureSamplerLoc_ =
        glGetUniformLocation(worldProgram_, "uMetallicRoughnessTexture");
    worldOcclusionTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uOcclusionTexture");
    worldEmissiveTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uEmissiveTexture");
    worldWrapSLoc_ = glGetUniformLocation(worldProgram_, "uWrapS");
    worldWrapTLoc_ = glGetUniformLocation(worldProgram_, "uWrapT");
    worldAlphaModeLoc_ = glGetUniformLocation(worldProgram_, "uAlphaMode");
    worldAlphaCutoffLoc_ = glGetUniformLocation(worldProgram_, "uAlphaCutoff");
    worldCameraPosLoc_ = glGetUniformLocation(worldProgram_, "uCameraPos");
    worldCameraForwardLoc_ = glGetUniformLocation(worldProgram_, "uCameraForward");
    worldCameraTargetLoc_ = glGetUniformLocation(worldProgram_, "uCameraTarget");
    worldNormalScaleLoc_ = glGetUniformLocation(worldProgram_, "uNormalScale");
    worldMetallicFactorLoc_ = glGetUniformLocation(worldProgram_, "uMetallicFactor");
    worldRoughnessFactorLoc_ = glGetUniformLocation(worldProgram_, "uRoughnessFactor");
    worldOcclusionStrengthLoc_ = glGetUniformLocation(worldProgram_, "uOcclusionStrength");
    worldEmissiveFactorLoc_ = glGetUniformLocation(worldProgram_, "uEmissiveFactor");
    worldMaterialModeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialMode");
    worldMaterialTimeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialTimeSec");
    worldMaterialFlagsLoc_ = glGetUniformLocation(worldProgram_, "uMaterialFlags");
    worldMaterialAtlasSizeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialAtlasSize");
    worldMaterialRect0Loc_ = glGetUniformLocation(worldProgram_, "uMaterialRect0");
    worldMaterialRect1Loc_ = glGetUniformLocation(worldProgram_, "uMaterialRect1");
    worldMaterialFlipbook0Loc_ = glGetUniformLocation(worldProgram_, "uMaterialFlipbook0");
    worldMaterialFlipbook1Loc_ = glGetUniformLocation(worldProgram_, "uMaterialFlipbook1");
    worldSkinningEnabledLoc_ = glGetUniformLocation(worldProgram_, "uSkinningEnabled");
    worldSkinMatrixCountLoc_ = glGetUniformLocation(worldProgram_, "uSkinMatrixCount");
    worldSkinMatricesLoc_ = glGetUniformLocation(worldProgram_, "uSkinMatrices[0]");
    if (worldViewProjLoc_ < 0 || worldModelLoc_ < 0 ||
        worldUseTextureLoc_ < 0 || worldTextureSamplerLoc_ < 0 ||
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0 ||
        worldCameraPosLoc_ < 0 || worldCameraForwardLoc_ < 0 ||
        worldMaterialModeLoc_ < 0 || worldMaterialTimeLoc_ < 0 || worldMaterialFlagsLoc_ < 0 ||
        worldMaterialAtlasSizeLoc_ < 0 || worldMaterialRect0Loc_ < 0 || worldMaterialRect1Loc_ < 0 ||
        worldMaterialFlipbook0Loc_ < 0 || worldMaterialFlipbook1Loc_ < 0 ||
        worldSkinningEnabledLoc_ < 0 || worldSkinMatrixCountLoc_ < 0 || worldSkinMatricesLoc_ < 0) {
        destroyWorldPipeline();
        return;
    }

    glGenVertexArrays(1, &worldVao_);
    glGenBuffers(1, &worldVbo_);
    glGenBuffers(1, &worldIbo_);
    if (worldVao_ == 0 || worldVbo_ == 0 || worldIbo_ == 0) {
        destroyWorldPipeline();
        return;
    }

    glBindVertexArray(worldVao_);
    glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(WorldMeshVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, r)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, nx)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, joint0)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(WorldMeshVertex, weight0)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLRenderBackend::destroyWorldPipeline() {
    if (worldIbo_ != 0) {
        glDeleteBuffers(1, &worldIbo_);
        worldIbo_ = 0;
    }
    if (worldVbo_ != 0) {
        glDeleteBuffers(1, &worldVbo_);
        worldVbo_ = 0;
    }
    if (worldVao_ != 0) {
        glDeleteVertexArrays(1, &worldVao_);
        worldVao_ = 0;
    }
    if (worldProgram_ != 0) {
        glDeleteProgram(worldProgram_);
        worldProgram_ = 0;
    }
    worldViewProjLoc_ = -1;
    worldModelLoc_ = -1;
    worldUseTextureLoc_ = -1;
    worldTextureSamplerLoc_ = -1;
    worldUseNormalTextureLoc_ = -1;
    worldUseMetallicRoughnessTextureLoc_ = -1;
    worldUseOcclusionTextureLoc_ = -1;
    worldUseEmissiveTextureLoc_ = -1;
    worldNormalTextureSamplerLoc_ = -1;
    worldMetallicRoughnessTextureSamplerLoc_ = -1;
    worldOcclusionTextureSamplerLoc_ = -1;
    worldEmissiveTextureSamplerLoc_ = -1;
    worldWrapSLoc_ = -1;
    worldWrapTLoc_ = -1;
    worldAlphaModeLoc_ = -1;
    worldAlphaCutoffLoc_ = -1;
    worldCameraPosLoc_ = -1;
    worldCameraForwardLoc_ = -1;
    worldCameraTargetLoc_ = -1;
    worldNormalScaleLoc_ = -1;
    worldMetallicFactorLoc_ = -1;
    worldRoughnessFactorLoc_ = -1;
    worldOcclusionStrengthLoc_ = -1;
    worldEmissiveFactorLoc_ = -1;
    worldMaterialModeLoc_ = -1;
    worldMaterialTimeLoc_ = -1;
    worldMaterialFlagsLoc_ = -1;
    worldMaterialAtlasSizeLoc_ = -1;
    worldMaterialRect0Loc_ = -1;
    worldMaterialRect1Loc_ = -1;
    worldMaterialFlipbook0Loc_ = -1;
    worldMaterialFlipbook1Loc_ = -1;
    worldSkinningEnabledLoc_ = -1;
    worldSkinMatrixCountLoc_ = -1;
    worldSkinMatricesLoc_ = -1;
}


