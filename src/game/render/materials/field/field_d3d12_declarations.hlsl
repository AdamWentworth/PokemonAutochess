float4 sampleGroundTexture(Texture2D tex, float2 uv) {
  // The shared world sampler contributes -0.35; this restores the source
  // FieldGroundShader01 effective -2.0 LOD bias.
  return tex.SampleBias(gSampRR, uv, -1.65f);
}
float4 sampleFieldTreeTexture(Texture2D tex, float2 uv) {
  // The shared sampler contributes -0.35; the source material requests zero.
  return tex.SampleBias(gSampRR, uv, 0.35f);
}
float4 sampleFieldGrassRepeat(
    Texture2D tex,
    float2 uv,
    float sourceMipBias) {
  // The shared sampler contributes -0.35; preserve the source material bias.
  return tex.SampleBias(gSampRR, uv, sourceMipBias + 0.35f);
}
float4 sampleFieldGrassClamp(
    Texture2D tex,
    float2 uv,
    float sourceMipBias) {
  return tex.SampleBias(gSampCC, uv, sourceMipBias + 0.35f);
}
float2 fieldCloudTextureUv(float3 worldPosition);
float evaluateFieldProjectedCloud(float3 worldPosition);
float evaluateFieldProjectedShadow(float3 worldPosition);
float evaluateFieldProjectedLighting(
    float toon,
    float3 worldPosition);
float3 applyGroundCliffSharedLighting(
    float3 surface,
    float3 worldPosition) {
  const float3 shadowColor = float3(0.235f, 0.361f, 0.391f);
  float light =
      evaluateFieldProjectedLighting(1.0f, worldPosition);
  return lerp(shadowColor, 1.0f.xxx, light) * surface;
}
float3 evaluateFieldGroundSurface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 blendUv = float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float2 uv2 = float2(i.sourceUv2.x, 1.0f - i.sourceUv2.y);
  float4 ground01 = sampleGroundTexture(gTex, uv0);
  float4 ground02 = sampleGroundTexture(gNormalTex, uv0);
  float4 grass02 = sampleGroundTexture(gMetallicRoughnessTex, uv0);
  float4 grass01 = sampleGroundTexture(gOcclusionTex, uv0);
  float blend = saturate(sampleGroundTexture(gEnvTex, blendUv).r);
  float4 grassMask = sampleGroundTexture(gEmissiveTex, uv2);
  float3 ground = lerp(ground01.rgb, ground02.rgb, blend);
  float3 grass = lerp(grass02.rgb, grass01.rgb, blend);
  float3 surface = lerp(ground, grass, saturate(grassMask.a));
  float4 authoredVertexColor =
      i.col * float4(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB,
          uVertexColorMulA);
  const float3 alphaLight =
      max(float3(uMaterialRect0W, uMaterialRect0H, uMaterialRect1U),
          float3(0.0f, 0.0f, 0.0f));
  float3 sourceSurface =
      grassMask.rgb * authoredVertexColor.rgb * surface +
      alphaLight * (1.0f - saturate(authoredVertexColor.a));
  return applyGroundCliffSharedLighting(sourceSurface, i.worldPos);
}
float3 evaluateFieldCliffSurface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 blendUv = float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float2 uv1 = float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float2 uv2 = float2(i.sourceUv2.x, 1.0f - i.sourceUv2.y);
  float4 cliffTex = sampleGroundTexture(gTex, uv1);
  float4 ground02 = sampleGroundTexture(gNormalTex, uv0);
  float4 ground01 = sampleGroundTexture(gMetallicRoughnessTex, uv0);
  float blend =
      saturate(sampleGroundTexture(gOcclusionTex, blendUv).r);
  float4 borderTex = sampleGroundTexture(gEmissiveTex, uv2);
  float3 normal = normalize(i.worldNormal);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialAtlasWidth;
  float rimMax = uMaterialAtlasHeight;
  float rimStrength = uMaterialRect0U;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate(
            ((1.0f - dot(normal, viewDirection)) - rimMin) /
            rimSpan) *
            rimStrength
      : 0.0f;
  float3 rimColor =
      max(float3(uMaterialRect0W, uMaterialRect0H, uMaterialRect1U),
          float3(0.0f, 0.0f, 0.0f));
  float3 cliff = cliffTex.rgb + rimColor * rim * cliffTex.a;
  float3 grass = lerp(ground02.rgb, ground01.rgb, blend);
  float3 surface = lerp(cliff, grass, saturate(borderTex.a));
  float4 authoredVertexColor =
      i.col * float4(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB,
          uVertexColorMulA);
  float3 sourceSurface =
      borderTex.rgb * authoredVertexColor.rgb * surface;
  return applyGroundCliffSharedLighting(sourceSurface, i.worldPos);
}


