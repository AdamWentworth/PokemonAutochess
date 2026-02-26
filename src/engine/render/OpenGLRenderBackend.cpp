#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

#include <glad/glad.h>
#include "engine/render/Renderer.h"
#include "engine/render/DebugGeometry.h"

namespace {

std::string toLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool containsCi(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

unsigned int compileShader(GLenum type, const char* source) {
    const unsigned int shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned int linkProgram(unsigned int vs, unsigned int fs) {
    if (vs == 0 || fs == 0) return 0;
    const unsigned int program = glCreateProgram();
    if (program == 0) return 0;
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";

} // namespace

OpenGLRenderBackend::OpenGLRenderBackend()
    : renderer_(std::make_unique<Renderer>()) {
    glEnable(GL_DEPTH_TEST);
}

OpenGLRenderBackend::~OpenGLRenderBackend() {
    shutdown();
}

void OpenGLRenderBackend::beginFrame(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderBackend::onResize(int width, int height) {
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

std::string OpenGLRenderBackend::activeGpuName() const {
    const GLubyte* renderer = glGetString(GL_RENDERER);
    return renderer ? reinterpret_cast<const char*>(renderer) : std::string{};
}

bool OpenGLRenderBackend::activeGpuIsDiscrete() const {
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const std::string vendorStr = vendor ? reinterpret_cast<const char*>(vendor) : "";
    const std::string rendererStr = renderer ? reinterpret_cast<const char*>(renderer) : "";
    return !containsCi(vendorStr, "intel") && !containsCi(rendererStr, "intel");
}

void OpenGLRenderBackend::shutdown() {
    destroyDebugPipeline();
    destroyWorldPipeline();
    destroySpritePipeline();
    clearTextureCaches();
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
}

void OpenGLRenderBackend::ensureDebugPipeline() {
    if (debugProgram_ != 0 && debugVao_ != 0 && debugVbo_ != 0 && debugSurfaceSizeLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) {
        return;
    }

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform vec2 uSurfaceSize;
        out vec4 vColor;
        void main() {
            vec2 ndc;
            ndc.x = (aPos.x / max(uSurfaceSize.x, 1.0)) * 2.0 - 1.0;
            ndc.y = 1.0 - (aPos.y / max(uSurfaceSize.y, 1.0)) * 2.0;
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vColor;
        }
    )GLSL";

    const unsigned int vs = compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }

    debugProgram_ = glCreateProgram();
    if (debugProgram_ == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }

    glAttachShader(debugProgram_, vs);
    glAttachShader(debugProgram_, fs);
    glLinkProgram(debugProgram_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkOk = 0;
    glGetProgramiv(debugProgram_, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        glDeleteProgram(debugProgram_);
        debugProgram_ = 0;
        return;
    }

    debugSurfaceSizeLoc_ = glGetUniformLocation(debugProgram_, "uSurfaceSize");
    if (debugSurfaceSizeLoc_ < 0) {
        destroyDebugPipeline();
        return;
    }

    glGenVertexArrays(1, &debugVao_);
    glGenBuffers(1, &debugVbo_);
    if (debugVao_ == 0 || debugVbo_ == 0) {
        destroyDebugPipeline();
        return;
    }

    glBindVertexArray(debugVao_);
    glBindBuffer(GL_ARRAY_BUFFER, debugVbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(float) * 6);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 2));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void OpenGLRenderBackend::destroyDebugPipeline() {
    if (debugVbo_ != 0) {
        glDeleteBuffers(1, &debugVbo_);
        debugVbo_ = 0;
    }
    if (debugVao_ != 0) {
        glDeleteVertexArrays(1, &debugVao_);
        debugVao_ = 0;
    }
    if (debugProgram_ != 0) {
        glDeleteProgram(debugProgram_);
        debugProgram_ = 0;
    }
    debugSurfaceSizeLoc_ = -1;
}

void OpenGLRenderBackend::ensureWorldPipeline() {
    if (worldProgram_ != 0 && worldVao_ != 0 && worldVbo_ != 0 && worldIbo_ != 0 &&
        worldViewProjLoc_ >= 0 && worldUseTextureLoc_ >= 0 && worldTextureSamplerLoc_ >= 0 &&
        worldWrapSLoc_ >= 0 && worldWrapTLoc_ >= 0 && worldAlphaModeLoc_ >= 0 && worldAlphaCutoffLoc_ >= 0 &&
        worldMaterialModeLoc_ >= 0 && worldMaterialTimeLoc_ >= 0 && worldMaterialFlagsLoc_ >= 0 &&
        worldMaterialAtlasSizeLoc_ >= 0 && worldMaterialRect0Loc_ >= 0 && worldMaterialRect1Loc_ >= 0 &&
        worldMaterialFlipbook0Loc_ >= 0 && worldMaterialFlipbook1Loc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) return;

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in vec4 aColor;
        uniform mat4 uViewProj;
        out vec2 vUv;
        out vec4 vColor;
        void main() {
            gl_Position = uViewProj * vec4(aPos, 1.0);
            vUv = aUv;
            vColor = aColor;
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec2 vUv;
        in vec4 vColor;
        uniform float uUseTexture;
        uniform float uWrapS;
        uniform float uWrapT;
        uniform float uAlphaMode;
        uniform float uAlphaCutoff;
        uniform float uMaterialMode;
        uniform float uMaterialTimeSec;
        uniform float uMaterialFlags;
        uniform vec2  uMaterialAtlasSize;
        uniform vec4  uMaterialRect0;
        uniform vec4  uMaterialRect1;
        uniform vec4  uMaterialFlipbook0;
        uniform vec4  uMaterialFlipbook1;
        uniform sampler2D uTexture;
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

        void main() {
            if (uMaterialMode > 0.5) {
                FragColor = evalFireTailExact();
                return;
            }
            vec4 tex = vec4(1.0);
            vec3 outSrgb = clamp(vColor.rgb, 0.0, 1.0);
            if (uUseTexture > 0.5) {
                vec2 uv = vec2(applyWrap(vUv.x, uWrapS), applyWrap(vUv.y, uWrapT));
                uv = clampWrappedUvToTexelCenter(uv);
                tex = texture(uTexture, uv);
                outSrgb = clamp(tex.rgb * vColor.rgb, 0.0, 1.0);
            }
            float outA = clamp(vColor.a * tex.a, 0.0, 1.0);
            if (uAlphaMode < 0.5) {
                outA = clamp(vColor.a, 0.0, 1.0);
            } else if (uAlphaMode < 1.5) {
                if (outA < clamp(uAlphaCutoff, 0.0, 1.0)) discard;
                outA = clamp(vColor.a, 0.0, 1.0);
            }
            FragColor = vec4(outSrgb, outA);
        }
    )GLSL";

    const unsigned int vs = compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }

    worldProgram_ = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (worldProgram_ == 0) return;

    worldViewProjLoc_ = glGetUniformLocation(worldProgram_, "uViewProj");
    worldUseTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseTexture");
    worldTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uTexture");
    worldWrapSLoc_ = glGetUniformLocation(worldProgram_, "uWrapS");
    worldWrapTLoc_ = glGetUniformLocation(worldProgram_, "uWrapT");
    worldAlphaModeLoc_ = glGetUniformLocation(worldProgram_, "uAlphaMode");
    worldAlphaCutoffLoc_ = glGetUniformLocation(worldProgram_, "uAlphaCutoff");
    worldMaterialModeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialMode");
    worldMaterialTimeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialTimeSec");
    worldMaterialFlagsLoc_ = glGetUniformLocation(worldProgram_, "uMaterialFlags");
    worldMaterialAtlasSizeLoc_ = glGetUniformLocation(worldProgram_, "uMaterialAtlasSize");
    worldMaterialRect0Loc_ = glGetUniformLocation(worldProgram_, "uMaterialRect0");
    worldMaterialRect1Loc_ = glGetUniformLocation(worldProgram_, "uMaterialRect1");
    worldMaterialFlipbook0Loc_ = glGetUniformLocation(worldProgram_, "uMaterialFlipbook0");
    worldMaterialFlipbook1Loc_ = glGetUniformLocation(worldProgram_, "uMaterialFlipbook1");
    if (worldViewProjLoc_ < 0 || worldUseTextureLoc_ < 0 || worldTextureSamplerLoc_ < 0 ||
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0 ||
        worldMaterialModeLoc_ < 0 || worldMaterialTimeLoc_ < 0 || worldMaterialFlagsLoc_ < 0 ||
        worldMaterialAtlasSizeLoc_ < 0 || worldMaterialRect0Loc_ < 0 || worldMaterialRect1Loc_ < 0 ||
        worldMaterialFlipbook0Loc_ < 0 || worldMaterialFlipbook1Loc_ < 0) {
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
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 5));

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
    worldUseTextureLoc_ = -1;
    worldTextureSamplerLoc_ = -1;
    worldWrapSLoc_ = -1;
    worldWrapTLoc_ = -1;
    worldAlphaModeLoc_ = -1;
    worldAlphaCutoffLoc_ = -1;
    worldMaterialModeLoc_ = -1;
    worldMaterialTimeLoc_ = -1;
    worldMaterialFlagsLoc_ = -1;
    worldMaterialAtlasSizeLoc_ = -1;
    worldMaterialRect0Loc_ = -1;
    worldMaterialRect1Loc_ = -1;
    worldMaterialFlipbook0Loc_ = -1;
    worldMaterialFlipbook1Loc_ = -1;
}

void OpenGLRenderBackend::ensureSpritePipeline() {
    if (spriteProgram_ != 0 && spriteVao_ != 0 && spriteVbo_ != 0 &&
        spriteSurfaceSizeLoc_ >= 0 && spriteSamplerLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) return;

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in vec4 aColor;
        uniform vec2 uSurfaceSize;
        out vec2 vUv;
        out vec4 vColor;
        void main() {
            vec2 ndc;
            ndc.x = (aPos.x / max(uSurfaceSize.x, 1.0)) * 2.0 - 1.0;
            ndc.y = 1.0 - (aPos.y / max(uSurfaceSize.y, 1.0)) * 2.0;
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUv = aUv;
            vColor = aColor;
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec2 vUv;
        in vec4 vColor;
        uniform sampler2D uTexture;
        out vec4 FragColor;
        void main() {
            FragColor = texture(uTexture, vUv) * vColor;
        }
    )GLSL";

    const unsigned int vs = compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }
    spriteProgram_ = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (spriteProgram_ == 0) return;

