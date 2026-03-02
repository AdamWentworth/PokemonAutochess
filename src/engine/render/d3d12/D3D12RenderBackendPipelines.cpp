#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <stdexcept>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

using namespace engine::render::d3d12_internal;
#endif

void D3D12RenderBackend::createDebugPipeline() {
#if defined(_WIN32)
    static constexpr char kVsSource[] =
        "struct VSIn { float2 pos : POSITION; float4 col : COLOR; };"
        "struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };"
        "VSOut main(VSIn i) { VSOut o; o.pos = float4(i.pos, 0.0, 1.0); o.col = i.col; return o; }";
    static constexpr char kPsSource[] =
        "struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR; };"
        "float4 main(PSIn i) : SV_TARGET { return i.col; }";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSource, sizeof(kVsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "vs_5_0", 0, 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for debug VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", 0, 0, psBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !psBlob) {
        throw std::runtime_error("D3DCompile failed for debug PS.");
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(debugRootSignature_.ReleaseAndGetAddressOf()))) ||
        !debugRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 debug pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = debugRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = TRUE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0] = rtBlend;
    pso.BlendState = blend;

    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = FALSE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.StencilEnable = FALSE;
    depthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    depthStencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.BackFace = depthStencil.FrontFace;
    pso.DepthStencilState = depthStencil;

    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(debugPipelineState_.ReleaseAndGetAddressOf()))) ||
        !debugPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 debug pipeline.");
    }

    constexpr std::size_t kBufferBytes = kMaxDebugVertices * sizeof(DebugVertex);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = kBufferBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &bufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(debugVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !debugVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 debug vertex buffer.");
    }

    debugVertexBufferGpuAddress_ = debugVertexBuffer_->GetGPUVirtualAddress();
    debugVertexStride_ = sizeof(DebugVertex);
    debugVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    debugVertexMappedData_ = nullptr;
    void* debugMapped = nullptr;
    D3D12_RANGE debugReadRange{0, 0};
    if (FAILED(debugVertexBuffer_->Map(0, &debugReadRange, &debugMapped)) || !debugMapped) {
        throw std::runtime_error("Map failed for D3D12 debug vertex buffer.");
    }
    debugVertexMappedData_ = static_cast<std::uint8_t*>(debugMapped);
#endif
}

void D3D12RenderBackend::createWorldPipeline() {
#if defined(_WIN32)
    static constexpr char kVsSource[] =
        "cbuffer VSConstants : register(b0) { float4x4 uViewProj; float4x4 uModel; };"
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; float3 worldPos : TEXCOORD1; };"
        "VSOut main(VSIn i) {"
        "  VSOut o;"
        "  float4 world = mul(uModel, float4(i.pos, 1.0f));"
        "  float4 clip = mul(uViewProj, world);"
        "  clip.z = clip.z * 0.5f + clip.w * 0.5f;"
        "  o.pos = clip;"
        "  o.uv = i.uv;"
        "  o.col = i.col;"
        "  o.worldPos = world.xyz;"
        "  return o;"
        "}";
    static constexpr char kPsSource[] = R"HLSL(
cbuffer PSConstants : register(b1) {
  float uUseTexture;
  float uWrapS;
  float uWrapT;
  float uAlphaMode;
  float uAlphaCutoff;
  float uMaterialMode;
  float uMaterialTimeSec;
  float uMaterialFlags;
  float uMaterialAtlasWidth;
  float uMaterialAtlasHeight;
  float uMaterialRect0U;
  float uMaterialRect0V;
  float uMaterialRect0W;
  float uMaterialRect0H;
  float uMaterialRect1U;
  float uMaterialRect1V;
  float uMaterialRect1W;
  float uMaterialRect1H;
  float uMaterialFlipbook0Cols;
  float uMaterialFlipbook0Rows;
  float uMaterialFlipbook0Frames;
  float uMaterialFlipbook0Fps;
  float uMaterialFlipbook1Cols;
  float uMaterialFlipbook1Rows;
  float uMaterialFlipbook1Frames;
  float uMaterialFlipbook1Fps;
  float uCharacterInkingEnabled;
};
Texture2D gTex : register(t0);
SamplerState gSampCC : register(s0);
SamplerState gSampRR : register(s1);
SamplerState gSampCR : register(s2);
SamplerState gSampRC : register(s3);
SamplerState gSampMR : register(s4);
SamplerState gSampRM : register(s5);
SamplerState gSampMM : register(s6);
SamplerState gSampCM : register(s7);
SamplerState gSampMC : register(s8);
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; float3 worldPos : TEXCOORD1; };