float3 foliageColorBalance(
    float3 color,
    float hueShift,
    float saturation,
    float value) {
  float3 hsv = rgbToHsv(max(color, 0.0f.xxx));
  hsv.x = frac(hsv.x + hueShift);
  hsv.y = saturate(hsv.y * saturation);
  hsv.z *= value;
  return hsvToRgb(hsv);
}
float foliageAcceptedLightCoordinate(float3 normal) {
  const float3 acceptedLightDirection =
      float3(0.32f, -0.42f, 0.85f);
  float sourceDot =
      dot(normalize(normal), acceptedLightDirection);
  return lerp(
      0.12f,
      0.96f,
      saturate((sourceDot + 0.15f) / 1.0f));
}
float3 foliageProjectionCompensation(
    float3 shadowColor,
    float3 worldPosition) {
  float cloud =
      evaluateFieldProjectedLighting(1.0f, worldPosition);
  float3 projectedLighting =
      lerp(max(shadowColor, 0.0f.xxx), 1.0f.xxx, cloud);
  return lerp(1.0f.xxx, projectedLighting, 0.25f);
}
float3 foliageColorize(
    float3 baseColor,
    float3 paletteColor,
    float factor) {
  const float3 luminanceWeights =
      float3(0.2126f, 0.7152f, 0.0722f);
  float baseLuminance =
      dot(max(baseColor, 0.0f.xxx), luminanceWeights);
  float paletteLuminance = max(
      dot(max(paletteColor, 0.0f.xxx), luminanceWeights),
      0.0001f);
  float3 paletteAtBaseLuminance =
      paletteColor * (baseLuminance / paletteLuminance);
  return lerp(baseColor, paletteAtBaseLuminance, saturate(factor));
}
float3 foliageAcceptedDisplayTransform(float3 color) {
  float3 exposed = max(color, 0.0f.xxx) * 0.72f;
  float3 mapped = saturate(
      (exposed * (2.51f * exposed + 0.03f)) /
      (exposed * (2.43f * exposed + 0.59f) + 0.14f));
  float luminance =
      dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
  float highlight = smoothstep(0.35f, 0.85f, luminance);
  float saturationRetention = lerp(0.9f, 0.55f, highlight);
  return lerp(luminance.xxx, mapped, saturationRetention);
}
float4 evaluateTunedCanopySurface(
    PSIn i,
    float hueShift,
    float saturation,
    float value) {
  float2 uv0 = i.uv;
  float4 texture01 = sampleFieldTreeTexture(gTex, uv0);
  if (texture01.a <= saturate(uAlphaCutoff)) discard;
  float lightCoordinate =
      foliageAcceptedLightCoordinate(i.worldNormal);
  float3 texture02 = sampleFieldTreeTexture(
      gNormalTex,
      float2(0.5f, 1.0f - lightCoordinate)).rgb;
  float texture03 =
      sampleFieldTreeTexture(gMetallicRoughnessTex, uv0).r;
  float3 acceptedLocalSurface =
      saturate(texture01.rgb + texture02 * texture03);
  float3 acceptedColor = foliageColorBalance(
      acceptedLocalSurface,
      hueShift,
      saturation,
      value);
  float3 shadowColor = max(
      float3(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB),
      0.0f.xxx);
  float3 finalColor =
      acceptedColor *
      foliageProjectionCompensation(
          shadowColor,
          i.worldPos);
  return float4(
      foliageAcceptedDisplayTransform(finalColor),
      texture01.a);
}
float4 evaluateTunedLayeredFoliageSurface(
    PSIn i,
    float hueShift,
    float saturation,
    float value,
    bool darkConifer) {
  float2 uv0 = i.uv;
  float4 texture01 = sampleFieldTreeTexture(gTex, uv0);
  if (texture01.a <= saturate(uAlphaCutoff)) discard;
  float3 texture02 =
      sampleFieldTreeTexture(gNormalTex, uv0).rgb;
  float lightCoordinate =
      foliageAcceptedLightCoordinate(i.worldNormal);
  float lightToon = gEmissiveTex.SampleBias(
      gSampCC,
      float2(lightCoordinate, 0.5f),
      0.35f).r;
  float3 directionalLightColor =
      float3(
          uMaterialRect1V,
          uMaterialRect1W,
          uMaterialRect1H);
  float3 acceptedLocalSurface = saturate(
      texture01.rgb +
      texture02 * directionalLightColor * saturate(lightToon));
  if (darkConifer) {
    float3 greenColor =
        float3(
            uVertexColorMulR,
            uVertexColorMulG,
            uVertexColorMulB);
    float inverseShade =
        1.0f -
        dot(
            saturate(texture02),
            float3(0.2126f, 0.7152f, 0.0722f));
    acceptedLocalSurface = foliageColorize(
        acceptedLocalSurface,
        greenColor,
        inverseShade);
  }
  float3 acceptedColor = foliageColorBalance(
      acceptedLocalSurface,
      hueShift,
      saturation,
      value);
  float3 shadowColor =
      float3(
          uMaterialRect0W,
          uMaterialRect0H,
          uMaterialRect1U);
  float3 finalColor =
      acceptedColor *
      foliageProjectionCompensation(
          shadowColor,
          i.worldPos);
  float3 displayedColor =
      foliageAcceptedDisplayTransform(finalColor);
  return float4(
      displayedColor,
      texture01.a);
}
float4 evaluateCanopySurface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 = float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float4 texture01 = sampleFieldTreeTexture(gTex, uv0);
  if (texture01.a <= saturate(uAlphaCutoff)) discard;
  float3 texture02 =
      sampleFieldTreeTexture(gNormalTex, uv1).rgb;
  float texture03 =
      sampleFieldTreeTexture(gMetallicRoughnessTex, uv0).r;
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      gOcclusionTex.SampleBias(
          gSampCC,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          0.35f).r);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialTimeSec;
  float rimMax = uMaterialFlags;
  float rimStrength = uMaterialAtlasWidth;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate(
            ((1.0f - dot(normal, viewDirection)) - rimMin) /
            rimSpan) *
            rimStrength
      : 0.0f;
  float lightGate =
      1.0f -
      saturate((1.0f - normalDotLight) * 12.7408008575f);
  float3 secondaryDirection =
      lerp(
          viewDirection,
          -sourceSunRay,
          uMaterialFlipbook0Fps);
  float secondaryMin = uMaterialFlipbook1Frames;
  float secondaryMax = uMaterialFlipbook1Fps;
  float secondarySpan =
      max(secondaryMax, secondaryMin) - secondaryMin;
  float secondaryCoordinate =
      saturate(1.0f - dot(normal, secondaryDirection));
  float secondary = secondarySpan > 0.0f
      ? saturate(
            (secondaryCoordinate - secondaryMin) /
            secondarySpan)
      : 0.0f;
  float3 shadowColor =
      max(float3(
              uVertexColorMulR,
              uVertexColorMulG,
              uVertexColorMulB),
          float3(0.0f, 0.0f, 0.0f));
  float3 rimColor =
      max(float3(
              uMaterialAtlasHeight,
              uMaterialRect0U,
              uMaterialRect0V),
          float3(0.0f, 0.0f, 0.0f));
  float3 rimColor02 =
      max(float3(
              uMaterialRect0W,
              uMaterialRect0H,
              uMaterialRect1U),
          float3(0.0f, 0.0f, 0.0f));
  float3 surface =
      texture01.rgb +
      texture02 * rim * rimColor +
      max(
          float3(
              uMaterialFlipbook0Cols,
              uMaterialFlipbook0Rows,
              uMaterialFlipbook0Frames),
          0.0f.xxx) *
          (1.0f - secondary) +
      texture03 * lightGate * rimColor02;
  return float4(
      lerp(
          shadowColor,
          1.0f.xxx,
          evaluateFieldProjectedLighting(toon, i.worldPos)) *
          surface,
      texture01.a);
}