    spriteSurfaceSizeLoc_ = glGetUniformLocation(spriteProgram_, "uSurfaceSize");
    spriteSamplerLoc_ = glGetUniformLocation(spriteProgram_, "uTexture");
    if (spriteSurfaceSizeLoc_ < 0 || spriteSamplerLoc_ < 0) {
        destroySpritePipeline();
        return;
    }

    glGenVertexArrays(1, &spriteVao_);
    glGenBuffers(1, &spriteVbo_);
    if (spriteVao_ == 0 || spriteVbo_ == 0) {
        destroySpritePipeline();
        return;
    }

    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(float) * 8);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 2));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 4));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (spriteFallbackTexture_ == 0) {
        static const unsigned char kFallbackRgba[16] = {
            72,  90, 108, 255,
            56,  70,  84, 255,
            56,  70,  84, 255,
            72,  90, 108, 255
        };
        glGenTextures(1, &spriteFallbackTexture_);
        if (spriteFallbackTexture_ != 0) {
            glBindTexture(GL_TEXTURE_2D, spriteFallbackTexture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA8,
                         2,
                         2,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         kFallbackRgba);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        spriteTextures_[kFallbackSpriteTextureKey] = spriteFallbackTexture_;
    }
}

void OpenGLRenderBackend::destroySpritePipeline() {
    if (spriteVbo_ != 0) {
        glDeleteBuffers(1, &spriteVbo_);
        spriteVbo_ = 0;
    }
    if (spriteVao_ != 0) {
        glDeleteVertexArrays(1, &spriteVao_);
        spriteVao_ = 0;
    }
    if (spriteProgram_ != 0) {
        glDeleteProgram(spriteProgram_);
        spriteProgram_ = 0;
    }
    spriteSurfaceSizeLoc_ = -1;
    spriteSamplerLoc_ = -1;
}

