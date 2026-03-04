#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/NeutralPmrem.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/WorldPbrShaderShared.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <string>
#include <stdexcept>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

using namespace engine::render::d3d12_internal;
#endif

namespace {
std::string d3dCompileErrorMessage(ID3DBlob* errBlob) {
    if (!errBlob || !errBlob->GetBufferPointer() || errBlob->GetBufferSize() == 0) {
        return {};
    }
    const char* msg = static_cast<const char*>(errBlob->GetBufferPointer());
    return std::string(msg, msg + errBlob->GetBufferSize());
}

UINT d3dCompileFlags() {
#if defined(_DEBUG)
    // Debug builds prioritize compile speed to reduce startup stalls.
    return D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG;
#else
    return D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
}
} // namespace

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
                          "main", "vs_5_0", d3dCompileFlags(), 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for debug VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", d3dCompileFlags(), 0, psBlob.ReleaseAndGetAddressOf(),
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
    rtBlend.BlendEnable = engine::render::parity_contract::kDebugBlendEnabled ? TRUE : FALSE;
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
    raster.CullMode =
        engine::render::parity_contract::kWorldCullEnabled ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise =
        engine::render::parity_contract::kWorldFrontFaceClockwise ? FALSE : TRUE;
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
        "cbuffer VSConstants : register(b0) { float4x4 uViewProj; float4x4 uModel; float4 uSkinMeta; };"
        "cbuffer VSSkinMatrices : register(b2) { float4x4 gSkinMatrices[64]; };"
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; float3 nrm : NORMAL; float4 jnts : BLENDINDICES; float4 wgts : BLENDWEIGHT; float4 tan : TANGENT; };"
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; float3 worldPos : TEXCOORD1; float3 worldNormal : TEXCOORD2; float4 worldTangent : TEXCOORD3; };"
        "float3 applySkinningPos(VSIn i, float3 localPos) {"
        "  if (uSkinMeta.x < 0.5f) return localPos;"
        "  float4 blended = float4(0.0f, 0.0f, 0.0f, 0.0f);"
        "  int c = (int)uSkinMeta.y;"
        "  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;"
        "  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;"
        "  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;"
        "  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;"
        "  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < 64) blended += mul(gSkinMatrices[j0], float4(localPos, 1.0f)) * w0;"
        "  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < 64) blended += mul(gSkinMatrices[j1], float4(localPos, 1.0f)) * w1;"
        "  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < 64) blended += mul(gSkinMatrices[j2], float4(localPos, 1.0f)) * w2;"
        "  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < 64) blended += mul(gSkinMatrices[j3], float4(localPos, 1.0f)) * w3;"
        "  if (abs(blended.w) > 1e-5f) return blended.xyz / blended.w;"
        "  return blended.xyz;"
        "}"
        "float3 applySkinningNormal(VSIn i, float3 localNormal) {"
        "  if (uSkinMeta.x < 0.5f) return localNormal;"
        "  float3 blended = float3(0.0f, 0.0f, 0.0f);"
        "  int c = (int)uSkinMeta.y;"
        "  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;"
        "  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;"
        "  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;"
        "  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;"
        "  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < 64) blended += mul((float3x3)gSkinMatrices[j0], localNormal) * w0;"
        "  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < 64) blended += mul((float3x3)gSkinMatrices[j1], localNormal) * w1;"
        "  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < 64) blended += mul((float3x3)gSkinMatrices[j2], localNormal) * w2;"
        "  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < 64) blended += mul((float3x3)gSkinMatrices[j3], localNormal) * w3;"
        "  float len2 = dot(blended, blended);"
        "  return (len2 > 1e-8f) ? normalize(blended) : float3(0.0f, 1.0f, 0.0f);"
        "}"
        "float4 applySkinningTangent(VSIn i, float4 localTangent) {"
        "  if (uSkinMeta.x < 0.5f) return localTangent;"
        "  float3 tangent = localTangent.xyz;"
        "  float3 blended = float3(0.0f, 0.0f, 0.0f);"
        "  int c = (int)uSkinMeta.y;"
        "  int j0 = (int)round(i.jnts.x); float w0 = i.wgts.x;"
        "  int j1 = (int)round(i.jnts.y); float w1 = i.wgts.y;"
        "  int j2 = (int)round(i.jnts.z); float w2 = i.wgts.z;"
        "  int j3 = (int)round(i.jnts.w); float w3 = i.wgts.w;"
        "  if (w0 > 0.00001f && j0 >= 0 && j0 < c && j0 < 64) blended += mul((float3x3)gSkinMatrices[j0], tangent) * w0;"
        "  if (w1 > 0.00001f && j1 >= 0 && j1 < c && j1 < 64) blended += mul((float3x3)gSkinMatrices[j1], tangent) * w1;"
        "  if (w2 > 0.00001f && j2 >= 0 && j2 < c && j2 < 64) blended += mul((float3x3)gSkinMatrices[j2], tangent) * w2;"
        "  if (w3 > 0.00001f && j3 >= 0 && j3 < c && j3 < 64) blended += mul((float3x3)gSkinMatrices[j3], tangent) * w3;"
        "  float len2 = dot(blended, blended);"
        "  if (len2 > 1e-8f) blended = normalize(blended);"
        "  else blended = tangent;"
        "  return float4(blended, localTangent.w);"
        "}"
        "VSOut main(VSIn i) {"
        "  VSOut o;"
        "  float3 localPos = i.pos;"
        "  float3 localNormal = i.nrm;"
        "  float4 localTangent = i.tan;"
        "  if (uSkinMeta.x > 0.5f) {"
        "    localPos = applySkinningPos(i, localPos);"
        "    localNormal = applySkinningNormal(i, localNormal);"
        "    localTangent = applySkinningTangent(i, localTangent);"
        "  }"
        "  float4 world = mul(uModel, float4(localPos, 1.0f));"
        "  float4 clip = mul(uViewProj, world);"
        "  clip.z = clip.z * 0.5f + clip.w * 0.5f;"
        "  o.pos = clip;"
        "  o.uv = i.uv;"
        "  o.col = i.col;"
        "  o.worldPos = world.xyz;"
        "  float3x3 normalM = (float3x3)uModel;"
        "  float3 wn = mul(normalM, localNormal);"
        "  float wnLen2 = dot(wn, wn);"
        "  o.worldNormal = (wnLen2 > 1e-8f) ? normalize(wn) : float3(0.0f, 1.0f, 0.0f);"
        "  float3 wt = mul(normalM, localTangent.xyz);"
        "  float wtLen2 = dot(wt, wt);"
        "  if (wtLen2 > 1e-8f) wt = normalize(wt);"
        "  o.worldTangent = float4(wt, localTangent.w);"
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
};
Texture2D gTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gMetallicRoughnessTex : register(t2);
Texture2D gOcclusionTex : register(t3);
Texture2D gEmissiveTex : register(t4);
Texture2D gEnvTex : register(t5);
SamplerState gSampCC : register(s0);
SamplerState gSampRR : register(s1);
SamplerState gSampCR : register(s2);
SamplerState gSampRC : register(s3);
SamplerState gSampMR : register(s4);
SamplerState gSampRM : register(s5);
SamplerState gSampMM : register(s6);
SamplerState gSampCM : register(s7);
SamplerState gSampMC : register(s8);
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; float3 worldPos : TEXCOORD1; float3 worldNormal : TEXCOORD2; float4 worldTangent : TEXCOORD3; };

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
float4 sampleTextureWithWrap(Texture2D tex,
                             float2 uv,
                             float2 uvDx,
                             float2 uvDy,
                             float wrapS,
                             float wrapT) {
  bool sClamp = isClampWrap(wrapS);
  bool tClamp = isClampWrap(wrapT);
  bool sMirror = isMirrorWrap(wrapS);
  bool tMirror = isMirrorWrap(wrapT);

  if (sClamp && tClamp) return tex.SampleGrad(gSampCC, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && !tClamp && !tMirror) return tex.SampleGrad(gSampRR, uv, uvDx, uvDy);
  if (sClamp && !tClamp && !tMirror) return tex.SampleGrad(gSampCR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tClamp) return tex.SampleGrad(gSampRC, uv, uvDx, uvDy);
  if (sMirror && !tClamp && !tMirror) return tex.SampleGrad(gSampMR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tMirror) return tex.SampleGrad(gSampRM, uv, uvDx, uvDy);
  if (sMirror && tMirror) return tex.SampleGrad(gSampMM, uv, uvDx, uvDy);
  if (sClamp && tMirror) return tex.SampleGrad(gSampCM, uv, uvDx, uvDy);
  if (sMirror && tClamp) return tex.SampleGrad(gSampMC, uv, uvDx, uvDy);
  return tex.SampleGrad(gSampRR, uv, uvDx, uvDy);
}
float4 sampleWorldTextureWithWrap(float2 uv, float2 uvDx, float2 uvDy) {
  return sampleTextureWithWrap(gTex, uv, uvDx, uvDy, uWrapS, uWrapT);
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
)HLSL"
R"HLSL(