float4 evaluateLayeredFoliageSurface(
    PSIn i,
    bool useProjectedCloud,
    bool useCanonicalTextureUv) {
  // Exact modes retain the decoded 1-V operation. Reviewed BuildModel
  // grass02 uses canonical presentation UVs, matching its exact Blender
  // checkpoint's direct model-texcoord links.
  float2 uv0 = useCanonicalTextureUv
      ? i.uv
      : float2(i.uv.x, 1.0f - i.uv.y);
  float4 texture01 = sampleFieldTreeTexture(gTex, uv0);
  if (texture01.a <= saturate(uAlphaCutoff)) discard;
  float3 texture02 =
      sampleFieldTreeTexture(gNormalTex, uv0).rgb;
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float2 toonUv =
      float2(toonCoordinate, 1.0f - toonCoordinate);
  float toon = saturate(
      gOcclusionTex.SampleBias(gSampCC, toonUv, 0.35f).r);
  float lightToon = saturate(
      gEmissiveTex.SampleBias(gSampCC, toonUv, 0.35f).r);
  float3 cameraPos =
      float3(
          uMaterialFlipbook1Cols,
          uMaterialFlipbook1Rows,
          uMaterialFlipbook1Frames);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float edge = saturate(1.0f - dot(normal, viewDirection));
  float rimMin = uMaterialTimeSec;
  float rimMax = uMaterialFlags;
  float rimStrength = uMaterialAtlasWidth;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate((edge - rimMin) / rimSpan) * rimStrength
      : 0.0f;
  float directional = saturate(edge * (5.0f / 3.0f));
  float3 greenColor =
      float3(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB);
  float3 shadowColor =
      float3(
          uMaterialRect0W,
          uMaterialRect0H,
          uMaterialRect1U);
  float3 rimColor =
      float3(
          uMaterialAtlasHeight,
          uMaterialRect0U,
          uMaterialRect0V);
  float3 directionalLightColor =
      float3(
          uMaterialRect1V,
          uMaterialRect1W,
          uMaterialRect1H);
  float3 rimColor02 =
      float3(
          uMaterialFlipbook0Cols,
          uMaterialFlipbook0Rows,
          uMaterialFlipbook0Frames);
  float3 secondary =
      rim * rimColor +
      (1.0f - directional) * rimColor02 +
      lightToon * directionalLightColor;
  float3 surface = texture01.rgb + texture02 * secondary;
  float3 authored = surface * i.col.rgb;
  float3 tinted = lerp(greenColor, authored, saturate(i.col.a));
  // The shared projected-depth PCF is held at one until its source matrix is
  // represented. Only pasted__pasted__tree15 samples the recovered cloud map.
  float light = useProjectedCloud
      ? evaluateFieldProjectedLighting(toon, i.worldPos)
      : toon * evaluateFieldProjectedShadow(i.worldPos);
  float3 lighting = lerp(shadowColor, 1.0f.xxx, light);
  return float4(lighting * tinted, texture01.a);
}



