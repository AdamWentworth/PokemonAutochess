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
float litTextureDetailLodBias() {
  const bool qualityControlled =
      (uMaterialMode > 1.5f && uMaterialMode < 2.5f) ||
      (uMaterialMode > 27.5f && uMaterialMode < 30.5f) ||
      (uMaterialMode > 31.5f && uMaterialMode < 35.5f);
  if (!qualityControlled) return 0.0f;
  if (uMaterialMode > 31.5f && uMaterialMode < 32.5f) {
    float profileField = floor(max(uMaterialFlipbook1Fps, 0.0f) / 1000.0f);
    float packedRemainder = max(uMaterialFlipbook1Fps, 0.0f) -
        profileField * 1000.0f;
    return clamp(floor(packedRemainder) * 0.01f - 1.0f, -0.75f, 1.25f);
  }
  if (uMaterialMode > 34.5f && uMaterialMode < 35.5f) {
    return clamp(uProjectedShadowRowZ.z, -0.75f, 1.25f);
  }
  return clamp(uMaterialFlipbook1Frames, -0.75f, 1.25f);
}
float4 sampleTextureWithWrap(Texture2D tex,
                             float2 uv,
                             float2 uvDx,
                             float2 uvDy,
                             float wrapS,
                             float wrapT) {
  const float lodScale = exp2(litTextureDetailLodBias());
  uvDx *= lodScale;
  uvDy *= lodScale;
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
float3 rgbToHsv(float3 color) {
  float4 k = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
  float4 p = lerp(
      float4(color.bg, k.wz),
      float4(color.gb, k.xy),
      step(color.b, color.g));
  float4 q = lerp(
      float4(p.xyw, color.r),
      float4(color.r, p.yzx),
      step(p.x, color.r));
  float chroma = q.x - min(q.w, q.y);
  const float epsilon = 1.0e-10f;
  return float3(
      abs(q.z + (q.w - q.y) / (6.0f * chroma + epsilon)),
      chroma / (q.x + epsilon),
      q.x);
}

float3 hsvToRgb(float3 hsv) {
  float3 p = abs(
      frac(hsv.xxx + float3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) *
          6.0f -
      3.0f);
  return hsv.z *
      lerp(1.0f.xxx, saturate(p - 1.0f), hsv.y);
}

__AUTOCHESS_FIELD_DECLARATIONS__
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

float3 encodeWorldSurfaceColor(float3 linearColor) {
  // The source writes linear color to UNORM before its dedicated
  // gamma_correction shader applies the standard sRGB transfer.
  return linearToSrgb(saturate(linearColor));
}

float3 resolveWorldSceneColor(float3 linearColor) {
  float3 clamped = saturate(linearColor);
  return (uSceneColorPostEnabled > 0.5f)
      ? clamped
      : encodeWorldSurfaceColor(clamped);
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

float4 sampleAtlasCombined(float4 rectUv, float2 grid, float frames, float fps, float2 localUV01, float seed, float t, bool coherent) {
  float speed = coherent ? 1.0f : lerp(0.85f, 1.10f, hash11(seed * 31.7f + 2.3f));
  float phase = coherent ? 0.0f : (seed * frames);
  float f = floor(t * fps * speed + phase);
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

float4 sampleFireDirect0(float2 uvLocal, float seed, float t) {
  return sampleAtlasCombined(float4(uMaterialRect0U, uMaterialRect0V, uMaterialRect0W, uMaterialRect0H),
                             float2(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows),
                             uMaterialFlipbook0Frames, uMaterialFlipbook0Fps, uvLocal, seed, t, true);
}

float4 sampleAtlasCombinedTopLeft(float4 rectUv, float2 grid, float frames, float fps, float2 localUV01, float t) {
  float f = floor(t * fps);
  float frame = fmod(f, max(1.0f, frames));
  if (frame < 0.0f) frame += max(1.0f, frames);
  float cols = max(1.0f, grid.x);
  float rows = max(1.0f, grid.y);
  float col = fmod(frame, cols);
  float row = floor(frame / cols);
  float2 cellUVLocal = (float2(col, row) + localUV01) / float2(cols, rows);
  float2 cellUv = clampUvToRegionPixels(cellUVLocal, rectUv);
  return gTex.Sample(gSampCC, cellUv);
}


float hash41(float4 p) {
  return frac(sin(dot(p, float4(127.1f, 311.7f, 74.7f, 269.5f))) * 43758.5453123f);
}

float valueNoise4D(float4 p) {
  float4 i = floor(p);
  float4 f = frac(p);
  float4 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
  float accum = 0.0f;
  [unroll]
  for (int dw = 0; dw < 2; ++dw) {
    [unroll]
    for (int dz = 0; dz < 2; ++dz) {
      [unroll]
      for (int dy = 0; dy < 2; ++dy) {
        [unroll]
        for (int dx = 0; dx < 2; ++dx) {
          float4 corner = float4((float)dx, (float)dy, (float)dz, (float)dw);
          float wx = lerp(1.0f - u.x, u.x, corner.x);
          float wy = lerp(1.0f - u.y, u.y, corner.y);
          float wz = lerp(1.0f - u.z, u.z, corner.z);
          float ww = lerp(1.0f - u.w, u.w, corner.w);
          accum += hash41(i + corner) * wx * wy * wz * ww;
        }
      }
    }
  }
  return accum;
}

float authoredFireNoise(float4 p) {
  float value = 0.0f;
  float amplitude = 1.0f;
  float amplitudeSum = 0.0f;
  [unroll]
  for (int octave = 0; octave < 2; ++octave) {
    value += amplitude * valueNoise4D(p);
    amplitudeSum += amplitude;
    p *= 2.0f;
    amplitude *= 0.5f;
  }
  return value / max(amplitudeSum, 1e-5f);
}

float2 nativeLayeredMaterialUv(PSIn i) {
  bool exactSourceTrack = uMaterialFlags > 1.5f;
  float baseScrollHz = max(uMaterialRect0W, 0.0f);
  float2 baseOffset = exactSourceTrack
      ? float2(uMaterialRect0W, uMaterialRect0H)
      : float2(
          baseScrollHz > 0.0f
              ? 1.0f - frac(uMaterialTimeSec * baseScrollHz)
              : 0.0f,
          0.0f);
  float2 baseScale = exactSourceTrack
      ? float2(uMaterialFlipbook0Fps, uMaterialFlipbook1Fps)
      : float2(1.0f, 1.0f);
  float2 baseUv = float2(
      (i.uv.x - baseOffset.x) * baseScale.x,
      1.0f - ((1.0f - i.uv.y) - baseOffset.y) * baseScale.y);
  baseUv.x = frac(baseUv.x);
  return baseUv;
}

float4 evalNativeLayeredUnlitDisplaced(PSIn i) {
  float emissionIntensity = max(uMaterialRect0V, 0.0f);
  float2 baseUv = nativeLayeredMaterialUv(i);
  float4 base = gTex.Sample(gSampCC, baseUv);
  float4 weights = saturate(gMetallicRoughnessTex.Sample(gSampCC, baseUv));
  float coverage = saturate(
      1.0f - dot(weights, float4(1.0f, 1.0f, 1.0f, 1.0f)));
  bool layeredCharacterCharacterComposite = uMaterialFlags > 3.3f;
  float3 color = layeredCharacterCharacterComposite
      ? base.rgb
      : base.rgb * coverage;
  float3 layer1 = float3(
      uMaterialFlipbook0Cols,
      uMaterialFlipbook0Rows,
      uMaterialFlipbook0Frames);
  float3 layer2 = float3(
      uMaterialFlipbook1Cols,
      uMaterialFlipbook1Rows,
      uMaterialFlipbook1Frames);
  // Scarlet's Unlit variation 48 applies every material-layer color as a
  // multiplier of the shared base-color map, then composites each mask as a
  // successive alpha-over operation.  Keeping that multiplication matters
  // for the general native contract even though Charmander's fire base map is
  // white.
  if (!layeredCharacterCharacterComposite) {
    color = lerp(color, base.rgb * layer1, weights.r);
    coverage += weights.r * (1.0f - coverage);
    color = lerp(color, base.rgb * layer2, weights.g);
    coverage += weights.g * (1.0f - coverage);
    color = lerp(color, base.rgb, weights.b);
    coverage += weights.b * (1.0f - coverage);
    color = lerp(color, base.rgb, weights.a);
    coverage += weights.a * (1.0f - coverage);
  } else {
    coverage = 1.0f;
  }
  float4 surface = float4(
      color / max(coverage, 1e-6f) * emissionIntensity,
      1.0f);
  // SSSEffect subtype 3 uses complete dynamic alpha. Gastly's 3.25 and
  // 3.375 subtypes remain opaque because both source meshes author zero alpha.
  // World-scene draws carry dynamic alpha in i.col; direct indexed draws
  // carry it in uVertexColorMulA. Authored Unlit fire remains opaque.
  surface.a = uMaterialFlags > 2.5f && uMaterialFlags < 3.125f
      ? saturate(i.col.a * uVertexColorMulA)
      : 1.0f;
  return surface;
}

float3 applyVolumeRimLighting(
    PSIn i,
    float3 color,
    float4 authoredResponse,
    bool useAuthoredShadowColor) {
  float3 normal = normalize(i.worldNormal);
  float3 cameraPos = uProjectedShadowRowX.xyz;
  float3 cameraForwardPacked = uProjectedShadowRowY.xyz;
  float cameraForwardLengthSquared = dot(
      cameraForwardPacked,
      cameraForwardPacked);
  float3 cameraForward = cameraForwardLengthSquared > 1e-10f
      ? cameraForwardPacked * rsqrt(cameraForwardLengthSquared)
      : float3(0.0f, -0.6139406f, -0.7893522f);
  float3 cameraRightPacked = cross(
      cameraForward,
      float3(0.0f, 1.0f, 0.0f));
  float cameraRightLengthSquared = dot(
      cameraRightPacked,
      cameraRightPacked);
  float3 cameraRight = cameraRightLengthSquared > 1e-10f
      ? cameraRightPacked * rsqrt(cameraRightLengthSquared)
      : float3(1.0f, 0.0f, 0.0f);
  float3 toCamera = cameraPos - i.worldPos;
  float toCameraLengthSquared = dot(toCamera, toCamera);
  float3 viewDirection = toCameraLengthSquared > 1e-10f
      ? toCamera * rsqrt(toCameraLengthSquared)
      : -cameraForward;
  float3 lightPosition = cameraPos +
      cameraRight * 0.5f - cameraForward * 0.8660254f;
  float3 lightVector = lightPosition - uProjectedShadowRowZ.xyz;
  float lightVectorLengthSquared = dot(lightVector, lightVector);
  float3 lightDirection = lightVectorLengthSquared > 1e-10f
      ? lightVector * rsqrt(lightVectorLengthSquared)
      : float3(0.45f, 0.86f, 0.24f);
  float lightFacing = clamp(
      dot(normal, lightDirection),
      -1.0f,
      1.0f);
  float viewFacing = clamp(
      dot(normal, viewDirection),
      -1.0f,
      1.0f);
  // Z-A IkCharacter: HalfLambertBias=.1, ShadowStrength=.7,
  // RimLightOffset=.2, RimLightContrast=2, RimLightIntensity=.8,
  // BackRimLightIntensity=.01.
  float edge = saturate(1.0f - max(viewFacing, 0.0f));
  float rimDomain = saturate((edge - 0.2f) / 0.8f);
  float rim;
  float backRim;
  float3 diffuseColor;
  if (useAuthoredShadowColor) {
    float wrappedLambert = saturate(lightFacing * 0.5f + 0.5f);
    float biasedLambert = wrappedLambert * wrappedLambert;
    const float shadowBandLow = 0.3465f;
    const float shadowBandHigh = 0.3535f;
    float shadowAmount = saturate(
        1.0f - (biasedLambert - shadowBandLow) /
            (shadowBandHigh - shadowBandLow));
    diffuseColor = color * lerp(
        float3(1.0f, 1.0f, 1.0f),
        authoredResponse.rgb,
        shadowAmount);
    float rimSmooth = rimDomain * rimDomain * (3.0f - 2.0f * rimDomain);
    float rimShape = saturate(rimSmooth * 5.0f - 2.0f);
    const float layeredCharacterRimPresentationScale = 0.25f;
    rim = rimShape * authoredResponse.a * layeredCharacterRimPresentationScale;
    float rimMask = saturate(authoredResponse.a / 0.8f);
    backRim = saturate(-viewFacing) * 0.01f * rimMask *
        layeredCharacterRimPresentationScale;
  } else {
    float halfLambert = saturate(lightFacing * 0.5f + 0.6f);
    float diffuse = lerp(1.0f, halfLambert, 0.7f);
    diffuseColor = color * diffuse;
    rim = rimDomain * rimDomain * 0.8f;
    backRim = saturate(-viewFacing) * 0.01f;
  }
  return max(
      diffuseColor + color * (rim + backRim),
      float3(0.0f, 0.0f, 0.0f));
}

float4 evalAuthoredFireMesh(PSIn i) {
  float2 uv = saturate(i.uv + float2(uMaterialFlipbook1Cols, uMaterialFlipbook1Rows));
  float4 baked = sampleAtlasCombinedTopLeft(
      float4(uMaterialRect0U, uMaterialRect0V, uMaterialRect0W, uMaterialRect0H),
      float2(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows),
      uMaterialFlipbook0Frames,
      uMaterialFlipbook0Fps,
      uv,
      uMaterialTimeSec);
  float rgbCoverage = smoothstep(
      0.03f,
      0.20f,
      max(baked.r, max(baked.g, baked.b)));
  baked.a = max(baked.a, rgbCoverage);
  float baseEngulf = 1.0f - smoothstep(0.0f, 0.28f, saturate(i.generated.y));
  float2 centerXZ = i.generated.xz - float2(0.5f, 0.5f);
  float centerDist = length(centerXZ * float2(1.2f, 1.0f));
  float coreMask = 1.0f - smoothstep(0.0f, 0.23f, centerDist);
  float tipHideMask = baseEngulf * coreMask;
  float warmMask =
      smoothstep(0.68f, 0.98f, baked.r) *
      smoothstep(0.56f, 0.90f, baked.g) *
      (1.0f - smoothstep(0.22f, 0.58f, baked.b));
  baked.rgb = lerp(baked.rgb, float3(1.0f, 0.68f, 0.16f), warmMask * 0.44f);
  baked.rgb = lerp(baked.rgb, float3(1.0f, 0.82f, 0.30f), tipHideMask * 0.55f);
  baked.a = max(baked.a, baseEngulf * 0.95f);
  baked.a = max(baked.a, tipHideMask);
  if (baked.a <= 0.08f) discard;
  baked.a = 1.0f;
  return baked;
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
  float4 fb1 = float4(1,1,1,1);
  float4 fb2 = float4(1,1,1,1);
  int fireFlags = (int)(uMaterialFlags + 0.5f);
  bool has1 = (fireFlags & 1) != 0;
  bool has2 = (fireFlags & 2) != 0;
  bool authoredFireMesh = (fireFlags & 8) != 0;
  if (authoredFireMesh) {
    return evalAuthoredFireMesh(i);
  }
  float wobbleScale1 = has2 ? 0.010f : 0.0009f;
  float wobbleScale2 = has2 ? 0.002f : 0.0002f;
  float2 local1 = uv + wobble * wobbleScale1;
  float2 local2 = uv + wobble * wobbleScale2;
  if (has1) {
    fb1 = sampleAtlasCombined(float4(uMaterialRect0U, uMaterialRect0V, uMaterialRect0W, uMaterialRect0H),
                              float2(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows),
                              uMaterialFlipbook0Frames, uMaterialFlipbook0Fps, local1, vSeed, t, !has2);
    if (has2) {
      fb2 = sampleAtlasCombined(float4(uMaterialRect1U, uMaterialRect1V, uMaterialRect1W, uMaterialRect1H),
                                float2(uMaterialFlipbook1Cols, uMaterialFlipbook1Rows),
                                uMaterialFlipbook1Frames, uMaterialFlipbook1Fps, local2, vSeed, t, false);
    } else {
      fb2 = fb1;
    }
  }

  if (has1 && !has2) {
    float2 directUv = float2(uv.x, 1.0f - uv.y);
    float4 fbDirect = sampleFireDirect0(directUv, vSeed, t);
    float alpha = saturate(fbDirect.a);
    float3 rgb = saturate(fbDirect.rgb * 1.15f);
    alpha *= bottomFade;
    alpha *= fade;
    alpha = clamp(alpha, 0.0f, 0.985f);
    if (alpha < 0.003f) discard;
    rgb *= alpha;
    return float4(rgb, alpha);
  }

  float fb1A = saturate(fb1.a);
  float fb1Lum = saturate(dot(fb1.rgb, float3(0.3333f, 0.3333f, 0.3333f)));
  float speed = has2 ? lerp(0.95f, 1.10f, hash11(vSeed * 19.31f)) : 1.0f;
  float flow = t * 1.55f * speed;
  float flowY = flow * lerp(0.75f, 1.55f, y * y);
  float width = lerp(0.30f, 0.055f, pow(y, 2.35f));
  float widthHybrid = width * 2.80f;
  float yy = (y * 2.0f - 1.0f);
  yy = yy * 1.45f + 0.38f;
  yy /= 1.12f;
  float2 p = float2(x / widthHybrid, yy) * 1.22f;
  float sway = fbm2D(float2(x * 1.7f, y * 3.8f) + float2(0.0f, -flowY * 0.65f) + vSeed * 7.0f);
  p.x += (sway - 0.5f) * (has2 ? 0.015f : 0.004f) * (1.0f - y);
  float d0 = length(p);
  float2 advP = advect2D(p * float2(1.20f, 1.0f) + vSeed * 6.0f, flowY, 0.25f);
  float n = fbm2D(advP * float2(2.7f, 4.5f) + vSeed * 11.0f);
  float d = d0 + (n - 0.5f) * 0.18f * (1.0f - y);
  float core = saturate(1.0f - smoothstep(0.00f, 0.88f, d));
  float outer = saturate(1.0f - smoothstep(0.30f, 1.05f, d));
  float blobs = lickBlobs(x, y, advP, flowY, vSeed);
  float body = saturate(smoothstep(0.92f, 0.12f, d));
  float procAlpha = body * (0.60f + 0.55f * blobs);
  float calmFlicker = smoothFlicker(t * 1.2f, vSeed);
  procAlpha *= has2 ? (0.92f + 0.15f * calmFlicker) : (0.985f + 0.03f * calmFlicker);
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
  float mixW = 0.50f;
  float3 rgb = lerp(hybridRgb, fb2Rgb, mixW);
  float alpha = lerp(hybridMaskedA, fb2MaskedA, mixW);
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


float3 safeNormalize(float3 value, float3 fallback) {
  float len2 = dot(value, value);
  if (len2 < 1e-8f) return fallback;
  return value * rsqrt(len2);
}

int decodeReviewLightingProfile(float3 cameraForwardPacked) {
  return clamp(
      (int)floor(length(cameraForwardPacked) + 0.5f) - 1,
      0,
      4);
}

float3 applyReviewLightingProfile(float3 composite,
                                  float3 resolvedAlbedo,
                                  float3 sourceNormal,
                                  float3 cameraForwardPacked) {
  const int profile = decodeReviewLightingProfile(cameraForwardPacked);
  if (profile == 0) return max(composite, float3(0.0f, 0.0f, 0.0f));
  if (profile == 4) return max(composite, float3(0.0f, 0.0f, 0.0f));
  const float3 albedo = max(resolvedAlbedo, float3(0.0f, 0.0f, 0.0f));
  if (profile == 1) {
    const float3 shadowFloor = albedo * 0.50f;
    return max(
        lerp(composite, max(composite, shadowFloor), 0.56f),
        float3(0.0f, 0.0f, 0.0f));
  }
  if (profile == 2) {
    const float3 highlight = max(composite - albedo, float3(0.0f, 0.0f, 0.0f));
    return max(
        lerp(composite, albedo, 0.82f) + highlight * 0.16f,
        float3(0.0f, 0.0f, 0.0f));
  }
  const float3 cameraForward = safeNormalize(
      cameraForwardPacked,
      float3(0.0f, -0.6139406f, -0.7893522f));
  const float3 cameraRight = safeNormalize(
      cross(cameraForward, float3(0.0f, 1.0f, 0.0f)),
      float3(1.0f, 0.0f, 0.0f));
  const float3 normal = safeNormalize(
      sourceNormal,
      float3(0.0f, 1.0f, 0.0f));
  const float grazing = pow(abs(dot(normal, cameraRight)), 0.70f);
  const float3 grazingSurface = albedo * (0.36f + 0.78f * grazing);
  const float3 highlight = max(composite - albedo, float3(0.0f, 0.0f, 0.0f));
  return max(
      lerp(max(composite, albedo * 0.34f), grazingSurface, 0.62f) +
          highlight * 0.20f,
      float3(0.0f, 0.0f, 0.0f));
}

__PHLOSION_SHARED_WORLD_PBR_SECTION__

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
float3 computeMappedNormalFromTexel(PSIn i,
                                    bool isFrontFace,
                                    float2 sampleUv,
                                    float3 normalTexel,
                                    float sourceNormalScale) {
  float faceDirection = isFrontFace ? 1.0f : -1.0f;
  float3 n = normalize(i.worldNormal);
  if (dot(n, n) < 1e-6f) {
    float3 dx = ddx(i.worldPos);
    float3 dy = ddy(i.worldPos);
    n = normalize(cross(dx, dy));
  }
  n *= faceDirection;
  float2 mapXY = normalTexel.xy * 2.0f - 1.0f;
  mapXY *= max(sourceNormalScale, 0.0f);
  // Support standard RGB tangent-space normals and two-channel packed XY
  // normals. Decoded XY maps can use blue=0 or blue=255 as a sentinel, so
  // reconstruct Z for both encodings.
  float authoredZ = normalTexel.z * 2.0f - 1.0f;
  float reconZ = sqrt(max(1.0f - saturate(dot(mapXY, mapXY)), 0.0f));
  float useReconstructedZ =
      (normalTexel.z <= (1.5f / 255.0f) ||
       normalTexel.z >= (253.5f / 255.0f))
          ? 1.0f
          : 0.0f;
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

float3 computeMappedNormal(PSIn i,
                           bool isFrontFace,
                           float2 sampleUv,
                           float2 uvDx,
                           float2 uvDy,
                           bool useNormalTexture,
                           float normalScale) {
  float3 normalTexel = useNormalTexture
      ? sampleTextureWithWrap(
            gNormalTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).xyz
      : float3(0.5f, 0.5f, 1.0f);
  return computeMappedNormalFromTexel(
      i,
      isFrontFace,
      sampleUv,
      normalTexel,
      useNormalTexture ? max(normalScale, 0.0f) * 1.25f : 0.0f);
}

float3 computeMappedFresnelLayerNormal(PSIn i,
                                       bool isFrontFace,
                                       float2 sampleUv,
                                       float2 uvDx,
                                       float2 uvDy,
                                       bool useLayerNormalTexture,
                                       float normalScale) {
  float3 normalTexel = useLayerNormalTexture
      ? sampleTextureWithWrap(
            gMetallicRoughnessTex,
            sampleUv,
            uvDx,
            uvDy,
            uWrapS,
            uWrapT).xyz
      : float3(0.5f, 0.5f, 1.0f);
  return computeMappedNormalFromTexel(
      i,
      isFrontFace,
      sampleUv,
      normalTexel,
      useLayerNormalTexture ? max(normalScale, 0.0f) : 0.0f);
}

float3 applyFacialOverlay(PSIn i,
                             bool isFrontFace,
                             float3 albedo,
                             float2 sampleUv,
                             float2 uvDx,
                             float2 uvDy,
                             bool useNormalTexture,
                             bool useMetallicRoughnessTexture,
                             bool useOcclusionTexture,
                             bool useEmissiveTexture,
                             float normalScale,
                             float occlusionStrength,
                             float3 cameraPos,
                             float3 cameraForwardPacked,
                             float3 cameraTarget,
                             bool concealTongue) {
  float3 normal = computeMappedNormal(
      i,
      isFrontFace,
      sampleUv,
      uvDx,
      uvDy,
      useNormalTexture,
      normalScale);
  float3 cameraForward = safeNormalize(
      cameraForwardPacked,
      normalize(float3(0.0f, -0.6139406f, -0.7893522f)));
  float3 cameraRight = cross(cameraForward, float3(0.0f, 1.0f, 0.0f));
  if (dot(cameraRight, cameraRight) < 1e-6f) {
    cameraRight = cross(cameraForward, float3(0.0f, 0.0f, 1.0f));
  }
  cameraRight = safeNormalize(cameraRight, float3(1.0f, 0.0f, 0.0f));
  float3 cameraUp = safeNormalize(
      cross(cameraRight, cameraForward),
      float3(0.0f, 1.0f, 0.0f));
  float3 view = safeNormalize(cameraPos - i.worldPos, -cameraForward);
  float3 light = safeNormalize(
      cameraRight * 0.45f + cameraUp * 0.86f - cameraForward * 0.24f,
      float3(0.45f, 0.86f, 0.24f));
  float4 shadowSpec = useMetallicRoughnessTexture
      ? sampleTextureWithWrap(
            gMetallicRoughnessTex,
            sampleUv,
            uvDx,
            uvDy,
            uWrapS,
            uWrapT)
      : float4(0.0f, 0.0f, 0.0f, 0.0f);
  float occlusion = useOcclusionTexture
      ? lerp(
            1.0f,
            sampleTextureWithWrap(
                gOcclusionTex,
                sampleUv,
                uvDx,
                uvDy,
                uWrapS,
                uWrapT).r,
            saturate(occlusionStrength))
      : 1.0f;
  float halfLambert = saturate(dot(normal, light) * 0.5f + 0.6f);
  float shadowAmount = (1.0f - halfLambert) * 0.7f;
  float3 shaded = lerp(albedo, shadowSpec.rgb, shadowAmount) * occlusion;
  float3 halfVector = safeNormalize(view + light, normal);
  // Z-A Gastly's authored face specular is at most 0.05. Forge reserves
  // values above 0.0625 for tongue coverage; this guard band keeps filtered
  // tongue edges distinct from face specular.
  float tongueMask = smoothstep(0.07f, 0.50f, shadowSpec.a);
  if (concealTongue && shadowSpec.a > 0.07f) {
    discard;
  }
  float sourceSpecularMask = max(
      shadowSpec.a - 0.5f * tongueMask,
      0.0f);
  float specular = pow(max(dot(normal, halfVector), 0.0f), 32.0f) *
      sourceSpecularMask;
  float tongueDiffuse = lerp(
      0.82f,
      1.06f,
      smoothstep(0.0f, 1.0f, halfLambert));
  float3 tongueShaded = albedo * tongueDiffuse *
      lerp(1.0f, occlusion, 0.25f);
  float ndh = max(dot(normal, halfVector), 0.0f);
  float tongueSpecular = pow(ndh, 12.0f) * 0.105f +
      pow(ndh, 48.0f) * 0.04f;
  shaded = lerp(shaded, tongueShaded, tongueMask);
  specular = lerp(specular, tongueSpecular, tongueMask);
  float edge = saturate(1.0f - max(dot(normal, view), 0.0f));
  float rimDomain = saturate((edge - 0.4f) / 0.6f);
  float rimMask = useEmissiveTexture
      ? sampleTextureWithWrap(
            gEmissiveTex,
            sampleUv,
            uvDx,
            uvDy,
            uWrapS,
            uWrapT).r
      : 1.0f;
  float rim = pow(rimDomain, 5.0f) * 0.8f * rimMask;
  float backRim = saturate(-dot(normal, view)) * 0.08f * rimMask;
  rim *= lerp(1.0f, 0.18f, tongueMask);
  backRim *= lerp(1.0f, 0.18f, tongueMask);
  return max(
      shaded + float3(specular, specular, specular) +
          albedo * (rim + backRim),
      float3(0.0f, 0.0f, 0.0f));
}

float3 applyWorldLitModel(PSIn i,
                          bool isFrontFace,
                          float3 linearColor,
                          float2 sampleUv,
                          float2 uvDx,
                          float2 uvDy,
                          bool useNormalTexture,
                          bool useMetallicRoughnessTexture,
                          bool useSpecularStrengthTexture,
                          bool useOcclusionTexture,
                          bool useEmissiveTexture,
                          float normalScale,
                          float metallicFactor,
                          float roughnessFactor,
                          float specularIntensity,
                          float occlusionStrength,
                          float3 emissiveFactor,
                          float3 cameraPos,
                          float3 cameraForwardPacked,
                          float3 cameraTarget) {
  float3 n = computeMappedNormal(i, isFrontFace, sampleUv, uvDx, uvDy, useNormalTexture, normalScale);
  float4 orm = float4(1.0f, 1.0f, 1.0f, 1.0f);
  if (useMetallicRoughnessTexture) {
    orm = sampleTextureWithWrap(
              gMetallicRoughnessTex,
              sampleUv,
              uvDx,
              uvDy,
              uWrapS,
              uWrapT);
  }
  float roughness = clamp(orm.g * saturate(roughnessFactor), 0.16f, 1.0f);
  float metallic = clamp(orm.b * saturate(metallicFactor), 0.0f, 1.0f);
  float ao = 1.0f;
  if (useOcclusionTexture) {
    float occTex = sampleTextureWithWrap(gOcclusionTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).r;
    ao = lerp(1.0f, occTex, saturate(occlusionStrength));
  }

  float3 albedo = saturate(linearColor);
  float dielectricSpecular = useSpecularStrengthTexture
      ? saturate(specularIntensity) * saturate(orm.a)
      : 0.04f;
  float3 F0 = lerp(
      float3(dielectricSpecular, dielectricSpecular, dielectricSpecular),
      albedo,
      metallic);
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
  const float directIntensity = __PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265f;
  const float3 ambientColor = float3(1.0f, 1.0f, 1.0f);
  const float ambientIntensity = __PHLOSION_PBR_AMBIENT_INTENSITY__;

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
  diffuseIBL *= __PHLOSION_PBR_DIFFUSE_IBL_SCALE__;
  specularIBL *= __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
  const bool layeredEyeMode =
      (uMaterialMode > 27.5f && uMaterialMode < 28.5f) ||
      (uMaterialMode > 29.5f && uMaterialMode < 30.5f);
  const bool bakedEyeDiffuse =
      layeredEyeMode && uProjectedShadowRowY.w < -0.5f;
  const bool separateEyeCoat =
      layeredEyeMode && !bakedEyeDiffuse;
  // Scarlet's EyeClearCoat reserves its outer specular response for the
  // dedicated coat pass below. PLA's plain Eye family has no separate coat,
  // so preserve its ordinary dielectric/environment reflection.
  if (separateEyeCoat) {
    specularIBL = float3(0.0f, 0.0f, 0.0f);
  }
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
  // PLA's plain Eye shader can carry a sparse layer-5 catchlight in the
  // emissive texture while the rest of the eye still needs its softer native
  // diffuse response. Gate the fill per pixel so the catchlight stays exact
  // without making Geodude's non-emissive sclera/iris collapse to charcoal.
  if (bakedEyeDiffuse) {
    float emissiveCoverage = saturate(max(emissive.r, max(emissive.g, emissive.b)));
    shaded = lerp(shaded, albedo, 0.25f * (1.0f - emissiveCoverage));
  }
  return max(shaded + emissive, float3(0.0f, 0.0f, 0.0f));
}

float3 sampleStageReflectionProbe(Texture2D probeTexture,
                                    float3 direction,
                                    float sourceLod,
                                    float fallbackRoughness);
float3 samplePackedSpecularProbe(Texture2D probeTexture,
                                  float3 direction,
                                  float roughness);

float2 resolveRefractiveEyeParallaxUv(PSIn i,
                                float2 uv,
                                float2 uvDx,
                                float2 uvDy,
                                float3 cameraPos) {
  float packedHeightIor = max(uMaterialFlipbook1Fps, 0.0f);
  float parallaxHeight = frac(packedHeightIor);
  if (parallaxHeight <= 1e-5f) return uv;
  float parallaxIor = max(floor(packedHeightIor) / 1000.0f, 1.0f);
  float3 geometricNormal = safeNormalize(
      i.worldNormal,
      float3(0.0f, 1.0f, 0.0f));
  float3 tangent = safeNormalize(
      i.worldTangent.xyz,
      float3(1.0f, 0.0f, 0.0f));
  float3 bitangent = cross(geometricNormal, tangent) * i.worldTangent.w;
  float3 viewWorld = safeNormalize(
      cameraPos - i.worldPos,
      geometricNormal);
  float3 viewTangent = float3(
      dot(viewWorld, tangent),
      dot(viewWorld, bitangent),
      dot(viewWorld, geometricNormal));
  float eta = 1.0f / parallaxIor;
  float refractionK = 1.0f - eta * eta *
      (1.0f - viewTangent.z * viewTangent.z);
  if (refractionK < 0.0f) return uv;
  float3 refracted = float3(
      -eta * viewTangent.x,
      -eta * viewTangent.y,
      -sqrt(refractionK));
  float refractedLengthSquared = dot(refracted, refracted);
  if (refractedLengthSquared <= 1e-8f || abs(refracted.z) <= 1e-6f) {
    return uv;
  }
  refracted *= rsqrt(refractedLengthSquared);

  // Z-A variations 682 and 1214 normalize the summed UV derivatives before
  // projecting the refracted direction into the authored texture footprint.
  float2 footprint = abs(uvDx + uvDy);
  float footprintLengthSquared = dot(footprint, footprint);
  footprint = footprintLengthSquared > 1e-8f
      ? footprint * rsqrt(footprintLengthSquared)
      : 0.70710678f.xx;

  float normalDotView = saturate(abs(viewTangent.z));
  float layerScale = 12.0f - 10.0f * normalDotView;
  int sampleCount = (int)(floor(layerScale) + 2.0f);
  float depthStep = 1.0f / layerScale;
  float grazingFade = 1.0f - pow(1.0f - normalDotView, 5.0f);
  float2 offsetStep = float2(-footprint.x, footprint.y) *
      (refracted.xy / refracted.z) *
      (parallaxHeight / layerScale) * grazingFade;

  float2 currentOffset = float2(0.0f, 0.0f);
  float currentDepth = 1.0f;
  float previousDepth = 1.1f;
  float previousHeight = 1.0f;
  [loop]
  for (int layer = 0; layer < 14; ++layer) {
    if (layer >= sampleCount) break;
    float sampledHeight = sampleTextureWithWrap(
        gEmissiveTex,
        uv + currentOffset,
        uvDx,
        uvDy,
        uWrapS,
        uWrapT).a;
    if (sampledHeight >= currentDepth) {
      float currentDelta = sampledHeight - currentDepth;
      float previousDelta = previousHeight - previousDepth;
      float denominator = currentDelta - previousDelta;
      if (abs(denominator) > 1e-6f) {
        currentOffset -= offsetStep * (currentDelta / denominator);
      }
      break;
    }
    previousDepth = currentDepth;
    previousHeight = sampledHeight;
    currentDepth -= depthStep;
    currentOffset += offsetStep;
  }
  return uv + currentOffset;
}

float3 layeredCharacterLocalReflectionDirection(float3 viewDirection,
                                    float3 mappedNormal) {
  // The compiled source max-abs normalizes this vector before its cube
  // lookup. Positive direction scaling is homogeneous for a cubemap, so keep
  // the exact reflect(-view, normal) ray. The diffuse-irradiance cube's Z flip
  // belongs to that separate scene resource and must not leak into this probe.
  return reflect(-viewDirection, mappedNormal);
}

float3 layeredCharacterEmissionColor(float packedColor) {
  float packed = floor(max(packedColor, 0.0f) + 0.5f);
  float red = floor(packed / 65536.0f);
  float remainder = packed - red * 65536.0f;
  float green = floor(remainder / 256.0f);
  float blue = remainder - green * 256.0f;
  float3 color = float3(red, green, blue) / 255.0f;
  float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
  return luminance > 1e-6f
      ? color / luminance
      : float3(1.0f, 1.0f, 1.0f);
}

int stageLightingCategory(float packedCategoryAndFlags) {
  float category;
  if (packedCategoryAndFlags >= 8.0f) {
    category = packedCategoryAndFlags - 8.0f;
  } else if (packedCategoryAndFlags < 0.5f) {
    category = 6.0f;
  } else if (frac(packedCategoryAndFlags) > 0.001f) {
    category = frac(packedCategoryAndFlags) * 16.0f;
  } else {
    category = packedCategoryAndFlags;
  }
  return clamp((int)round(category), 0, 7);
}

float stageDirectIntensity(int category) {
  if (category == 2) return 0.08f;
  if (category == 5) return 4.14f;
  if (category == 6) return 4.20f;
  return 3.14f;
}

float stageGiIntensity(int category) {
  return category == 2 ? 0.07f : 1.0f;
}

float3 stageRimColor(int category) {
  if (category == 0) return 1.0f.xxx;
  if (category == 1) {
    return float3(0.7254902f, 0.9843137f, 0.5333334f);
  }
  return 0.0f.xxx;
}

float3 applyLayeredCharacter(PSIn i,
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
                              float halfLambertBias,
                              float shadowStrength,
                              float occlusionStrength,
                              float3 rimParameters,
                              float3 cameraPos,
                              float3 cameraForwardPacked,
                              float3 cameraTarget,
                              bool nativeEye) {
  sampleUv = nativeEye
      ? resolveRefractiveEyeParallaxUv(i, sampleUv, uvDx, uvDy, cameraPos)
      : sampleUv;
  if (nativeEye) {
    linearColor = saturate(sampleTextureWithWrap(
        gTex, sampleUv, uvDx, uvDy, uWrapS, uWrapT).rgb * i.col.rgb);
  }
  float3 normal = computeMappedNormal(
      i,
      isFrontFace,
      sampleUv,
      uvDx,
      uvDy,
      useNormalTexture,
      // Z-A's IkCharacter NormalHeight is literal; the shared PBR decoder's
      // 1.25 presentation boost must not amplify facial/body relief.
      normalScale * 0.8f);
  float3 cameraForward = safeNormalize(
      cameraForwardPacked,
      normalize(float3(0.0f, -0.6139406f, -0.7893522f)));
  float3 cameraRight = cross(cameraForward, float3(0.0f, 1.0f, 0.0f));
  if (dot(cameraRight, cameraRight) < 1e-6f) {
    cameraRight = cross(cameraForward, float3(0.0f, 0.0f, 1.0f));
  }
  cameraRight = safeNormalize(cameraRight, float3(1.0f, 0.0f, 0.0f));
  float3 viewDirection = safeNormalize(
      cameraPos - i.worldPos,
      -cameraForward);
  bool zaSourceStage =
      decodeReviewLightingProfile(cameraForwardPacked) == 4;
  int zaLightCategory = stageLightingCategory(uLightProjectionUvRowV.w);
  float3 lightPosition =
      cameraPos + cameraRight * 0.5f - cameraForward * 0.8660254f;
  // The retained stage record and imported model shading basis use opposite Z
  // handedness. Convert here so the source-stage review rig illuminates the
  // character's face instead of its back.
  float3 lightDirection = zaSourceStage
      ? float3(-0.44695543f, 0.64944804f, -0.61518134f)
      : safeNormalize(
            lightPosition - cameraTarget,
            float3(0.45f, 0.86f, 0.24f));
  float sourceDirectScale = zaSourceStage
      ? stageDirectIntensity(zaLightCategory) * (1.0f / 3.14159265f)
      : 1.0f;
  float4 shadowSpec = useMetallicRoughnessTexture
      ? sampleTextureWithWrap(
            gMetallicRoughnessTex,
            sampleUv,
            uvDx,
            uvDy,
            uWrapS,
            uWrapT)
      : float4(1.0f, 1.0f, 1.0f, 0.0f);
  float4 surfaceControl = useOcclusionTexture
      ? sampleTextureWithWrap(
            gOcclusionTex,
            sampleUv,
            uvDx,
            uvDy,
            uWrapS,
            uWrapT)
      : float4(1.0f, 0.0f, 1.0f / 3.0f, 0.0f);
  // Forge has already evaluated the selected fragment's literal AO use:
  // OcclusionMap * OcclusionStrength blends ShadowingColorMap from the base
  // ShadowingColor before ordered layers. Do not apply that lane twice.
  float metallic = saturate(surfaceControl.g);
  float specularOffset = surfaceControl.b * 1.5f - 0.5f;
  float specularContrast = surfaceControl.a * 5.0f;
  float shadowingGiGain = saturate(uMaterialFlipbook1Frames);
  // D3D12's fixed 64-DWORD root signature repacks mode-32 rect0.xyz into
  // otherwise-unused PS-only fields; see makeWorldPsConstants().
  float reflectionBlur = max(uMaterialTimeSec, 0.0f);
  float packedSurface = nativeEye
      ? 0.0f
      : max(uMaterialFlipbook1Fps, 0.0f);
  float packedSurfaceRemainder = packedSurface;
  float diffusionLevels = nativeEye
      ? 0.0f
      : saturate(frac(packedSurfaceRemainder));
  float normalDotLightSigned = dot(normal, lightDirection);
  float lambert = max(normalDotLightSigned, 0.0f);
  float wrappedLambert = saturate(
      normalDotLightSigned * 0.5f + 0.5f);
  bool hasAuthoredColorProcess = uProjectedShadowRowX.w > 0.05f;
  float authoredShadowBias = hasAuthoredColorProcess
      ? uProjectedShadowRowX.x
      : 1.0f;
  // All selected Z-A IkCharacter variants apply x + bias * (x^2 - x) to
  // wrapped N.L. ShadowingBias is not a power curve.
  float biasedLambert = saturate(
      wrappedLambert + authoredShadowBias *
          (wrappedLambert * wrappedLambert - wrappedLambert));
  float authoredShadowShift = hasAuthoredColorProcess
      ? uProjectedShadowRowX.y
      : -0.5f;
  float authoredShadowContrast = hasAuthoredColorProcess
      ? uProjectedShadowRowX.z
      : 0.0f;
  float halfLambertBiasSquared = halfLambertBias * halfLambertBias;
  float shadowBandLow =
      (0.5f - 0.5f * halfLambertBiasSquared) * shadowStrength;
  float shadowBandHigh =
      (0.5f + 0.5f * halfLambertBiasSquared) * shadowStrength;
  float shadowBandWidth = max(shadowBandHigh - shadowBandLow, 1e-5f);
  float shadowAmount = saturate(
      1.0f - (biasedLambert - shadowBandLow) / shadowBandWidth);
  // Selected Z-A IkCharacter programs combine a projected 2D mask with a
  // 16-tap cascaded shadow-array result, then multiply that visibility into
  // wrapped N.L before ShadowingShift. The loose model archive contains
  // neither bound scene texture, so keep the scene boundary explicitly
  // neutral. Do not substitute the project-specific projected-shadow format.
  const float sourceSceneShadowVisibility = 1.0f;
  const float sourceSceneShadowBypass = 0.0f;
  float effectiveDirectShadowVisibility = saturate(
      sourceSceneShadowVisibility +
          sourceSceneShadowBypass * sourceSceneShadowBypass);
  float shadowedWrappedLambert =
      wrappedLambert * sourceSceneShadowVisibility;
  float3 albedo = saturate(linearColor);
  // ShadowingGIGain scales the compiled RGB difference between the
  // unshadowed diffuse color and the AO-resolved absolute shadow color. The
  // packed shadowSpec RGB is not a multiplicative tint; multiplying by it
  // double-darkens pale bodies around eye sockets and other contours.
  float combinedShadowAmount = shadowAmount * shadowingGiGain;
  float3 shaded = lerp(
      albedo,
      shadowSpec.rgb,
      combinedShadowAmount) *
      (zaSourceStage
           ? biasedLambert * effectiveDirectShadowVisibility *
                 sourceDirectScale
           : 1.0f);
  if (hasAuthoredColorProcess) {
    // Source middle/dark processing consumes max(directDiffuse RGB) after
    // inverse-pi scene light and shadow composition. With unavailable scene
    // RGB normalized to unit white, this is its literal scalar counterpart.
    float colorProcessLight = saturate(
        biasedLambert * effectiveDirectShadowVisibility *
        (zaSourceStage ? sourceDirectScale : 1.0f));
    float midDomain = saturate(
        1.0f - colorProcessLight + uProjectedShadowRowY.x);
    float midSmooth = midDomain * midDomain *
        (3.0f - 2.0f * midDomain);
    float midArea = saturate(
        midSmooth * (1.0f + 2.0f * uProjectedShadowRowY.y) -
        uProjectedShadowRowY.y);
    float darkDomain = saturate(
        1.0f - colorProcessLight + uProjectedShadowRowY.w);
    float darkSmooth = darkDomain * darkDomain *
        (3.0f - 2.0f * darkDomain);
    float darkArea = saturate(
        darkSmooth * (1.0f + 2.0f * uProjectedShadowRowZ.x) -
        uProjectedShadowRowZ.x);
    float shadowProcessDomain = saturate(
        shadowedWrappedLambert - authoredShadowShift);
    float shadowProcessSmooth = shadowProcessDomain *
        shadowProcessDomain * (3.0f - 2.0f * shadowProcessDomain);
    float shadowProcessArea = saturate(
        shadowProcessSmooth *
            (1.0f + 2.0f * authoredShadowContrast) -
        authoredShadowContrast);
    float3 darkHsv = rgbToHsv(max(shaded, 0.0f.xxx));
    darkHsv.x = frac(darkHsv.x + uProjectedShadowRowZ.y);
    float3 midHsv = rgbToHsv(max(shaded, 0.0f.xxx));
    midHsv.x = frac(midHsv.x + uProjectedShadowRowY.z);
    float3 darkHueColor = hsvToRgb(darkHsv);
    float3 midHueColor = hsvToRgb(midHsv);
    float3 baseToMidHue = lerp(shaded, midHueColor, midArea);
    float3 darkToBaseMid = lerp(darkHueColor, baseToMidHue, darkArea);
    float3 baseMidToDark = lerp(baseToMidHue, darkHueColor, darkArea);
    float hueAreaScale = 1.0f - 0.5f * midArea *
        uProjectedShadowRowZ.w;
    shaded = lerp(
        darkToBaseMid,
        baseMidToDark,
        shadowProcessArea) * hueAreaScale;
    shaded *= 1.0f + 2.0f * diffusionLevels *
        (1.0f - colorProcessLight);
  }
  float4 rimResponse = !nativeEye && useEmissiveTexture
      ? sampleTextureWithWrap(
            gEmissiveTex,
            sampleUv,
            uvDx,
            uvDy,
            uWrapS,
            uWrapT)
      : float4(0.0f, 0.0f, 0.0f, 1.0f);
  float facing = dot(normal, viewDirection);
  float edge = saturate(1.0f - max(facing, 0.0f));
  float rimOffset = clamp(rimParameters.r, 0.0f, 0.99f);
  float rimDomain = saturate(
      (edge - rimOffset) / max(1.0f - rimOffset, 1e-4f));
  // Selected Z-A IkCharacter 514/594 applies cubic smoothstep followed by
  // clamp(x * (1 + 2c) - c); RimLightContrast is not a power exponent.
  float rimSmooth = rimDomain * rimDomain * (3.0f - 2.0f * rimDomain);
  float rimContrast = rimParameters.g;
  float rimShape = saturate(
      rimSmooth * (1.0f + 2.0f * rimContrast) - rimContrast);
  // The packed map carries raw pre-composite Z-A rim scalars. Keep the
  // unresolved source-exposure calibration explicit in presentation code
  // instead of baking it irreversibly into imported assets.
  const float layeredCharacterRimPresentationScale = 0.25f;
  float rim = nativeEye
      ? 0.0f
      : rimShape * rimResponse.r * layeredCharacterRimPresentationScale;
  float backRim = nativeEye
      ? 0.0f
      : rimShape * smoothstep(
            0.0f,
            1.0f,
            saturate(
                (0.4f - normalDotLightSigned - saturate(facing)) * 2.5f)) *
          rimResponse.g * layeredCharacterRimPresentationScale;
  float3 sceneRimColor = zaSourceStage
      ? stageRimColor(zaLightCategory)
      : 1.0f.xxx;
  float specularStrength = saturate(shadowSpec.a);
  // Every selected Kanto Z-A material disables EnableHairSpecular. Fur and
  // feather relief stays in the real normal/specular/rim paths; adding a
  // species-classified sheen here would execute a source-disabled branch.
  // Z-A's selected IkCharacter programs add scene diffuse irradiance at LOD
  // 0 with the mapped normal's Z component flipped. The source cube and
  // exposure are not present in the loose assets; use the strongly filtered
  // end of the authored local environment carrier as the explicit offline
  // bridge. Its mip-5 mean is 0.00627 linear luminance, so the explicit 32x
  // exposure bridge restores a neutral 0.20 diffuse fill. Its sampler falls
  // back to Phlosion's neutral room for an ordinary environment texture.
  float3 diffuseProbeDirection = safeNormalize(
      float3(normal.x, normal.y, -normal.z),
      normal);
  float3 neutralDiffuseIrradiance = zaSourceStage
      ? samplePackedSpecularProbe(
            gLightProjectionTex,
            diffuseProbeDirection,
            1.0f)
      : sampleStageReflectionProbe(
            gEnvTex,
            diffuseProbeDirection,
            5.0f,
            1.0f);
  if (zaSourceStage) {
    neutralDiffuseIrradiance = clamp(
        neutralDiffuseIrradiance,
        float3(0.0f, 0.0f, 0.0f),
        float3(0.006f, 0.006f, 0.006f));
  }
  // The retained cube is exact, but the source framebuffer exposure is not
  // present in the loose UI-light package. A 96x review exposure keeps broad
  // airborne silhouettes readable from above and behind without rotating the
  // recovered key light or baking a fill into the imported material.
  float layeredCharacterDiffuseEnvironmentExposureBridge = zaSourceStage
      ? 96.0f * 3.14159265f
      : 32.0f;
  float3 environmentDiffuse = neutralDiffuseIrradiance * albedo *
      (1.0f - metallic) * layeredCharacterDiffuseEnvironmentExposureBridge *
      (zaSourceStage
           ? stageGiIntensity(zaLightCategory) * (1.0f / 3.14159265f)
           : 1.0f);
  float3 nativeBase = shaded + environmentDiffuse +
      albedo * sceneRimColor * (rim + backRim);

  // The decompiled Z-A IkCharacter body program carries no generic
  // roughness/PBR coat. Preserve its layer-resolved specular shape, metal
  // response, reflection blur and diffusion controls.
  float3 halfDirection = safeNormalize(
      lightDirection + viewDirection,
      normal);
  float normalDotHalf = max(dot(normal, halfDirection), 0.0f);
  float normalDotView = max(dot(normal, viewDirection), 0.0f);
  float normalDotLight = lambert;
  // Selected 514/594 subtracts the authored offset, smoothsteps the domain,
  // and performs the literal clamp(x * (1 + 2c) - c) contrast remap.
  float specularDomain = saturate(normalDotHalf - specularOffset);
  float specularSmooth = specularDomain * specularDomain *
      (3.0f - 2.0f * specularDomain);
  float specularLobe = saturate(
      specularSmooth * (1.0f + 2.0f * specularContrast) - specularContrast);
  // Compiled IkCharacter applies the layer-resolved intensity once. Squaring
  // it was a viewer gloss workaround and suppressed authored weak highlights.
  float dielectricSpecular = specularStrength;
  // Eye 682/1214 retains the same direct-specular/local-reflection split.
  float surfaceSpecular = dielectricSpecular;
  float3 specularColor = 1.0f.xxx;
  float3 directSpecular = specularColor * surfaceSpecular * specularLobe *
      normalDotLight * (zaSourceStage ? sourceDirectScale : 0.72f);
  float3 reflection = layeredCharacterLocalReflectionDirection(
      viewDirection,
      normal);
  float reflectionRoughness = clamp(
      reflectionBlur * 0.16f,
      0.04f,
      0.92f);
  float3 environmentRadiance = sampleStageReflectionProbe(
      gEnvTex,
      reflection,
      reflectionBlur + max(litTextureDetailLodBias(), 0.0f),
      reflectionRoughness);
  float grazingResponse = lerp(
      1.0f,
      1.35f,
      pow(1.0f - normalDotView, 5.0f));
  float environmentOcclusion = computeSpecularOcclusion(
      normalDotView,
      1.0f,
      reflectionRoughness);
  float3 environmentSpecular =
      environmentRadiance * albedo * metallic * grazingResponse *
      environmentOcclusion * __PHLOSION_PBR_SPECULAR_IBL_SCALE__ *
      (zaSourceStage ? 32.0f : 1.0f);
  float3 diffuse = nativeBase * (1.0f - metallic * 0.85f);
  // Mode 32 packs per-pixel body-emission luminance into blue and transports
  // its material-constant 24-bit RGB through the mode-local rowU.x lane.
  // Mode 35's highlight is pre-lighting source color.
  float3 bodyEmission = !nativeEye && useEmissiveTexture
      ? rimResponse.b * layeredCharacterEmissionColor(uLightProjectionUvRowU.x)
      : float3(0.0f, 0.0f, 0.0f);
  return max(
      diffuse + directSpecular + environmentSpecular + bodyEmission,
      float3(0.0f, 0.0f, 0.0f));
}

float3 applySubsurfaceSurface(PSIn i,
                             bool isFrontFace,
                             float3 linearColor,
                             float2 sampleUv,
                             float2 uvDx,
                             float2 uvDy,
                             bool useNormalTexture,
                             bool useMetallicRoughnessTexture,
                             bool useOcclusionTexture,
                             bool useSssMaskTexture,
                             float normalScale,
                             float roughnessFactor,
                             float occlusionStrength,
                             float3 subsurfaceColor,
                             float3 cameraPos,
                             float3 cameraForwardPacked,
                             float surfaceProfile) {
  float3 normal = computeMappedNormal(
      i,
      isFrontFace,
      sampleUv,
      uvDx,
      uvDy,
      useNormalTexture,
      normalScale);
  float3 cameraForward = safeNormalize(
      cameraForwardPacked,
      normalize(float3(0.0f, -0.6139406f, -0.7893522f)));
  float3 cameraRight = cross(cameraForward, float3(0.0f, 1.0f, 0.0f));
  if (dot(cameraRight, cameraRight) < 1e-6f) {
    cameraRight = cross(cameraForward, float3(0.0f, 0.0f, 1.0f));
  }
  cameraRight = safeNormalize(cameraRight, float3(1.0f, 0.0f, 0.0f));
  float3 cameraUp = safeNormalize(
      cross(cameraRight, cameraForward),
      float3(0.0f, 1.0f, 0.0f));
  float3 viewDirection = safeNormalize(cameraPos - i.worldPos, -cameraForward);
  float3 lightDirection = safeNormalize(
      cameraRight * 0.45f + cameraUp * 0.86f - cameraForward * 0.24f,
      float3(0.45f, 0.86f, 0.24f));
  float3 halfDirection = safeNormalize(
      lightDirection + viewDirection,
      normal);
  float roughness = useMetallicRoughnessTexture
      ? clamp(
            sampleTextureWithWrap(
                gMetallicRoughnessTex,
                sampleUv,
                uvDx,
                uvDy,
                uWrapS,
                uWrapT).g * saturate(roughnessFactor),
            0.04f,
            1.0f)
      : 1.0f;
  bool fibreSurface = abs(surfaceProfile - 1.0f) < 0.25f;
  float coarseRoughness = fibreSurface && useMetallicRoughnessTexture
      ? clamp(
            sampleTextureWithWrap(
                gMetallicRoughnessTex,
                sampleUv,
                uvDx * 4.0f,
                uvDy * 4.0f,
                uWrapS,
                uWrapT).g * saturate(roughnessFactor),
            0.04f,
            1.0f)
      : roughness;
  float ao = useOcclusionTexture
      ? lerp(
            1.0f,
            sampleTextureWithWrap(
                gOcclusionTex,
                sampleUv,
                uvDx,
                uvDy,
                uWrapS,
                uWrapT).r,
            saturate(occlusionStrength))
      : 1.0f;
  float sssMask = useSssMaskTexture
      ? sampleTextureWithWrap(
            gEmissiveTex,
            sampleUv,
            uvDx,
            uvDy,
            uWrapS,
            uWrapT).r
      : 0.0f;
  float3 albedo = saturate(linearColor);
  float3 subsurfaceTint = lerp(
      albedo,
      max(subsurfaceColor, float3(0.0f, 0.0f, 0.0f)),
      0.35f);
  float wrappedNdotL = saturate((dot(normal, lightDirection) + 0.5f) / 1.5f);
  float subsurfaceFill = saturate(sssMask) *
      (1.0f - max(dot(normal, lightDirection), 0.0f)) * 0.10f;
  float specularPower = lerp(16.0f, 96.0f, 1.0f - roughness);
  float sourceSpecular = pow(
      max(dot(normal, halfDirection), 0.0f),
      specularPower) * 0.04f * 0.45f;
  float nDotV = saturate(dot(normal, viewDirection));
  float3 environmentFresnel = fresnelSchlickRoughness(
      nDotV,
      float3(0.04f, 0.04f, 0.04f),
      roughness);
  // Exact SV SSS variation 56 samples diffuse irradiance at the mapped
  // normal (tcb_34, LOD 0) and specular radiance at the reflected view
  // vector (tcb_36, roughness-selected LOD). Source scene cubes are runtime
  // state, so bridge their proven roles through the shared neutral room.
  float3 environmentDiffuse = sampleNeutralEnvironment(normal, 1.0f) *
      albedo * (float3(1.0f, 1.0f, 1.0f) - environmentFresnel) * ao *
      __PHLOSION_PBR_DIFFUSE_IBL_SCALE__ * 1.18f;
  float3 reflection = reflect(-viewDirection, normal);
  float3 environmentSpecular = sampleNeutralEnvironment(
      reflection,
      roughness) * environmentFresnel *
      computeSpecularOcclusion(nDotV, ao, roughness) *
      __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
  float3 directDiffuse = albedo * 0.90f * wrappedNdotL * ao;
  // The exact SV scene cubes remain unavailable offline. Preserve useful
  // neutral-rig exposure while retaining authored AO.
  float3 neutralFill = albedo *
      lerp(0.08f, 0.12f, saturate(sssMask)) *
      lerp(1.0f, ao, 0.5f);
  float qualityDetail = saturate(
      (0.90f - litTextureDetailLodBias()) / 1.30f);
  float fibreRelief = saturate(
      (coarseRoughness - roughness) * 3.25f);
  float velvet = pow(1.0f - nDotV, 2.5f);
  float fibreSheen = fibreSurface
      ? qualityDetail * wrappedNdotL *
            (fibreRelief * (0.18f + 0.14f * velvet) + velvet * 0.08f)
      : 0.0f;
  return max(
      environmentDiffuse + directDiffuse + neutralFill +
          environmentSpecular +
          subsurfaceTint * subsurfaceFill +
          float3(sourceSpecular, sourceSpecular, sourceSpecular) +
          albedo * fibreSheen,
      float3(0.0f, 0.0f, 0.0f));
}

float3 decodePackedProbeTexel(Texture2D probeTexture, int2 texel) {
  float4 packedRg = probeTexture.Load(int3(texel, 0));
  float4 packedBa = probeTexture.Load(int3(texel + int2(1, 0), 0));
  uint4 rg = (uint4)round(saturate(packedRg) * 255.0f);
  uint4 ba = (uint4)round(saturate(packedBa) * 255.0f);
  return float3(
      f16tof32(rg.r | (rg.g << 8u)),
      f16tof32(rg.b | (rg.a << 8u)),
      f16tof32(ba.r | (ba.g << 8u)));
}

float3 samplePackedSpecularProbe(Texture2D probeTexture,
                                  float3 direction,
                                  float roughness) {
  uint atlasWidth = 0u;
  uint atlasHeight = 0u;
  probeTexture.GetDimensions(atlasWidth, atlasHeight);
  if (atlasWidth != atlasHeight * 3u ||
      atlasHeight < 2u || (atlasHeight & 1u) != 0u) {
    return sampleNeutralEnvironment(direction, roughness);
  }
  int faceSize = (int)(atlasHeight / 2u);
  float3 d = safeNormalize(direction, float3(0.0f, 0.0f, 1.0f));
  float3 a = abs(d);
  int face;
  float2 faceUv;
  if (a.x >= a.y && a.x >= a.z) {
    if (d.x >= 0.0f) {
      face = 0;
      faceUv = float2(-d.z, -d.y) / a.x;
    } else {
      face = 1;
      faceUv = float2(d.z, -d.y) / a.x;
    }
  } else if (a.y >= a.z) {
    if (d.y >= 0.0f) {
      face = 2;
      faceUv = float2(d.x, d.z) / a.y;
    } else {
      face = 3;
      faceUv = float2(d.x, -d.z) / a.y;
    }
  } else if (d.z >= 0.0f) {
    face = 4;
    faceUv = float2(d.x, -d.y) / a.z;
  } else {
    face = 5;
    faceUv = float2(-d.x, -d.y) / a.z;
  }
  float2 p = saturate(faceUv * 0.5f + 0.5f) * (float)faceSize - 0.5f;
  int2 lo = clamp(
      (int2)floor(p), int2(0, 0), int2(faceSize - 1, faceSize - 1));
  int2 hi = min(lo + int2(1, 1), int2(faceSize - 1, faceSize - 1));
  float2 blend = frac(p);
  int2 origin = int2(
      (face % 3) * faceSize * 2,
      (face / 3) * faceSize);
  float3 c00 = decodePackedProbeTexel(
      probeTexture, origin + int2(lo.x * 2, lo.y));
  float3 c10 = decodePackedProbeTexel(
      probeTexture, origin + int2(hi.x * 2, lo.y));
  float3 c01 = decodePackedProbeTexel(
      probeTexture, origin + int2(lo.x * 2, hi.y));
  float3 c11 = decodePackedProbeTexel(
      probeTexture, origin + int2(hi.x * 2, hi.y));
  return lerp(
      lerp(c00, c10, blend.x),
      lerp(c01, c11, blend.x),
      blend.y);
}

float3 sampleStageReflectionProbeMip(Texture2D probeTexture,
                                       float3 direction,
                                       int baseFaceSize,
                                       int mipLevel) {
  int mipSize = max(baseFaceSize >> mipLevel, 1);
  float3 d = safeNormalize(direction, float3(0.0f, 0.0f, 1.0f));
  float3 a = abs(d);
  int face;
  float2 faceUv;
  if (a.x >= a.y && a.x >= a.z) {
    if (d.x >= 0.0f) {
      face = 0;
      faceUv = float2(-d.z, -d.y) / a.x;
    } else {
      face = 1;
      faceUv = float2(d.z, -d.y) / a.x;
    }
  } else if (a.y >= a.z) {
    if (d.y >= 0.0f) {
      face = 2;
      faceUv = float2(d.x, d.z) / a.y;
    } else {
      face = 3;
      faceUv = float2(d.x, -d.z) / a.y;
    }
  } else if (d.z >= 0.0f) {
    face = 4;
    faceUv = float2(d.x, -d.y) / a.z;
  } else {
    face = 5;
    faceUv = float2(-d.x, -d.y) / a.z;
  }
  float2 p = saturate(faceUv * 0.5f + 0.5f) *
      (float)mipSize - 0.5f;
  int2 lo = clamp(
      (int2)floor(p), int2(0, 0), int2(mipSize - 1, mipSize - 1));
  int2 hi = min(lo + int2(1, 1), int2(mipSize - 1, mipSize - 1));
  float2 blend = frac(p);
  int mipStripY = baseFaceSize * 4 - mipSize * 4;
  int2 origin = int2(
      (face % 3) * mipSize * 2,
      mipStripY + (face / 3) * mipSize);
  float3 c00 = decodePackedProbeTexel(
      probeTexture, origin + int2(lo.x * 2, lo.y));
  float3 c10 = decodePackedProbeTexel(
      probeTexture, origin + int2(hi.x * 2, lo.y));
  float3 c01 = decodePackedProbeTexel(
      probeTexture, origin + int2(lo.x * 2, hi.y));
  float3 c11 = decodePackedProbeTexel(
      probeTexture, origin + int2(hi.x * 2, hi.y));
  return lerp(
      lerp(c00, c10, blend.x),
      lerp(c01, c11, blend.x),
      blend.y);
}

float3 sampleStageReflectionProbe(Texture2D probeTexture,
                                    float3 direction,
                                    float sourceLod,
                                    float fallbackRoughness) {
  uint atlasWidth = 0u;
  uint atlasHeight = 0u;
  probeTexture.GetDimensions(atlasWidth, atlasHeight);
  if (atlasWidth < 6u || atlasWidth % 6u != 0u) {
    return sampleNeutralEnvironment(direction, fallbackRoughness);
  }
  uint faceSize = atlasWidth / 6u;
  if (atlasHeight != faceSize * 4u - 2u ||
      (faceSize & (faceSize - 1u)) != 0u) {
    return sampleNeutralEnvironment(direction, fallbackRoughness);
  }
  int maxMip = (int)round(log2((float)faceSize));
  float lod = clamp(sourceLod, 0.0f, (float)maxMip);
  int lo = (int)floor(lod);
  int hi = min(lo + 1, maxMip);
  return lerp(
      sampleStageReflectionProbeMip(
          probeTexture, direction, (int)faceSize, lo),
      sampleStageReflectionProbeMip(
          probeTexture, direction, (int)faceSize, hi),
      frac(lod));
}

float3 viewAngleLayerBase(float3 baseMap) {
  float3 tinted = max(baseMap, float3(0.0f, 0.0f, 0.0f)) *
      max(uProjectedShadowRowX.rgb, float3(0.0f, 0.0f, 0.0f));
  float luminance = dot(tinted, float3(0.299f, 0.587f, 0.114f));
  return max(
      lerp(luminance.xxx, tinted, max(uLightProjectionUvRowU.x, 0.0f)),
      float3(0.0f, 0.0f, 0.0f));
}

float3 applyViewAngleLayerLayer(PSIn i,
                                      float3 litBase,
                                      float3 primaryColor,
                                      float3 normal,
                                      float2 sampleUv,
                                      float2 uvDx,
                                      float2 uvDy,
                                      bool useOcclusionTexture,
                                      bool useLayerTexture,
                                      float metallicFactor,
                                      float roughnessFactor,
                                      float occlusionStrength,
                                      float3 cameraPos,
                                      float3 cameraForwardPacked) {
  float3 cameraForward = safeNormalize(
      cameraForwardPacked,
      float3(0.0f, 0.0f, -1.0f));
  float3 viewDirection = safeNormalize(
      cameraPos - i.worldPos,
      -cameraForward);
  float nDotV = saturate(dot(normal, viewDirection));
  float angleTerm = 1.0f - max(
      nDotV - saturate(uProjectedShadowRowZ.w),
      0.0f);
  float fresnelAlpha = lerp(
      saturate(uProjectedShadowRowZ.y),
      saturate(uProjectedShadowRowZ.z),
      pow(saturate(angleTerm), 5.0f));
  float3 layerMap = useLayerTexture
      ? sampleTextureWithWrap(
            gEmissiveTex,
            sampleUv,
            uvDx,
            uvDy,
            uWrapS,
            uWrapT).rgb
      : float3(0.0f, 0.0f, 0.0f);
  float ao = useOcclusionTexture
      ? lerp(
            1.0f,
            sampleTextureWithWrap(
                gOcclusionTex,
                sampleUv,
                uvDx,
                uvDy,
                uWrapS,
                uWrapT).r,
            saturate(occlusionStrength))
      : 1.0f;
  float3 layerColor = layerMap *
      max(uProjectedShadowRowY.rgb, float3(0.0f, 0.0f, 0.0f)) *
      ao * max(uLightProjectionUvRowU.y, 0.0f) *
      (1.0f - fresnelAlpha);
  float3 environmentRadiance = samplePackedSpecularProbe(
      gEnvTex,
      reflect(-viewDirection, normal),
      clamp(roughnessFactor, 0.04f, 1.0f));
  float3 f0 = lerp(
      float3(0.04f, 0.04f, 0.04f),
      saturate(primaryColor),
      saturate(metallicFactor));
  float3 localProbe = environmentRadiance *
      fresnelSchlick(nDotV, f0) *
      max(uProjectedShadowRowZ.x, 0.0f) * ao *
      __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
  return max(
      litBase + layerColor + localProbe,
      float3(0.0f, 0.0f, 0.0f));
}

float3 applyLayeredEyeCoat(PSIn i,
                               float3 linearColor,
                               float3 n,
                               float3 cameraPos,
                               float3 cameraForwardPacked,
                               float3 cameraTarget) {
  float3 camForward = safeNormalize(
      cameraForwardPacked,
      normalize(float3(0.0f, -0.6139406f, -0.7893522f)));
  float3 camRight = cross(camForward, float3(0.0f, 1.0f, 0.0f));
  if (dot(camRight, camRight) < 1e-6f) {
    camRight = cross(camForward, float3(0.0f, 0.0f, 1.0f));
  }
  camRight = safeNormalize(camRight, float3(1.0f, 0.0f, 0.0f));
  float3 v = safeNormalize(cameraPos - i.worldPos, -camForward);
  float3 lightPos = cameraPos + camRight * 0.5f - camForward * 0.8660254f;
  float3 l = safeNormalize(lightPos - cameraTarget, float3(0.45f, 0.86f, 0.24f));
  float3 h = safeNormalize(v + l, n);
  float ndv = max(dot(n, v), 0.0f);
  float ndl = max(dot(n, l), 0.0f);
  float ndh = max(dot(n, h), 0.0f);
  float vdh = max(dot(v, h), 0.0f);
  // Mode 28/30 uses the projected-shadow rows as a lossless PS-only material
  // transport. A negative metallic marker identifies PLA's plain Eye family,
  // which has authored mask/emission but no EyeClearCoat lobe.
  float clearCoatMetallic = uProjectedShadowRowY.w;
  if (clearCoatMetallic < -0.5f) return linearColor;
  float roughness = clamp(uProjectedShadowRowX.x, 0.04f, 1.0f);
  float3 clearCoatBaseColor = max(
      uProjectedShadowRowY.xyz,
      float3(0.0f, 0.0f, 0.0f));
  float3 clearCoatF0 = lerp(
      float3(0.04f, 0.04f, 0.04f),
      clearCoatBaseColor,
      saturate(clearCoatMetallic));
  float distribution = distributionGGX(ndh, roughness);
  float geometry = geometrySchlickGGX(ndv, roughness) *
      geometrySchlickGGX(ndl, roughness);
  float3 fresnel = fresnelSchlick(vdh, clearCoatF0);
  float3 direct = distribution * geometry * fresnel /
      max(4.0f * ndv * ndl, 1e-4f) *
      (__PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265f) * ndl;
  float3 reflection = reflect(-v, n);
  float3 environment = sampleNeutralEnvironment(reflection, roughness) *
      fresnelSchlickRoughness(ndv, clearCoatF0, roughness) *
      __PHLOSION_PBR_SPECULAR_IBL_SCALE__;
  float3 coatLighting = direct + environment;
  float coatPeak = max(
      coatLighting.x,
      max(coatLighting.y, coatLighting.z));
  float3 boundedCoat = coatLighting / (1.0f + coatPeak);
  const float sceneCoatBridge = 0.20f;
  float3 result = linearColor *
      (float3(1.0f, 1.0f, 1.0f) - fresnel * 0.18f) +
      boundedCoat * sceneCoatBridge;

  // The compiled highlight branch proves these material inputs and classifies
  // fp_c8[96] as an optional point-light position/enable field. Its bound
  // source value and light energy are unavailable. Evaluate the authored
  // GGX/tint/energy portion against Phlosion's isolated viewer-light bridge;
  // the bounded bridge prevents unknown source exposure from becoming an
  // invented full-eye emissive wash.
  float3 highlightEmission = max(
      uProjectedShadowRowZ.xyz,
      float3(0.0f, 0.0f, 0.0f));
  float highlightEnergy = max(
      highlightEmission.x,
      max(highlightEmission.y, highlightEmission.z));
  float highlightEnabled = saturate(uProjectedShadowRowX.w);
  if (highlightEnabled > 0.0f && highlightEnergy > 1e-5f) {
    float highlightRoughness = clamp(uProjectedShadowRowX.y, 0.04f, 1.0f);
    float highlightMetallic = saturate(uProjectedShadowRowX.z);
    float3 highlightTint = highlightEmission / highlightEnergy;
    float3 highlightF0 = lerp(
        float3(0.04f, 0.04f, 0.04f),
        highlightTint,
        highlightMetallic);
    float highlightDistribution = distributionGGX(ndh, highlightRoughness);
    float highlightGeometry = geometrySchlickGGX(ndv, highlightRoughness) *
        geometrySchlickGGX(ndl, highlightRoughness);
    float3 highlightFresnel = fresnelSchlick(vdh, highlightF0);
    float3 highlightDirect = highlightDistribution * highlightGeometry *
        highlightFresnel / max(4.0f * ndv * ndl, 1e-4f) *
        (__PHLOSION_PBR_DIRECT_INTENSITY__ * 3.14159265f) * ndl;
    float highlightPeak = max(
        highlightDirect.x,
        max(highlightDirect.y, highlightDirect.z));
    float3 boundedHighlight = highlightDirect / (1.0f + highlightPeak);
    const float sceneHighlightBridge = 0.12f;
    result += boundedHighlight *
        (sceneHighlightBridge * highlightEnabled *
         (1.0f - exp(-highlightEnergy)));
  }
  return max(result, float3(0.0f, 0.0f, 0.0f));
}

float3 applyCharacterInking(PSIn i, float3 linearColor, float3 n, float3 cameraPos, float3 cameraForwardPacked) {
  return linearColor;
}