float3 safeNormalize(float3 value, float3 fallback) {
  float len2 = dot(value, value);
  if (len2 < 1e-8f) return fallback;
  return value * rsqrt(len2);
}

__PAC_SHARED_WORLD_PBR_SECTION__

float3 perturbNormal2Arb(float3 eyePos, float3 surfNorm, float3 mapN, float2 uv, float faceDirection) {
  float3 q0 = ddx(eyePos.xyz);
  float3 q1 = ddy(eyePos.xyz);
  float2 st0 = ddx(uv);
  float2 st1 = ddy(uv);

  float3 N = surfNorm;
  float3 q1perp = cross(q1, N);
  float3 q0perp = cross(N, q0);
  float3 T = q1perp * st0.x + q0perp * st1.x;
  float3 B = q1perp * st0.y + q0perp * st1.y;

  float det = max(dot(T, T), dot(B, B));
  float scale = (det <= 1e-10f) ? 0.0f : faceDirection * rsqrt(det);
  return normalize(T * (mapN.x * scale) + B * (mapN.y * scale) + N * mapN.z);
}
float3 computeMappedNormal(PSIn i,
                           bool isFrontFace,
                           float2 sampleUv,
                           float2 uvDx,
                           float2 uvDy,
                           bool useNormalTexture,
                           float normalScale) {
  float faceDirection = isFrontFace ? 1.0f : -1.0f;
  float3 n = normalize(i.worldNormal);
  if (dot(n, n) < 1e-6f) {
    float3 dx = ddx(i.worldPos);
    float3 dy = ddy(i.worldPos);
    n = normalize(cross(dx, dy));
  }
  n *= faceDirection;
  if (!useNormalTexture) return n;

  float3 normalTexel = sampleTextureWithWrap(gNormalTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).xyz;
  float2 mapXY = normalTexel.xy * 2.0f - 1.0f;
  mapXY *= max(normalScale, 0.0f) * 1.25f;
  // Support standard RGB tangent-space normals and packed-XY normals (blue=0).
  float authoredZ = normalTexel.z * 2.0f - 1.0f;
  float reconZ = sqrt(max(1.0f - saturate(dot(mapXY, mapXY)), 0.0f));
  float useReconstructedZ = (normalTexel.z <= (1.5f / 255.0f)) ? 1.0f : 0.0f;
  float mapZ = lerp(authoredZ, reconZ, useReconstructedZ);
  float3 mapN = normalize(float3(mapXY, mapZ));
  float3 mapped = float3(0.0f, 0.0f, 0.0f);
  float3 tangent = i.worldTangent.xyz;
  float tangentLen2 = dot(tangent, tangent);
  bool hasAuthoredTangent = tangentLen2 > 1e-6f && abs(i.worldTangent.w) > 0.5f;
  if (hasAuthoredTangent) {
    tangent *= rsqrt(tangentLen2);
    tangent = tangent - n * dot(n, tangent);
    float orthoLen2 = dot(tangent, tangent);
    if (orthoLen2 > 1e-10f) {
      tangent *= rsqrt(orthoLen2);
      float tangentSign = (i.worldTangent.w < 0.0f) ? -1.0f : 1.0f;
      float3 bitangent = normalize(cross(n, tangent)) * tangentSign;
      if (!isFrontFace) {
        tangent = -tangent;
        bitangent = -bitangent;
      }
      mapped = normalize(tangent * mapN.x + bitangent * mapN.y + n * mapN.z);
    } else {
      hasAuthoredTangent = false;
    }
  }
  if (!hasAuthoredTangent) {
    mapped = perturbNormal2Arb(i.worldPos, n, mapN, sampleUv, faceDirection);
  }
  return mapped;
}