float4 evaluateFieldGrassSurface(PSIn i, bool withRim) {
  float sourceMipBias = uMaterialFlipbook0Fps;
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 =
      float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float2 blendUv =
      float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  bool floorFoliageCard = !withRim && i.sourceUv2.x < -2048.0f;
  float3 textureMap02 =
      sampleFieldGrassRepeat(
          gNormalTex,
          uv0,
          sourceMipBias).rgb;
  float3 textureMap01 =
      sampleFieldGrassRepeat(
          gTex,
          uv0,
          sourceMipBias).rgb;
  float4 greenHikari =
      sampleFieldGrassRepeat(
          gMetallicRoughnessTex,
          floorFoliageCard
              ? float2(i.sourceUv1.x, i.sourceUv1.y)
              : uv1,
          sourceMipBias);
  if (floorFoliageCard) {
    if (greenHikari.r >= saturate(uAlphaCutoff)) discard;
  } else if (greenHikari.a <= saturate(uAlphaCutoff)) {
    discard;
  }
  float greenBlend = saturate(
      sampleFieldGrassRepeat(
          gOcclusionTex,
          blendUv,
          sourceMipBias).r);
  float highlight = saturate(
      sampleFieldGrassRepeat(
          gEmissiveTex,
          uv1,
          sourceMipBias).r);
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      sampleFieldGrassClamp(
          gEnvTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          sourceMipBias).r);
  float3 sourceColor =
      float3(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB);
  float3 decoration =
      lerp(textureMap02, textureMap01, greenBlend) +
      sourceColor * (1.0f - highlight);
  float3 authoredColor = i.col.rgb;
  if (!withRim && sourceMipBias > -1.0f &&
      normalize(i.worldNormal).y > 0.9f) {
    authoredColor =
        float3(0.180392161f, 0.482352942f, 0.431372553f);
  }
  float3 surface =
      decoration * greenHikari.rgb * authoredColor;
  if (withRim) {
    float3 cameraPos =
        float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
    float3 viewDirection = normalize(cameraPos - i.worldPos);
    float edge = saturate(1.0f - dot(normal, viewDirection));
    float rimMin = uMaterialTimeSec;
    float rimMax = uMaterialFlags;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0f
        ? saturate((edge - rimMin) / rimSpan) *
              uMaterialAtlasWidth
        : 0.0f;
    float3 rimColor =
        float3(
            uMaterialAtlasHeight,
            uMaterialRect0U,
            uMaterialRect0V);
    surface += rimColor * rim;
  }
  float3 shadowColor =
      float3(
          uMaterialRect0W,
          uMaterialRect0H,
          uMaterialRect1U);
  float3 result =
      lerp(
          shadowColor,
          1.0f.xxx,
          evaluateFieldProjectedLighting(toon, i.worldPos)) *
      surface;
  float alpha = greenHikari.a;
  if (!withRim) {
    float3 onGameColor =
        float3(
            uMaterialTimeSec,
            uMaterialFlags,
            uMaterialAtlasWidth);
    float onGameValue = saturate(uMaterialAtlasHeight);
    result *= lerp(1.0f.xxx, onGameColor, onGameValue);
    alpha *= saturate(uMaterialRect0U);
  }
  return float4(result, alpha);
}

float2 fieldCloudTextureUv(float3 worldPosition) {
  float4 position = float4(worldPosition, 1.0f);
  float sourceU = dot(position, uLightProjectionUvRowU);
  float sourceV = dot(position, uLightProjectionUvRowV);
  return float2(sourceU, 1.0f - sourceV);
}

float evaluateFieldProjectedCloud(float3 worldPosition) {
  return saturate(
      sampleFieldGrassRepeat(
          gLightProjectionTex,
          fieldCloudTextureUv(worldPosition),
          0.0f).r);
}

float evaluateFieldProjectedShadow(float3 worldPosition) {
  if (uProjectedShadowEnabled < 0.5f) return 1.0f;
  float4 position = float4(worldPosition, 1.0f);
  float4 shadowClip = float4(
      dot(uProjectedShadowRowX, position),
      dot(uProjectedShadowRowY, position),
      dot(uProjectedShadowRowZ, position),
      1.0f);
  float3 shadowNdc = shadowClip.xyz / shadowClip.w;
  float2 shadowUv = shadowNdc.xy * 0.5f + 0.5f;
  if (any(shadowUv < 0.0f.xx) || any(shadowUv > 1.0f.xx)) {
    return 1.0f;
  }
  float reference =
      shadowNdc.z * 0.5f + 0.5f -
      uProjectedShadowBias / shadowClip.w;
  static const float2 poisson[10] = {
      float2(-0.8405f, -0.0740f),
      float2(-0.3262f, -0.4058f),
      float2(-0.2034f,  0.4573f),
      float2(-0.6985f,  0.6206f),
      float2( 0.9635f, -0.1944f),
      float2( 0.4734f, -0.4800f),
      float2( 0.5195f,  0.7670f),
      float2( 0.1855f, -0.8945f),
      float2( 0.5074f,  0.0650f),
      float2(-0.3219f,  0.5954f)};
  uint width = 1u;
  uint height = 1u;
  gProjectedShadowTex.GetDimensions(width, height);
  int2 extent = int2(max(width, 1u), max(height, 1u));
  float projectedShadow = 0.0f;
  [loop]
  for (int tap = 0; tap < 10; ++tap) {
    float2 tapUv =
        shadowUv + poisson[tap] *
        (0.0004f * uProjectedShadowSamplingScale);
    float2 texelPosition = tapUv * float2(extent) - 0.5f.xx;
    int2 baseTexel = int2(floor(texelPosition));
    float2 blend = frac(texelPosition);
    float comparisons[4];
    [loop]
    for (int corner = 0; corner < 4; ++corner) {
      int2 texel = baseTexel + int2(corner & 1, (corner >> 1) & 1);
      float storedDepth = 1.0f;
      if (texel.x >= 0 && texel.y >= 0 &&
          texel.x < extent.x && texel.y < extent.y) {
        float3 packed =
            gProjectedShadowTex.Load(int3(texel, 0)).rgb * 255.0f;
        storedDepth =
            dot(packed, float3(65536.0f, 256.0f, 1.0f)) /
            16777215.0f;
      }
      comparisons[corner] = reference <= storedDepth ? 1.0f : 0.0f;
    }
    float row0 = lerp(comparisons[0], comparisons[1], blend.x);
    float row1 = lerp(comparisons[2], comparisons[3], blend.x);
    projectedShadow += lerp(row0, row1, blend.y) * 0.1f;
  }
  return projectedShadow;
}