float applyWrap(float coord, float mode) {
  if (abs(mode - 33071.0f) < 0.5f) return saturate(coord);
  if (abs(mode - 33648.0f) < 0.5f) {
    float i = floor(coord);
    float f = frac(coord);
    float odd = fmod(abs(i), 2.0f);
    return (odd >= 1.0f) ? (1.0f - f) : f;
  }
  return frac(coord);
}
float2 clampWrappedUvToTexelCenter(float2 uv) {
  uint w = 1, h = 1;
  gTex.GetDimensions(w, h);
  float2 texSize = max(float2((float)w, (float)h), float2(1.0f, 1.0f));
  float2 halfTexel = 0.5f / texSize;
  return clamp(uv, halfTexel, 1.0f.xx - halfTexel);
}
bool isClampWrap(float mode) { return abs(mode - 33071.0f) < 0.5f; }
bool isMirrorWrap(float mode) { return abs(mode - 33648.0f) < 0.5f; }
float4 sampleWorldTextureWithWrap(float2 uv, float2 uvDx, float2 uvDy) {
  bool sClamp = isClampWrap(uWrapS);
  bool tClamp = isClampWrap(uWrapT);
  bool sMirror = isMirrorWrap(uWrapS);
  bool tMirror = isMirrorWrap(uWrapT);

  if (sClamp && tClamp) return gTex.SampleGrad(gSampCC, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && !tClamp && !tMirror) return gTex.SampleGrad(gSampRR, uv, uvDx, uvDy);
  if (sClamp && !tClamp && !tMirror) return gTex.SampleGrad(gSampCR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tClamp) return gTex.SampleGrad(gSampRC, uv, uvDx, uvDy);
  if (sMirror && !tClamp && !tMirror) return gTex.SampleGrad(gSampMR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tMirror) return gTex.SampleGrad(gSampRM, uv, uvDx, uvDy);
  if (sMirror && tMirror) return gTex.SampleGrad(gSampMM, uv, uvDx, uvDy);
  if (sClamp && tMirror) return gTex.SampleGrad(gSampCM, uv, uvDx, uvDy);
  if (sMirror && tClamp) return gTex.SampleGrad(gSampMC, uv, uvDx, uvDy);
  return gTex.SampleGrad(gSampRR, uv, uvDx, uvDy);
}