float3 applyWorldLitModel(PSIn i,
                          bool isFrontFace,
                          float3 linearColor,
                          float2 sampleUv,
                          float2 uvDx,
                          float2 uvDy,
                          bool useNormalTexture,
                          bool useMetallicRoughnessTexture,
                          bool useOcclusionTexture,
                          bool useEmissiveTexture,
                          float normalScale,
                          float metallicFactor,
                          float roughnessFactor,
                          float occlusionStrength,
                          float3 emissiveFactor,
                          float3 cameraPos,
                          float3 cameraForwardPacked,
                          float3 cameraTarget) {
  float3 n = computeMappedNormal(i, isFrontFace, sampleUv, uvDx, uvDy, useNormalTexture, normalScale);
  float3 orm = float3(1.0f, 1.0f, 1.0f);
  if (useMetallicRoughnessTexture) {
    orm = sampleTextureWithWrap(
              gMetallicRoughnessTex,
              sampleUv,
              uvDx,
              uvDy,
              uWrapS,
              uWrapT).rgb;
  }
  float roughness = clamp(orm.g * saturate(roughnessFactor), 0.16f, 1.0f);
  float metallic = clamp(orm.b * saturate(metallicFactor), 0.0f, 1.0f);
  float ao = 1.0f;
  if (useOcclusionTexture) {
    float occTex = sampleTextureWithWrap(gOcclusionTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).r;
    ao = lerp(1.0f, occTex, saturate(occlusionStrength));
  }

  float3 albedo = saturate(linearColor);
  float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
  float3 diffuseColor = albedo * (1.0f - metallic);
  const float specularF90 = 1.0f;
  float3 camForward = safeNormalize(cameraForwardPacked, normalize(float3(0.0f, -0.6139406f, -0.7893522f)));
  float3 camRight = cross(camForward, float3(0.0f, 1.0f, 0.0f));
  if (dot(camRight, camRight) < 1e-6f) {
    camRight = cross(camForward, float3(0.0f, 0.0f, 1.0f));
  }
  camRight = safeNormalize(camRight, float3(1.0f, 0.0f, 0.0f));
  float3 camUp = safeNormalize(cross(camRight, camForward), float3(0.0f, 1.0f, 0.0f));
  float3 v = safeNormalize(cameraPos - i.worldPos, -camForward);
  const float3 directColor = float3(1.0f, 1.0f, 1.0f);
  const float directIntensity = __PAC_PBR_DIRECT_INTENSITY__ * 3.14159265f;
  const float3 ambientColor = float3(1.0f, 1.0f, 1.0f);
  const float ambientIntensity = __PAC_PBR_AMBIENT_INTENSITY__;

  float3 lightPos = cameraPos + camRight * 0.5f + camUp * 0.0f - camForward * 0.8660254f;
  float3 l0 = safeNormalize(lightPos - cameraTarget, float3(0.45f, 0.86f, 0.24f));
  float3 direct = evalDirectPbr(n, v, l0, directColor * directIntensity, albedo, F0, roughness, metallic);

  float NdotV = max(dot(n, v), 0.0f);
  float3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
  float3 kS = F;
  float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);

  float3 r = reflect(-v, n);
  float3 envIrradiance = 3.14159265f * sampleNeutralEnvironment(n, 1.0f);
  float3 envRadiance = sampleNeutralEnvironment(r, roughness);
  float3 singleScattering = float3(0.0f, 0.0f, 0.0f);
  float3 multiScattering = float3(0.0f, 0.0f, 0.0f);
  computeMultiscattering(n, v, F0, specularF90, roughness, singleScattering, multiScattering);
  float3 cosineWeightedIrradiance = envIrradiance * (1.0f / 3.14159265f);
  float3 totalScattering = singleScattering + multiScattering;
  float energyComp = 1.0f - max(max(totalScattering.r, totalScattering.g), totalScattering.b);
  float3 diffuseIBL = diffuseColor * max(energyComp, 0.0f) * cosineWeightedIrradiance;
  float3 specularIBL = envRadiance * singleScattering + multiScattering * cosineWeightedIrradiance;
  diffuseIBL *= __PAC_PBR_DIFFUSE_IBL_SCALE__;
  specularIBL *= __PAC_PBR_SPECULAR_IBL_SCALE__;
  diffuseIBL *= ao;
  float specularOcclusion = computeSpecularOcclusion(NdotV, ao, roughness);
  specularIBL *= specularOcclusion;
  float3 ibl = diffuseIBL + specularIBL;

  float3 ambientLight = kD * albedo * ambientColor * ambientIntensity;
  float3 shaded = direct + ibl + ambientLight;

  float3 emissiveTex = useEmissiveTexture
      ? saturate(sampleTextureWithWrap(gEmissiveTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).rgb)
      : float3(1.0f, 1.0f, 1.0f);
  float3 emissive = emissiveTex * max(emissiveFactor, float3(0.0f, 0.0f, 0.0f));
  return max(shaded + emissive, float3(0.0f, 0.0f, 0.0f));
}