float evaluateFieldProjectedLighting(
    float toon,
    float3 worldPosition) {
  return min(
      saturate(toon) *
          evaluateFieldProjectedShadow(worldPosition),
      evaluateFieldProjectedCloud(worldPosition));
}

float4 evaluateGroundCoverSurface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 = float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float4 texture01 =
      sampleFieldGrassRepeat(gTex, uv0, 0.0f);
  float4 texture02 =
      sampleFieldGrassRepeat(gNormalTex, uv1, 0.0f);
  float texture03 = saturate(
      sampleFieldGrassRepeat(
          gMetallicRoughnessTex,
          uv1,
          0.0f).r);
  float4 base = lerp(texture01, texture02, texture03);
  float alpha =
      base.a * i.col.a * saturate(uMaterialTimeSec) *
      saturate(uMaterialAtlasWidth);
  if (alpha <= saturate(uAlphaCutoff)) discard;
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      sampleFieldGrassClamp(
          gOcclusionTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          0.0f).r);
  float projectedLight =
      evaluateFieldProjectedLighting(toon, i.worldPos);
  float3 shadowColor =
      float3(uVertexColorMulR, uVertexColorMulG, uVertexColorMulB);
  float3 onGameColor =
      float3(uMaterialAtlasHeight, uMaterialRect0U, uMaterialRect0V);
  float3 surface =
      base.rgb * i.col.rgb *
      lerp(1.0f.xxx, onGameColor, saturate(uMaterialFlags));
  return float4(
      lerp(shadowColor, 1.0f.xxx, projectedLight) * surface,
      alpha);
}

float4 evaluateLayeredGroundCoverSurface(PSIn i) {
  float2 maskUv =
      float2(
          i.uv.x + uMaterialRect0V,
          1.0f - (i.uv.y + uMaterialRect0W));
  float2 uv1Primary =
      float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float2 uv1Secondary =
      float2(i.sourceUv1.x + 1.0f, 0.5f - i.sourceUv1.y);
  float lightLine =
      saturate(
          sampleFieldGrassRepeat(gNormalTex, maskUv, 0.0f).r);
  float4 alpha01Primary =
      sampleFieldGrassRepeat(gTex, uv1Primary, 0.0f);
  float4 alpha01Secondary =
      sampleFieldGrassRepeat(gTex, uv1Secondary, 0.0f);
  float4 base =
      lerp(alpha01Primary, alpha01Secondary, lightLine);
  float alpha = base.a * i.col.a * saturate(uMaterialFlags);
  if (alpha <= saturate(uAlphaCutoff)) discard;
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 blendUv =
      float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float greenBlend = saturate(
      sampleFieldGrassRepeat(gEmissiveTex, blendUv, 0.0f).r);
  float3 textureMap01 =
      sampleFieldGrassRepeat(
          gMetallicRoughnessTex,
          uv0,
          0.0f).rgb;
  float3 textureMap02 =
      sampleFieldGrassRepeat(gOcclusionTex, uv0, 0.0f).rgb;
  float3 decoration = lerp(textureMap02, textureMap01, greenBlend);
  float projectedLight =
      evaluateFieldProjectedLighting(1.0f, i.worldPos);
  float3 shadowColor =
      float3(uVertexColorMulR, uVertexColorMulG, uVertexColorMulB);
  float3 onGameColor =
      float3(
          uMaterialAtlasWidth,
          uMaterialAtlasHeight,
          uMaterialRect0U);
  float3 surface =
      (base.rgb + decoration) * i.col.rgb *
      lerp(1.0f.xxx, onGameColor, saturate(uMaterialTimeSec));
  return float4(
      lerp(shadowColor, 1.0f.xxx, projectedLight) * surface,
      alpha);
}



float4 evaluateRoadstoneOverlaySurface(PSIn i) {
  float sourceMipBias = uMaterialFlipbook0Fps;
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float4 texture01 =
      sampleFieldGrassRepeat(gTex, uv0, sourceMipBias);
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      sampleFieldGrassClamp(
          gOcclusionTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          sourceMipBias).r);
  float3 onGameColor =
      float3(
          uMaterialTimeSec,
          uMaterialFlags,
          uMaterialAtlasWidth);
  float alpha =
      texture01.a * i.col.a *
      saturate(uMaterialRect0V) *
      saturate(uMaterialRect0U);
  float3 surface =
      texture01.rgb * i.col.rgb *
      lerp(
          1.0f.xxx,
          onGameColor,
          saturate(uMaterialAtlasHeight));
  float3 lighting =
      lerp(
          float3(
              uVertexColorMulR,
              uVertexColorMulG,
              uVertexColorMulB),
          1.0f.xxx,
          evaluateFieldProjectedLighting(toon, i.worldPos));
  return float4(lighting * surface * alpha, alpha);
}