float hash11(float x) { return frac(sin(x * 12.9898f) * 43758.5453f); }
float hash21(float2 p) {
  float n = dot(p, float2(127.1f, 311.7f));
  return frac(sin(n) * 43758.5453f);
}
float valueNoise2D(float2 p) {
  float2 i = floor(p);
  float2 f = frac(p);
  float2 u = f * f * (3.0f - 2.0f * f);
  float a = hash21(i);
  float b = hash21(i + float2(1.0f, 0.0f));
  float c = hash21(i + float2(0.0f, 1.0f));
  float d = hash21(i + float2(1.0f, 1.0f));
  return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float smoothFlicker(float t, float seed) {
  float x = t * 9.0f + seed * 97.0f;
  float i = floor(x);
  float f = frac(x);
  f = f * f * (3.0f - 2.0f * f);
  return lerp(hash11(i), hash11(i + 1.0f), f);
}
float fbm2D(float2 p) {
  float v = 0.0f;
  float a = 0.5f;
  [unroll]
  for (int k = 0; k < 5; ++k) {
    v += a * valueNoise2D(p);
    p *= 2.02f;
    a *= 0.5f;
  }
  return v;
}
float2 fbmGrad(float2 p) {
  float e = 0.03f;
  float nx = fbm2D(p + float2(e, 0.0f)) - fbm2D(p - float2(e, 0.0f));
  float ny = fbm2D(p + float2(0.0f, e)) - fbm2D(p - float2(0.0f, e));
  return float2(nx, ny) / (2.0f * e);
}
float2 curl2D(float2 p) {
  float2 g = fbmGrad(p);
  return float2(g.y, -g.x);
}
float2 advect2D(float2 p, float flowY, float amount) {
  float2 c1 = curl2D(p * 1.30f + float2(0.0f, -flowY * 0.10f));
  float2 c2 = curl2D(p * 2.70f + float2(3.1f, -flowY * 0.18f));
  return p + (c1 * 0.65f + c2 * 0.35f) * amount;
}
float3 tonemapSoftLocal(float3 c) { return c / (1.0f + c); }

float3 srgbToLinear(float3 c) {
  c = saturate(c);
  float3 lo = c / 12.92f;
  float3 hi = pow((c + 0.055f) / 1.055f, 2.4f);
  return lerp(lo, hi, step(float3(0.04045f, 0.04045f, 0.04045f), c));
}

float3 linearToSrgb(float3 c) {
  c = max(c, float3(0.0f, 0.0f, 0.0f));
  float3 lo = c * 12.92f;
  float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
  return lerp(lo, hi, step(float3(0.0031308f, 0.0031308f, 0.0031308f), c));
}

float2 clampUvToRegionPixels(float2 localUV01, float4 rectUv) {
  float2 atlasSize = max(float2(uMaterialAtlasWidth, uMaterialAtlasHeight), float2(1.0f, 1.0f));
  float2 rectPx = max(rectUv.zw * atlasSize, float2(1.0f, 1.0f));
  float2 minPx = float2(0.5f, 0.5f) / atlasSize;
  float2 maxPx = (rectPx - float2(0.5f, 0.5f)) / atlasSize;
  float2 uv = saturate(localUV01);
  float2 regionUv = rectUv.xy + uv * rectUv.zw;
  return rectUv.xy + clamp(regionUv - rectUv.xy, minPx, maxPx);
}

float4 sampleAtlasCombined(float4 rectUv, float2 grid, float frames, float fps, float2 localUV01, float seed, float t) {
  float speed = lerp(0.85f, 1.10f, hash11(seed * 31.7f + 2.3f));
  float f = floor(t * fps * speed + seed * frames);
  float frame = fmod(f, max(1.0f, frames));
  if (frame < 0.0f) frame += max(1.0f, frames);
  float cols = max(1.0f, grid.x);
  float rows = max(1.0f, grid.y);
  float col = fmod(frame, cols);
  float rowFromTop = floor(frame / cols);
  float row = (rows - 1.0f) - rowFromTop;
  float2 cellUVLocal = (float2(col, row) + localUV01) / float2(cols, rows);
  float2 cellUv = clampUvToRegionPixels(cellUVLocal, rectUv);
  return gTex.Sample(gSampCC, cellUv);
}

float lickBlobs(float x, float y, float2 advP, float flowY, float seed) {
  float k = y * 6.6f + flowY * 0.55f;
  float seg = floor(k);
  float f = frac(k);
  float cx1 = (hash11(seg + seed * 31.0f) - 0.5f) * 0.95f * (1.0f - y);
  float cx2 = (hash11(seg + seed * 73.0f) - 0.5f) * 0.95f * (1.0f - y);
  float w = lerp(0.34f, 0.085f, y);
  float2 q1 = float2((x - cx1) / w,        (f - 0.30f) / 0.70f);
  float2 q2 = float2((x - cx2) / (w*0.85f),(f - 0.45f) / 0.65f);
  float m1 = 1.0f - smoothstep(0.60f, 1.00f, length(q1 * float2(1.0f, 1.45f)));
  float m2 = 1.0f - smoothstep(0.60f, 1.00f, length(q2 * float2(1.0f, 1.60f)));
  float br = fbm2D(advP * float2(7.0f, 12.0f) + seed * 17.0f);
  float broken = smoothstep(0.25f, 0.88f, br);
  float gate = smoothstep(0.05f, 0.22f, y) * (1.0f - smoothstep(0.86f, 1.0f, y));
  float m = (m1 + 0.85f * m2) * broken * gate;
  return saturate(m);
}

float4 evalFireTailExact(PSIn i) {
  float age = saturate(i.col.r);
  float vSeed = saturate(i.col.g);
  float t = uMaterialTimeSec;
  // Legacy fire_tail.frag flips gl_PointCoord.y; shared quads already provide the legacy-facing orientation.
  float2 uv = i.uv;
  float2 cc = (uv - 0.5f) * 2.0f;
  float x = cc.x;
  float y = saturate(uv.y);
  float bottomFade = smoothstep(0.00f, 0.11f, y);

  float baseT = smoothstep(0.00f, 0.22f, y);
  float xScaleBase = lerp(2.55f, 1.90f, baseT);
  float yScaleBase = lerp(1.05f, 0.75f, baseT);
  float reBase = length(float2(cc.x * xScaleBase, cc.y * yScaleBase));
  float radialMaskBase = 1.0f - smoothstep(0.98f, 1.10f, reBase);
  float tightMask = 1.0f - smoothstep(0.62f, 0.88f, reBase);
  float reLoose = length(cc * float2(0.55f, 0.85f));
  float radialMaskLoose = 1.0f - smoothstep(0.98f, 1.20f, reLoose);

  float fade = (1.0f - age);
  fade = pow(lerp(fade, 1.0f, 0.25f), 0.75f);

  float2 wobble = float2(
    smoothFlicker(t * 0.9f, vSeed + 0.17f),
    smoothFlicker(t * 1.1f, vSeed + 0.73f)
  ) - 0.5f;
  float2 local1 = uv + wobble * 0.010f;
  float2 local2 = uv + wobble * 0.002f;

  float4 fb1 = float4(1,1,1,1);
  float4 fb2 = float4(1,1,1,1);
  bool has1 = (uMaterialFlags >= 0.5f);
  bool has2 = (uMaterialFlags >= 2.5f);
  if (has1) {
    fb1 = sampleAtlasCombined(float4(uMaterialRect0U, uMaterialRect0V, uMaterialRect0W, uMaterialRect0H),
                              float2(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows),
                              uMaterialFlipbook0Frames, uMaterialFlipbook0Fps, local1, vSeed, t);
    if (has2) {
      fb2 = sampleAtlasCombined(float4(uMaterialRect1U, uMaterialRect1V, uMaterialRect1W, uMaterialRect1H),
                                float2(uMaterialFlipbook1Cols, uMaterialFlipbook1Rows),
                                uMaterialFlipbook1Frames, uMaterialFlipbook1Fps, local2, vSeed, t);
    } else {
      fb2 = fb1;
    }
  }

  float fb1A = saturate(fb1.a);
  float fb1Lum = saturate(dot(fb1.rgb, float3(0.3333f, 0.3333f, 0.3333f)));
  float speed = lerp(0.95f, 1.10f, hash11(vSeed * 19.31f));
  float flow = t * 1.55f * speed;
  float flowY = flow * lerp(0.75f, 1.55f, y * y);
  float width = lerp(0.30f, 0.055f, pow(y, 2.35f));
  float widthHybrid = width * 2.80f;
  float yy = (y * 2.0f - 1.0f);
  yy = yy * 1.45f + 0.38f;
  yy /= 1.12f;
  float2 p = float2(x / widthHybrid, yy) * 1.22f;
  float sway = fbm2D(float2(x * 1.7f, y * 3.8f) + float2(0.0f, -flowY * 0.65f) + vSeed * 7.0f);
  p.x += (sway - 0.5f) * 0.015f * (1.0f - y);
  float d0 = length(p);
  float2 advP = advect2D(p * float2(1.20f, 1.0f) + vSeed * 6.0f, flowY, 0.25f);
  float n = fbm2D(advP * float2(2.7f, 4.5f) + vSeed * 11.0f);
  float d = d0 + (n - 0.5f) * 0.18f * (1.0f - y);
  float core = saturate(1.0f - smoothstep(0.00f, 0.88f, d));
  float outer = saturate(1.0f - smoothstep(0.30f, 1.05f, d));
  float blobs = lickBlobs(x, y, advP, flowY, vSeed);
  float body = saturate(smoothstep(0.92f, 0.12f, d));
  float procAlpha = body * (0.60f + 0.55f * blobs);
  procAlpha *= (0.92f + 0.15f * smoothFlicker(t * 1.2f, vSeed));
  procAlpha *= bottomFade;
  procAlpha *= fade;
  procAlpha = 1.0f - exp(-procAlpha * 1.85f);
  procAlpha = clamp(procAlpha, 0.0f, 0.96f);

  float3 yellow = float3(1.70f, 1.20f, 0.28f);
  float3 red = float3(1.45f, 0.18f, 0.06f);
  float3 orange = float3(1.60f, 0.55f, 0.12f);
  float wave = 0.5f + 0.5f * sin((x * 1.8f + y * 8.5f - flowY * 4.9f) + vSeed * 7.0f);
  float kk = y * 6.0f - flowY * 0.55f;
  float seg = floor(kk);
  float segRand = hash11(seg + vSeed * 71.3f);
  float segRand2 = hash11(seg + vSeed * 19.7f + 5.0f);
  float tri1 = abs(frac((x * 0.85f + y * 1.05f - flowY * 0.18f) * 2.8f + vSeed * 7.0f) - 0.5f) * 2.0f;
  float tri2 = abs(frac((x * 1.10f - y * 0.60f - flowY * 0.14f) * 3.8f + vSeed * 3.0f) - 0.5f) * 2.0f;
  float zig = lerp(tri1, tri2, 0.50f + 0.50f * (segRand - 0.5f));
  zig = smoothstep(0.15f, 0.85f, zig);
  float warp = fbm2D(advect2D(float2(x * 0.85f, y * 1.2f) + vSeed * 6.0f, flowY, 0.22f) * float2(4.5f, 7.5f)) - 0.5f;
  float jag = 0.0f;
  jag += (segRand - 0.5f) * 0.10f;
  jag += (segRand2 - 0.5f) * 0.05f;
  jag += (zig - 0.5f) * 0.14f;
  jag += warp * 0.06f;
  jag *= (1.0f - 0.55f * smoothstep(0.65f, 1.0f, y));
  float boundary = clamp(0.34f + jag, 0.14f, 0.62f);
  float redMask = smoothstep(boundary, boundary + 0.11f, y);
  float3 procRgb = lerp(yellow, red, redMask);
  float band = smoothstep(boundary - 0.02f, boundary + 0.02f, y) *
               (1.0f - smoothstep(boundary + 0.02f, boundary + 0.10f, y));
  procRgb = lerp(procRgb, orange, 0.55f * band);
  float climb = core * (1.0f - smoothstep(0.55f, 0.95f, y)) * (0.35f + 0.65f * wave);
  procRgb = lerp(procRgb, yellow, 0.18f * climb);
  procRgb *= (1.18f + 0.35f * outer);

  float3 hybridRgb = procRgb;
  float hybridAlpha = procAlpha;
  if (has1) {
    hybridAlpha = clamp(hybridAlpha * lerp(0.55f, 1.65f, fb1A), 0.0f, 0.96f);
    hybridRgb *= lerp(0.85f, 1.25f, fb1Lum);
    hybridRgb *= lerp(float3(1.0f,1.0f,1.0f), fb1.rgb * 1.35f, 0.30f);
  }

  float3 fb2Rgb = fb2.rgb;
  float fb2Alpha = pow(saturate(fb2.a), 0.66f);
  float hot = smoothstep(0.10f, 0.55f, 1.0f - y);
  float3 tint = lerp(red, yellow, hot);
  fb2Rgb *= tint * 1.30f;
  fb2Alpha *= tightMask;
  fb2Alpha *= bottomFade;

  float hybridMaskedA = hybridAlpha * radialMaskLoose * bottomFade;
  float fb2MaskedA = fb2Alpha * radialMaskBase;
  float3 rgb = lerp(hybridRgb, fb2Rgb, 0.50f);
  float alpha = lerp(hybridMaskedA, fb2MaskedA, 0.50f);
  alpha *= fade;
  alpha = clamp(alpha + 0.10f * outer * fade, 0.0f, 0.985f);
  rgb *= 2.60f;
  float emissive = (0.85f * outer + 0.45f * core) * fade;
  rgb *= (1.0f + 2.10f * emissive);
  rgb = tonemapSoftLocal(rgb);
  if (alpha < 0.003f) discard;
  rgb *= alpha;
  return float4(rgb, alpha);
}

float3 applyWorldLitModel(PSIn i, bool isFrontFace, float3 linearColor) {
  float3 dx = ddx(i.worldPos);
  float3 dy = ddy(i.worldPos);
  float3 n = normalize(cross(dx, dy));
  if (!isFrontFace) {
    n = -n;
  }
  float3 l = normalize(float3(0.45f, 0.90f, 0.35f));
  float ndl = saturate(dot(n, l));
  float hemi = n.y * 0.5f + 0.5f;
  float ambient = lerp(0.58f, 0.92f, saturate(hemi));
  float lit = ambient * 0.45f + (0.22f + 0.78f * ndl) * 0.70f;
  return max(linearColor * lit, 0.0f.xxx);
}

float3 applyCharacterInking(PSIn i, bool isFrontFace, float3 linearColor) {
  if (uCharacterInkingEnabled < 0.5f) return linearColor;
  float3 dx = ddx(i.worldPos);
  float3 dy = ddy(i.worldPos);
  float3 n = normalize(cross(dx, dy));
  if (!isFrontFace) n = -n;
  float3 v = normalize(float3(0.0f, 7.0f, 9.0f) - i.worldPos);
  float ndv = saturate(dot(n, v));
  float edge = 1.0f - ndv;
  float fw = max(fwidth(edge), 1e-4f);
  float t0 = 0.84f;
  float t1 = 0.985f;
  float ringOuter = smoothstep(t0 - fw * 1.5f, t0 + fw * 1.5f, edge);
  float ringInner = smoothstep(t1 - fw * 1.5f, t1 + fw * 1.5f, edge);
  float outline = saturate(ringOuter - ringInner);
  return lerp(linearColor, 0.0f.xxx, outline);
}

float4 main(PSIn i, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
  if (uMaterialMode > 2.5f && uMaterialMode < 3.5f) {
    if (isFrontFace) discard;
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
  if (uMaterialMode > 0.5f && uMaterialMode < 1.5f) {
    return evalFireTailExact(i);
  }
  float4 tex = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float3 outLinear = saturate(i.col.rgb);
  if (uUseTexture > 0.5f) {
    float2 uvDx = ddx(i.uv);
    float2 uvDy = ddy(i.uv);
    tex = sampleWorldTextureWithWrap(i.uv, uvDx, uvDy);
    outLinear = srgbToLinear(saturate(tex.rgb)) * outLinear;
  }
  float outA = saturate(i.col.a * tex.a);
  if (uAlphaMode < 0.5f) {
    outA = saturate(i.col.a);
  } else if (uAlphaMode < 1.5f) {
    if (outA < saturate(uAlphaCutoff)) discard;
    outA = saturate(i.col.a);
  }
  if (uMaterialMode >= 1.5f) {
    outLinear = applyWorldLitModel(i, isFrontFace, outLinear);
  }
  float3 outSrgb = linearToSrgb(saturate(outLinear));
  return float4(outSrgb, outA);
}
)HLSL";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSource, sizeof(kVsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "vs_5_0", 0, 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for world VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", 0, 0, psBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !psBlob) {
        throw std::runtime_error("D3DCompile failed for world PS.");
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.Num32BitValues = 32;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.Num32BitValues = static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float));
    rootParams[1].Constants.ShaderRegister = 1;
    rootParams[1].Constants.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    auto makeStaticWorldSampler = [](UINT shaderRegister,
                                     D3D12_TEXTURE_ADDRESS_MODE addressU,
                                     D3D12_TEXTURE_ADDRESS_MODE addressV) {
        D3D12_STATIC_SAMPLER_DESC s{};
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = addressU;
        s.AddressV = addressV;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MipLODBias = 0.0f;
        s.MaxAnisotropy = 1;
        s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        s.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        s.MinLOD = 0.0f;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ShaderRegister = shaderRegister;
        s.RegisterSpace = 0;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return s;
    };
    std::array<D3D12_STATIC_SAMPLER_DESC, 9> worldSamplers = {
        makeStaticWorldSampler(0, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // CC
        makeStaticWorldSampler(1, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // RR
        makeStaticWorldSampler(2, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // CR
        makeStaticWorldSampler(3, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // RC
        makeStaticWorldSampler(4, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // MR
        makeStaticWorldSampler(5, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // RM
        makeStaticWorldSampler(6, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // MM
        makeStaticWorldSampler(7, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // CM
        makeStaticWorldSampler(8, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // MC
    };

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(_countof(rootParams));
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = static_cast<UINT>(worldSamplers.size());
    rsDesc.pStaticSamplers = worldSamplers.data();
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed for world pipeline.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(worldRootSignature_.ReleaseAndGetAddressOf()))) ||
        !worldRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 world pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = worldRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = TRUE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0] = rtBlend;
    pso.BlendState = blend;
    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = TRUE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthStencil.StencilEnable = FALSE;
    pso.DepthStencilState = depthStencil;

    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(worldPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC blendPso = pso;
    blendPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &blendPso,
            IID_PPV_ARGS(worldBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC additiveBlendPso = blendPso;
    additiveBlendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    additiveBlendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    additiveBlendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &additiveBlendPso,
            IID_PPV_ARGS(worldAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldAdditiveBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world additive blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC premulBlendPso = blendPso;
    premulBlendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    premulBlendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    premulBlendPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    premulBlendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    premulBlendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    premulBlendPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &premulBlendPso,
            IID_PPV_ARGS(worldPremultipliedBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldPremultipliedBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world premultiplied blend pipeline.");
    }

    constexpr std::size_t kBufferBytes = kMaxWorldVertices * sizeof(WorldVertex);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = kBufferBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &bufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world vertex buffer.");
    }

    worldVertexBufferGpuAddress_ = worldVertexBuffer_->GetGPUVirtualAddress();
    worldVertexStride_ = sizeof(WorldVertex);
    worldVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    worldVertexMappedData_ = nullptr;
    void* worldVertexMapped = nullptr;
    D3D12_RANGE worldVertexReadRange{0, 0};
    if (FAILED(worldVertexBuffer_->Map(0, &worldVertexReadRange, &worldVertexMapped)) || !worldVertexMapped) {
        throw std::runtime_error("Map failed for D3D12 world vertex buffer.");
    }
    worldVertexMappedData_ = static_cast<std::uint8_t*>(worldVertexMapped);

    const std::size_t indexBufferBytes = kMaxWorldIndices * sizeof(std::uint32_t);
    D3D12_RESOURCE_DESC indexBufferDesc = bufferDesc;
    indexBufferDesc.Width = indexBufferBytes;
    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &indexBufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldIndexBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldIndexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world index buffer.");
    }
    worldIndexBufferGpuAddress_ = worldIndexBuffer_->GetGPUVirtualAddress();
    worldIndexBufferSize_ = static_cast<UINT>(indexBufferBytes);
    worldIndexMappedData_ = nullptr;
    void* worldIndexMapped = nullptr;
    D3D12_RANGE worldIndexReadRange{0, 0};
    if (FAILED(worldIndexBuffer_->Map(0, &worldIndexReadRange, &worldIndexMapped)) || !worldIndexMapped) {
        throw std::runtime_error("Map failed for D3D12 world index buffer.");
    }
    worldIndexMappedData_ = static_cast<std::uint8_t*>(worldIndexMapped);
#endif
}

void D3D12RenderBackend::createSpritePipeline() {
#if defined(_WIN32)
    if (!device_) return;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.NumDescriptors = static_cast<UINT>(kMaxSrvDescriptors);
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap_.ReleaseAndGetAddressOf()))) ||
        !srvHeap_) {
        throw std::runtime_error("CreateDescriptorHeap (SRV) failed for D3D12 sprite pipeline.");
    }
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    nextSrvDescriptorIndex_ = 0;
    spriteTextures_.clear();
    worldTextures_.clear();

    static constexpr char kVsSource[] =
        "cbuffer VSConstants : register(b0) { float2 uSurfaceSize; };"
        "struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "VSOut main(VSIn i) {"
        "  VSOut o;"
        "  float2 ndc;"
        "  ndc.x = (i.pos.x / max(uSurfaceSize.x, 1.0f)) * 2.0f - 1.0f;"
        "  ndc.y = 1.0f - (i.pos.y / max(uSurfaceSize.y, 1.0f)) * 2.0f;"
        "  o.pos = float4(ndc, 0.0f, 1.0f);"
        "  o.uv = i.uv;"
        "  o.col = i.col;"
        "  return o;"
        "}";
    static constexpr char kPsSource[] =
        "Texture2D gTex : register(t0);"
        "SamplerState gSamp : register(s0);"
        "struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "float4 main(PSIn i) : SV_TARGET {"
        "  float4 tex = gTex.Sample(gSamp, i.uv);"
        "  return tex * i.col;"
        "}";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSource, sizeof(kVsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "vs_5_0", 0, 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for sprite VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", 0, 0, psBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !psBlob) {
        throw std::runtime_error("D3DCompile failed for sprite PS.");
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.Num32BitValues = 2;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_ANISOTROPIC;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = -0.25f;
    sampler.MaxAnisotropy = 16;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(_countof(rootParams));
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed for sprite pipeline.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(spriteRootSignature_.ReleaseAndGetAddressOf()))) ||
        !spriteRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 sprite pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = spriteRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = TRUE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0] = rtBlend;
    pso.BlendState = blend;
    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = FALSE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.StencilEnable = FALSE;
    pso.DepthStencilState = depthStencil;

    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(spritePipelineState_.ReleaseAndGetAddressOf()))) ||
        !spritePipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 sprite pipeline.");
    }

    constexpr std::size_t kBufferBytes = kMaxSpriteVertices * sizeof(SpriteVertex);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = kBufferBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &bufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(spriteVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !spriteVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 sprite vertex buffer.");
    }
    spriteVertexBufferGpuAddress_ = spriteVertexBuffer_->GetGPUVirtualAddress();
    spriteVertexStride_ = sizeof(SpriteVertex);
    spriteVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    spriteVertexMappedData_ = nullptr;
    void* spriteMapped = nullptr;
    D3D12_RANGE spriteReadRange{0, 0};
    if (FAILED(spriteVertexBuffer_->Map(0, &spriteReadRange, &spriteMapped)) || !spriteMapped) {
        throw std::runtime_error("Map failed for D3D12 sprite vertex buffer.");
    }
    spriteVertexMappedData_ = static_cast<std::uint8_t*>(spriteMapped);

    if (SpriteTexture* fallback = ensureFallbackSpriteTexture()) {
        worldFallbackTextureDescriptorIndex_ = fallback->descriptorIndex;
    } else {
        throw std::runtime_error("Failed to create fallback texture for D3D12 world pipeline.");
    }
#endif
}