float3 applyCharacterInking(PSIn i, float3 linearColor, float3 n, float3 cameraPos, float3 cameraForwardPacked) {
  return linearColor;
}
)HLSL"
R"HLSL(

float4 main(PSIn i, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {
  if (uMaterialMode > 2.5f && uMaterialMode < 3.5f) {
    if (!isFrontFace) discard;
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
  if (uMaterialMode > 0.5f && uMaterialMode < 1.5f) {
    return evalFireTailExact(i);
  }
  float4 tex = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float3 outLinear = saturate(i.col.rgb);
  float2 wrappedUv = float2(applyWrap(i.uv.x, uWrapS), applyWrap(i.uv.y, uWrapT));
  bool clampS = isClampWrap(uWrapS);
  bool clampT = isClampWrap(uWrapT);
  if (clampS || clampT) {
    wrappedUv = clampWrappedUvToTexelCenter(wrappedUv);
  }
  float2 uvDx = ddx(wrappedUv);
  float2 uvDy = ddy(wrappedUv);
  if (uUseTexture > 0.5f) {
    tex = sampleWorldTextureWithWrap(wrappedUv, uvDx, uvDy);
    outLinear = saturate(tex.rgb) * outLinear;
  }
  float outA = saturate(i.col.a * tex.a);
  if (uAlphaMode < 0.5f) {
    outA = saturate(i.col.a);
  } else if (uAlphaMode < 1.5f) {
    if (outA < saturate(uAlphaCutoff)) discard;
    outA = saturate(i.col.a);
  }
  const float pbrDebugView = uMaterialFlipbook1Fps;
  if (uMaterialMode >= 1.5f && pbrDebugView > 0.5f) {
    float3 dbg = float3(0.0f, 0.0f, 0.0f);
    if (pbrDebugView < 1.5f) {
      // 1: Base/albedo sample.
      dbg = saturate(tex.rgb);
    } else if (pbrDebugView < 2.5f) {
      // 2: Normal map sample.
      dbg = sampleTextureWithWrap(gNormalTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).rgb;
    } else if (pbrDebugView < 3.5f) {
      // 3: Roughness channel.
      const float rgh =
          sampleTextureWithWrap(gMetallicRoughnessTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).g;
      dbg = float3(rgh, rgh, rgh);
    } else if (pbrDebugView < 4.5f) {
      // 4: Metallic channel.
      const float met =
          sampleTextureWithWrap(gMetallicRoughnessTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).b;
      dbg = float3(met, met, met);
    } else if (pbrDebugView < 5.5f) {
      // 5: AO channel.
      const float ao =
          sampleTextureWithWrap(gOcclusionTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).r;
      dbg = float3(ao, ao, ao);
    }
    return float4(linearToSrgb(saturate(dbg)), 1.0f);
  }
  if (uMaterialMode >= 1.5f) {
    const int pbrFlags = (int)(uMaterialFlags + 0.5f);
    const bool useNormalTexture = (pbrFlags & (1 << 0)) != 0;
    const bool useMetallicRoughnessTexture = (pbrFlags & (1 << 1)) != 0;
    const bool useOcclusionTexture = (pbrFlags & (1 << 2)) != 0;
    const bool useEmissiveTexture = (pbrFlags & (1 << 3)) != 0;
    const float normalScale = max(uMaterialAtlasWidth, 0.0f);
    const float metallicFactor = saturate(uMaterialAtlasHeight);
    const float roughnessFactor = saturate(uMaterialRect0U);
    const float occlusionStrength = saturate(uMaterialRect0V);
    const float3 emissiveFactor =
        max(float3(uMaterialRect0W, uMaterialRect0H, uMaterialRect1U), float3(0.0f, 0.0f, 0.0f));
    const float3 cameraPos = float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
    const float3 cameraForward = float3(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows, uMaterialFlipbook0Frames);
    const float3 cameraTarget = float3(uMaterialFlipbook0Fps, uMaterialFlipbook1Cols, uMaterialFlipbook1Rows);
    outLinear = applyWorldLitModel(i,
                                   isFrontFace,
                                   outLinear,
                                   wrappedUv,
                                   uvDx,
                                   uvDy,
                                   useNormalTexture,
                                   useMetallicRoughnessTexture,
                                   useOcclusionTexture,
                                   useEmissiveTexture,
                                   normalScale,
                                   metallicFactor,
                                   roughnessFactor,
                                   occlusionStrength,
                                   emissiveFactor,
                                   cameraPos,
                                   cameraForward,
                                   cameraTarget);
  }
  const float toneMappingExposure = __PAC_PBR_TONEMAP_EXPOSURE__;
  const float toneMappingMode = 1.0f;
  float3 mapped = applyViewerToneMapping(
      max(outLinear, float3(0.0f, 0.0f, 0.0f)),
      toneMappingMode,
      toneMappingExposure);
  float3 outSrgb = linearToSrgb(mapped);
  return float4(outSrgb, outA);
}
)HLSL";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSource, sizeof(kVsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "vs_5_0", d3dCompileFlags(), 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for world VS.");
    }
    const std::string worldPsSource =
        engine::render::world_pbr_shader_shared::injectSharedWorldPbr(
            kPsSource, engine::render::world_pbr_shader_shared::ShaderLanguage::Hlsl);
    errBlob.Reset();
    if (FAILED(D3DCompile(worldPsSource.c_str(), worldPsSource.size(), nullptr, nullptr, nullptr,
                          "main", "ps_5_0", d3dCompileFlags(), 0, psBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !psBlob) {
        const std::string details = d3dCompileErrorMessage(errBlob.Get());
        if (!details.empty()) {
            throw std::runtime_error(std::string("D3DCompile failed for world PS: ") + details);
        }
        throw std::runtime_error("D3DCompile failed for world PS.");
    }

    std::array<D3D12_DESCRIPTOR_RANGE, 6> srvRanges{};
    for (UINT i = 0; i < static_cast<UINT>(srvRanges.size()); ++i) {
        srvRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[i].NumDescriptors = 1;
        srvRanges[i].BaseShaderRegister = i;
        srvRanges[i].RegisterSpace = 0;
        srvRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER rootParams[9]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.Num32BitValues = static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float));
    rootParams[1].Constants.ShaderRegister = 1;
    rootParams[1].Constants.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 2;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    for (UINT i = 0; i < static_cast<UINT>(srvRanges.size()); ++i) {
        const UINT rootIndex = 3u + i;
        rootParams[rootIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[rootIndex].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[rootIndex].DescriptorTable.pDescriptorRanges = &srvRanges[i];
        rootParams[rootIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    auto makeStaticWorldSampler = [](UINT shaderRegister,
                                     D3D12_TEXTURE_ADDRESS_MODE addressU,
                                     D3D12_TEXTURE_ADDRESS_MODE addressV) {
        D3D12_STATIC_SAMPLER_DESC s{};
        // Match OpenGL world texture quality closer: anisotropic + slight negative LOD bias.
        s.Filter = D3D12_FILTER_ANISOTROPIC;
        s.AddressU = addressU;
        s.AddressV = addressV;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MipLODBias = -0.35f;
        s.MaxAnisotropy = 16;
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
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = worldRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blendOpaque{};
    blendOpaque.AlphaToCoverageEnable = FALSE;
    blendOpaque.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtOpaque{};
    rtOpaque.BlendEnable = engine::render::parity_contract::kWorldOpaqueBlendEnabled ? TRUE : FALSE;
    rtOpaque.LogicOpEnable = FALSE;
    rtOpaque.SrcBlend = D3D12_BLEND_ONE;
    rtOpaque.DestBlend = D3D12_BLEND_ZERO;
    rtOpaque.BlendOp = D3D12_BLEND_OP_ADD;
    rtOpaque.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtOpaque.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtOpaque.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtOpaque.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtOpaque.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendOpaque.RenderTarget[0] = rtOpaque;
    pso.BlendState = blendOpaque;
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
    depthStencil.DepthFunc = engine::render::parity_contract::kWorldDepthFuncLessEqual
        ? D3D12_COMPARISON_FUNC_LESS_EQUAL
        : D3D12_COMPARISON_FUNC_LESS;
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
    blendPso.BlendState.RenderTarget[0].BlendEnable =
        engine::render::parity_contract::kWorldBlendPipelineEnabled ? TRUE : FALSE;
    blendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
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

    // Per-draw VS constant upload ring buffer (view-proj + model + skin meta).
    const std::size_t kWorldVsConstantsBytesPerDraw = alignUp(36u * sizeof(float), 256u);
    const std::size_t kMaxWorldDrawsPerFrame = 4096u;
    const std::size_t kWorldVsConstantsBufferBytes =
        kWorldVsConstantsBytesPerDraw * kMaxWorldDrawsPerFrame;
    D3D12_RESOURCE_DESC worldVsConstantsDesc = bufferDesc;
    worldVsConstantsDesc.Width = kWorldVsConstantsBufferBytes;
    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &worldVsConstantsDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldVsConstantBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldVsConstantBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world VS constants buffer.");
    }
    worldVsConstantBufferGpuAddress_ = worldVsConstantBuffer_->GetGPUVirtualAddress();
    worldVsConstantBufferSize_ = static_cast<UINT>(kWorldVsConstantsBufferBytes);
    worldVsConstantMappedData_ = nullptr;
    void* worldVsMapped = nullptr;
    D3D12_RANGE worldVsReadRange{0, 0};
    if (FAILED(worldVsConstantBuffer_->Map(0, &worldVsReadRange, &worldVsMapped)) || !worldVsMapped) {
        throw std::runtime_error("Map failed for D3D12 world VS constants buffer.");
    }
    worldVsConstantMappedData_ = static_cast<std::uint8_t*>(worldVsMapped);
    std::memset(worldVsConstantMappedData_, 0, worldVsConstantBufferSize_);
    worldVsConstantFrameOffset_ = 0u;

    // Per-draw GPU clip-skinning matrix upload ring buffer.
    const std::size_t kMaxGpuSkinMatrices = 64u;
    const std::size_t kSkinMatrixBytesPerDraw = alignUp(
        kMaxGpuSkinMatrices * 16u * sizeof(float), 256u);
    const std::size_t kSkinMatrixBufferBytes =
        kSkinMatrixBytesPerDraw * kMaxWorldDrawsPerFrame;
    D3D12_RESOURCE_DESC skinMatrixBufferDesc = bufferDesc;
    skinMatrixBufferDesc.Width = kSkinMatrixBufferBytes;
    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &skinMatrixBufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldSkinMatrixBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldSkinMatrixBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world skin-matrix buffer.");
    }
    worldSkinMatrixBufferGpuAddress_ = worldSkinMatrixBuffer_->GetGPUVirtualAddress();
    worldSkinMatrixBufferSize_ = static_cast<UINT>(kSkinMatrixBufferBytes);
    worldSkinMatrixMappedData_ = nullptr;
    void* worldSkinMapped = nullptr;
    D3D12_RANGE worldSkinReadRange{0, 0};
    if (FAILED(worldSkinMatrixBuffer_->Map(0, &worldSkinReadRange, &worldSkinMapped)) || !worldSkinMapped) {
        throw std::runtime_error("Map failed for D3D12 world skin-matrix buffer.");
    }
    worldSkinMatrixMappedData_ = static_cast<std::uint8_t*>(worldSkinMapped);
    std::memset(worldSkinMatrixMappedData_, 0, worldSkinMatrixBufferSize_);
    // Reserve identity at offset 0 for non-skinned draws.
    constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(worldSkinMatrixMappedData_, kIdentity, sizeof(kIdentity));
    worldSkinMatrixFrameOffset_ = 256u;
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
        "float3 linearToSrgb(float3 c) {"
        "  c = max(c, float3(0.0f, 0.0f, 0.0f));"
        "  float3 lo = c * 12.92f;"
        "  float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;"
        "  return lerp(lo, hi, step(float3(0.0031308f, 0.0031308f, 0.0031308f), c));"
        "}"
        "float4 main(PSIn i) : SV_TARGET {"
        "  float4 tex = gTex.Sample(gSamp, i.uv);"
        "  float3 rgb = linearToSrgb(tex.rgb * saturate(i.col.rgb));"
        "  float a = saturate(tex.a * i.col.a);"
        "  return float4(rgb, a);"
        "}";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSource, sizeof(kVsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "vs_5_0", d3dCompileFlags(), 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for sprite VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", d3dCompileFlags(), 0, psBlob.ReleaseAndGetAddressOf(),
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
    sampler.MipLODBias = -0.35f;
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

    static const unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};
    static const unsigned char kFallbackFlatNormalRgba[4] = {128u, 128u, 255u, 255u};
    SpriteTexture* fallbackBase = ensureWorldTextureRaw(
        "__world_fallback_white_base_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        kGlClampToEdge,
        kGlClampToEdge,
        /*srgb=*/true);
    SpriteTexture* fallbackNormal = ensureWorldTextureRaw(
        "__world_fallback_flat_normal_1x1__",
        kFallbackFlatNormalRgba,
        1,
        1,
        kGlClampToEdge,
        kGlClampToEdge,
        /*srgb=*/false);
    SpriteTexture* fallbackLinear = ensureWorldTextureRaw(
        "__world_fallback_white_linear_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        kGlClampToEdge,
        kGlClampToEdge,
        /*srgb=*/false);
    SpriteTexture* fallbackEmissive = ensureWorldTextureRaw(
        "__world_fallback_white_emissive_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        kGlClampToEdge,
        kGlClampToEdge,
        /*srgb=*/true);
    if (!fallbackBase || !fallbackNormal || !fallbackLinear || !fallbackEmissive) {
        throw std::runtime_error("Failed to create fallback textures for D3D12 world pipeline.");
    }
    worldFallbackTextureDescriptorIndex_ = fallbackBase->descriptorIndex;
    worldFallbackNormalTextureDescriptorIndex_ = fallbackNormal->descriptorIndex;
    worldFallbackMetallicRoughnessTextureDescriptorIndex_ = fallbackLinear->descriptorIndex;
    worldFallbackOcclusionTextureDescriptorIndex_ = fallbackLinear->descriptorIndex;
    worldFallbackEmissiveTextureDescriptorIndex_ = fallbackEmissive->descriptorIndex;
    // Keep startup fast: use a cheap 1x1 linear fallback at boot and lazily promote to PMREM.
    worldFallbackEnvTextureDescriptorIndex_ = fallbackLinear->descriptorIndex;
    worldFallbackEnvTextureReady_ = false;
#endif
}

void D3D12RenderBackend::ensureWorldFallbackEnvTexture() {
#if defined(_WIN32)
    if (worldFallbackEnvTextureReady_) return;

    const auto& neutralPmremAtlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    SpriteTexture* fallbackEnv = !neutralPmremAtlas.rgba16f.empty()
        ? ensureWorldTextureRawHalfFloat(
            "__neutral_room_pmrem_rgba16f_v1__",
            neutralPmremAtlas.rgba16f.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            kGlClampToEdge,
            kGlClampToEdge)
        : ensureWorldTextureRaw(
            "__neutral_room_pmrem_rgbm_v1__",
            neutralPmremAtlas.rgba.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            kGlClampToEdge,
            kGlClampToEdge,
            /*srgb=*/false);
    if (!fallbackEnv || !fallbackEnv->valid) return;

    worldFallbackEnvTextureDescriptorIndex_ = fallbackEnv->descriptorIndex;
    worldFallbackEnvTextureReady_ = true;
#endif
}