float4 evaluateRockMaskOverlaySurface(PSIn i) {
  float sourceMipBias = uMaterialFlipbook0Fps;
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 =
      float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float2 blendUv =
      float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float3 textureMap01 =
      sampleFieldGrassRepeat(
          gTex,
          uv0,
          sourceMipBias).rgb;
  float3 textureMap02 =
      sampleFieldGrassRepeat(
          gNormalTex,
          uv0,
          sourceMipBias).rgb;
  float4 greenHikari =
      sampleFieldGrassRepeat(
          gMetallicRoughnessTex,
          uv1,
          sourceMipBias);
  float greenBlend = saturate(
      sampleFieldGrassRepeat(
          gOcclusionTex,
          blendUv,
          sourceMipBias).r);
  float highlight = saturate(
      sampleFieldGrassRepeat(
          gEmissiveTex,
          uv1,
          sourceMipBias).r);
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      sampleFieldGrassClamp(
          gEnvTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          sourceMipBias).r);
  float3 sourceColor =
      float3(
          uMaterialFlipbook0Cols,
          uMaterialFlipbook0Rows,
          uMaterialFlipbook0Frames);
  float3 decoration =
      lerp(textureMap02, textureMap01, greenBlend) +
      sourceColor * (1.0f - highlight);
  float3 onGameColor =
      float3(
          uMaterialTimeSec,
          uMaterialFlags,
          uMaterialAtlasWidth);
  float alpha =
      greenHikari.a * saturate(uMaterialRect0U);
  float3 surface =
      decoration * greenHikari.rgb * i.col.rgb *
      lerp(
          1.0f.xxx,
          onGameColor,
          saturate(uMaterialAtlasHeight));
  float3 lighting =
      lerp(
          float3(
              uVertexColorMulR,
              uVertexColorMulG,
              uVertexColorMulB),
          1.0f.xxx,
          evaluateFieldProjectedLighting(toon, i.worldPos));
  return float4(lighting * surface * alpha, alpha);
}



float evaluateFieldRockLightToon(float toonCoordinate) {
  static const float sourceValues[54] = {
      1.0f, 3.0f, 5.0f, 7.0f, 10.0f, 13.0f, 16.0f, 19.0f,
      22.0f, 26.0f, 30.0f, 34.0f, 38.0f, 43.0f, 47.0f, 53.0f,
      58.0f, 63.0f, 68.0f, 73.0f, 80.0f, 85.0f, 91.0f, 97.0f,
      103.0f, 110.0f, 116.0f, 122.0f, 129.0f, 135.0f, 142.0f,
      148.0f, 155.0f, 161.0f, 168.0f, 174.0f, 181.0f, 188.0f,
      193.0f, 200.0f, 207.0f, 211.0f, 218.0f, 223.0f, 227.0f,
      232.0f, 236.0f, 239.0f, 243.0f, 247.0f, 249.0f, 253.0f,
      255.0f, 255.0f};
  float sourceTexel = saturate(toonCoordinate) * 512.0f - 0.5f;
  int lower = (int)floor(sourceTexel);
  int upper = lower + 1;
  float lowerValue = lower < 458
      ? 0.0f
      : (lower >= 512 ? 255.0f : sourceValues[lower - 458]);
  float upperValue = upper < 458
      ? 0.0f
      : (upper >= 512 ? 255.0f : sourceValues[upper - 458]);
  return lerp(lowerValue, upperValue, frac(sourceTexel)) / 255.0f;
}

float lgpeFlowerCoverage(float alpha) {
  float coverageT = saturate((alpha - 0.55f) / (0.85f - 0.55f));
  return coverageT * coverageT * (3.0f - 2.0f * coverageT);
}

float lgpeFlowerDitherThreshold(float2 fragmentPosition) {
  int2 pixel = int2(floor(fmod(fragmentPosition, 4.0f)));
  static const float bayer[16] = {
       0.5f,  8.5f,  2.5f, 10.5f,
      12.5f,  4.5f, 14.5f,  6.5f,
       3.5f, 11.5f,  1.5f,  9.5f,
      15.5f,  7.5f, 13.5f,  5.5f};
  return bayer[pixel.x + pixel.y * 4] / 16.0f;
}

float3 lgpeFlowerFieldHighlight(float3 sourceColor) {
  float maximum = max(max(sourceColor.r, sourceColor.g), sourceColor.b);
  float minimum = min(min(sourceColor.r, sourceColor.g), sourceColor.b);
  float chroma = maximum - minimum;
  float sourceSaturation =
      maximum > 0.000001f ? chroma / maximum : 0.0f;
  float saturation = saturate(sourceSaturation * 1.14f);
  float value = maximum * 1.12f;
  float adjustedMinimum = value * (1.0f - saturation);
  float adjustedChroma = value * saturation;
  float3 hueComponent =
      chroma > 0.000001f
          ? (sourceColor - minimum.xxx) / chroma
          : 0.0f.xxx;
  return adjustedMinimum.xxx + hueComponent * adjustedChroma;
}

float4 evaluateFieldFlowerSurface(PSIn i) {
  float sourceMipBias = uMaterialFlipbook0Fps;
  bool buildmodelReview =
      uMaterialMode > 19.5f && uMaterialMode < 20.5f;
  float2 uv0 =
      buildmodelReview
          ? i.uv
          : float2(i.uv.x, 1.0f - i.uv.y);
  float4 texture01 =
      sampleFieldGrassRepeat(gTex, uv0, sourceMipBias);
  float4 authoredVertexColor =
      i.col *
      float4(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB,
          uVertexColorMulA);
  float sourceAlpha =
      texture01.a * authoredVertexColor.a *
      saturate(uMaterialRect0V) *
      saturate(uMaterialRect0U);
  float alpha = sourceAlpha;
  if (buildmodelReview) {
    alpha = lgpeFlowerCoverage(sourceAlpha);
    if (alpha <= lgpeFlowerDitherThreshold(i.pos.xy)) discard;
  } else if (alpha <= saturate(uAlphaCutoff)) {
    discard;
  }

  float3 shadowColor =
      float3(
          uMaterialRect0W,
          uMaterialRect0H,
          uMaterialRect1U);
  if (buildmodelReview) {
    float projectedLight =
        evaluateFieldProjectedLighting(1.0f, i.worldPos);
    float3 normal = normalize(i.worldNormal);
    const float3 sourceSunRay =
        float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5f + 0.5f;
    float toon = saturate(
        sampleFieldGrassClamp(
            gOcclusionTex,
            float2(toonCoordinate, 1.0f - toonCoordinate),
            sourceMipBias).r);
    float3 projectedLighting =
        lerp(shadowColor, 1.0f.xxx, projectedLight);
    float3 projectionCompensation =
        lerp(1.0f.xxx, projectedLighting, 0.25f);
    float3 accepted = lgpeFlowerFieldHighlight(texture01.rgb);
    float3 exact =
        texture01.rgb * authoredVertexColor.rgb *
        projectionCompensation;
    float3 restrained = lerp(accepted, exact, 0.35f);
    float3 fieldLighting =
        lerp(
            shadowColor,
            1.0f.xxx,
            evaluateFieldProjectedLighting(toon, i.worldPos));
    return float4(
        restrained * (fieldLighting + 0.12f.xxx),
        alpha);
  }

  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      sampleFieldGrassClamp(
          gOcclusionTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          sourceMipBias).r);
  float3 onGameColor =
      float3(
          uMaterialTimeSec,
          uMaterialFlags,
          uMaterialAtlasWidth);
  float3 surface =
      texture01.rgb * authoredVertexColor.rgb *
      lerp(
          1.0f.xxx,
          onGameColor,
          saturate(uMaterialAtlasHeight));
  float3 lighting = lerp(
      shadowColor,
      1.0f.xxx,
      evaluateFieldProjectedLighting(toon, i.worldPos));
  return float4(lighting * surface, alpha);
}

float4 evaluateFieldRockSurface(PSIn i) {
  float sourceMipBias = uMaterialFlipbook0Fps;
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 =
      float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float2 uv2 =
      float2(i.sourceUv2.x, 1.0f - i.sourceUv2.y);
  float2 blendUv =
      float2(i.uv.x * 0.3f, 1.0f - i.uv.y * 0.3f);
  float4 rockTexture =
      sampleFieldGrassRepeat(gTex, uv1, sourceMipBias);
  float3 groundTexture02 =
      sampleFieldGrassRepeat(
          gNormalTex,
          uv0,
          sourceMipBias).rgb;
  float3 groundTexture01 =
      sampleFieldGrassRepeat(
          gMetallicRoughnessTex,
          uv0,
          sourceMipBias).rgb;
  float blend = saturate(
      sampleFieldGrassRepeat(
          gOcclusionTex,
          blendUv,
          sourceMipBias).r);
  float4 borderTexture =
      sampleFieldGrassRepeat(
          gEmissiveTex,
          uv2,
          sourceMipBias);

  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float shadowToon = saturate(
      sampleFieldGrassClamp(
          gEnvTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          sourceMipBias).r);
  float lightToon =
      evaluateFieldRockLightToon(toonCoordinate);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialRect0V;
  float rimMax = uMaterialTimeSec;
  float rimStrength = uMaterialFlags;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate(
            ((1.0f - dot(normal, viewDirection)) - rimMin) /
            rimSpan) *
            rimStrength
      : 0.0f;
  float3 lightColor =
      float3(
          uMaterialRect0W,
          uMaterialRect0H,
          uMaterialRect1U);
  float3 rimColor =
      float3(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB);
  float3 rock =
      rockTexture.rgb + lightColor * lightToon +
      rimColor * rim * rockTexture.a;
  float3 ground =
      lerp(groundTexture02, groundTexture01, blend);
  float3 surface =
      lerp(rock, ground, saturate(borderTexture.a));
  float3 shadowColor =
      float3(
          uMaterialAtlasWidth,
          uMaterialAtlasHeight,
          uMaterialRect0U);
  float3 lighting = lerp(
      shadowColor,
      1.0f.xxx,
      evaluateFieldProjectedLighting(shadowToon, i.worldPos));
  return float4(
      lighting * borderTexture.rgb * i.col.rgb * surface,
      1.0f);
}

float4 evaluatePaintedSurfaceSurface(PSIn i) {
  float sourceMipBias = uMaterialFlipbook0Fps;
  float2 uv0 = i.uv;
  float4 texture01 =
      sampleFieldGrassRepeat(gTex, uv0, sourceMipBias);
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float2 toonUv =
      float2(toonCoordinate, 1.0f - toonCoordinate);
  float shadowToon = saturate(
      sampleFieldGrassClamp(
          gOcclusionTex,
          toonUv,
          sourceMipBias).r);
  float lightToon =
      evaluateFieldRockLightToon(toonCoordinate);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialRect0W;
  float rimMax = uMaterialRect0H;
  float rimStrength = uMaterialRect1U;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate(
            ((1.0f - dot(normal, viewDirection)) - rimMin) /
            rimSpan) *
            rimStrength
      : 0.0f;
  float3 lightColor =
      float3(
          uVertexColorMulR,
          uVertexColorMulG,
          uVertexColorMulB);
  float3 shadowColor =
      float3(
          uMaterialTimeSec,
          uMaterialFlags,
          uMaterialAtlasWidth);
  float3 rimColor =
      float3(
          uMaterialAtlasHeight,
          uMaterialRect0U,
          uMaterialRect0V);
  // Source ShadowColor and OnGameColor are exactly white, so the recovered
  // AutoShadow and OnGame mixes are neutral for this Route 1 sign material.
  float3 surface =
      texture01.rgb + lightColor * lightToon + rimColor * rim;
  float3 lighting =
      lerp(
          shadowColor,
          1.0f.xxx,
          evaluateFieldProjectedLighting(
              shadowToon,
              i.worldPos));
  return float4(
      lighting * surface * i.col.rgb,
      texture01.a * i.col.a);
}

float4 evaluateFieldEncounterGrassSurface(PSIn i) {
  float sourceMipBias = uMaterialFlipbook0Fps;
  float2 uv0 = i.uv;
  float4 texture01 =
      sampleFieldGrassRepeat(gTex, uv0, sourceMipBias);
  if (texture01.a <= saturate(uAlphaCutoff)) discard;
  float rimMask = saturate(
      sampleFieldGrassRepeat(
          gNormalTex,
          uv0,
          sourceMipBias).r);
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float toonCoordinate =
      dot(normal, -sourceSunRay) * 0.5f + 0.5f;
  float shadowToon = saturate(
      sampleFieldGrassClamp(
          gOcclusionTex,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          sourceMipBias).r);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialRect0W;
  float rimMax = uMaterialRect0H;
  float rimStrength = uMaterialRect1U;
  float rim =
      smoothstep(
          rimMin,
          max(rimMax, rimMin + 1.0e-5f),
          saturate(1.0f - abs(dot(normal, viewDirection)))) *
      rimStrength * rimMask;
  float3 shadowColor =
      float3(
          uMaterialTimeSec,
          uMaterialFlags,
          uMaterialAtlasWidth);
  float3 rimColor =
      float3(
          uMaterialAtlasHeight,
          uMaterialRect0U,
          uMaterialRect0V);
  float3 base = texture01.rgb * i.col.rgb;
  float3 lighting =
      lerp(
          shadowColor,
          1.0f.xxx,
          evaluateFieldProjectedLighting(
              shadowToon,
              i.worldPos));
  return float4(
      (base + rimColor * rim) * lighting,
      texture01.a * i.col.a);
}

float4 evaluateTreeTrunkSurface(PSIn i) {
  float2 uv0 = float2(i.uv.x, 1.0f - i.uv.y);
  float2 uv1 = float2(i.sourceUv1.x, 1.0f - i.sourceUv1.y);
  float4 texture01 = sampleFieldTreeTexture(gTex, uv0);
  float highlightAlpha =
      sampleFieldTreeTexture(gNormalTex, uv1).a;
  float3 normal = normalize(i.worldNormal);
  const float3 sourceSunRay =
      float3(0.5533391237f, 0.2078260481f, -0.8066127300f);
  float normalDotLight = dot(normal, -sourceSunRay);
  float toonCoordinate = normalDotLight * 0.5f + 0.5f;
  float toon = saturate(
      gOcclusionTex.SampleBias(
          gSampCC,
          float2(toonCoordinate, 1.0f - toonCoordinate),
          0.35f).r);
  float3 cameraPos =
      float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
  float3 viewDirection = normalize(cameraPos - i.worldPos);
  float rimMin = uMaterialTimeSec;
  float rimMax = uMaterialFlags;
  float rimStrength = uMaterialAtlasWidth;
  float rimSpan = max(rimMax, rimMin) - rimMin;
  float rim = rimSpan > 0.0f
      ? saturate(
            (saturate(1.0f - dot(normal, viewDirection)) - rimMin) /
            rimSpan) *
            rimStrength
      : 0.0f;
  float3 shadowColor =
      max(float3(
              uVertexColorMulR,
              uVertexColorMulG,
              uVertexColorMulB),
          float3(0.0f, 0.0f, 0.0f));
  float3 rimColor =
      max(float3(
              uMaterialAtlasHeight,
              uMaterialRect0U,
              uMaterialRect0V),
          float3(0.0f, 0.0f, 0.0f));
  float3 lighting = lerp(shadowColor, 1.0f.xxx, toon);
  float3 surface =
      texture01.rgb + rimColor * rim * highlightAlpha;
  return float4(
      lighting * surface * i.col.rgb,
      texture01.a * i.col.a);
}
