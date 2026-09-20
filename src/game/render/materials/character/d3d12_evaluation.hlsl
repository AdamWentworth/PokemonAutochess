
  if (uMaterialMode > 2.5f && uMaterialMode < 3.5f) {
    // Match the OpenGL/Vulkan inverted-hull outline contract: discard the
    // expanded mesh's front faces and retain only its back-facing silhouette.
    if (isFrontFace) discard;
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
  if (uMaterialMode > 0.5f && uMaterialMode < 1.5f) {
    return evalFireTailExact(i);
  }
  if (uMaterialMode > 26.5f && uMaterialMode < 27.5f) {
    float4 surface = evalNativeLayeredUnlitDisplaced(i);
    if (uMaterialFlags > 2.5f && uMaterialFlags < 3.5f) {
      bool useAuthoredShadowColor = uMaterialFlags > 3.3f;
      float4 authoredResponse = useAuthoredShadowColor
          ? gEmissiveTex.Sample(gSampCC, nativeLayeredMaterialUv(i))
          : float4(surface.rgb, 0.0f);
      surface.rgb = applyVolumeRimLighting(
          i,
          surface.rgb,
          authoredResponse,
          useAuthoredShadowColor);
    }
    const float toneMappingExposure = __PHLOSION_PBR_TONEMAP_EXPOSURE__;
    // Do not feed Scarlet's authored HDR Unlit layer colors through the
    // viewer ACES fit.  ACES pulls the saturated [5,.075,.0295] red and
    // [4,.8,.18] orange layers toward the same pale yellow, erasing the
    // source mask gradient as the animated flame mesh deforms.  The native
    // shader writes emissive color directly; a channel-linear exposure/clamp
    // is the closest available output transform until Scarlet's global
    // post-process is represented as its own contract.
    float3 mapped = linearToneMapping(
        max(surface.rgb, float3(0.0f, 0.0f, 0.0f)),
        toneMappingExposure);
    return float4(resolveWorldSceneColor(mapped), surface.a);
  }
__AUTOCHESS_FIELD_EVALUATION__
  float4 tex = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float3 outLinear = saturate(i.col.rgb * float3(uVertexColorMulR, uVertexColorMulG, uVertexColorMulB));
  const bool animatedEyeMaterial =
      uMaterialMode > 28.5f && uMaterialMode < 30.5f;
  const float2 materialUv = animatedEyeMaterial
      ? float2(
            i.uv.x * uLightProjectionUvRowU.x +
                uLightProjectionUvRowU.z,
            i.uv.y * uLightProjectionUvRowU.y +
                uLightProjectionUvRowU.w)
      : i.uv;
  float2 wrappedUv = float2(
      applyWrap(materialUv.x, uWrapS),
      applyWrap(materialUv.y, uWrapT));
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
  float3 reviewAlbedo = outLinear;
  const float3 reviewCameraForward = float3(
      uMaterialFlipbook0Cols,
      uMaterialFlipbook0Rows,
      uMaterialFlipbook0Frames);
  float outA = saturate(i.col.a * uVertexColorMulA * tex.a);
  float alphaWindowMin = saturate(uAlphaWindowMin);
  float alphaWindowMax = saturate(uAlphaWindowMax);
  if (alphaWindowMax < 1.0f || alphaWindowMin > 0.0f) {
    if (outA < alphaWindowMin || outA >= alphaWindowMax) discard;
  }
  if (uAlphaMode < 0.5f) {
    outA = saturate(i.col.a * uVertexColorMulA);
  } else if (uAlphaMode < 1.5f) {
    if (outA < saturate(uAlphaCutoff)) discard;
    outA = saturate(i.col.a * uVertexColorMulA);
  }
  const bool explicitMaterialDebug =
      uMaterialFlipbook1Fps < -100.5f;
  const int materialDebugFlags = (int)(uMaterialFlags + 0.5f);
  const float pbrDebugView = explicitMaterialDebug
      ? -uMaterialFlipbook1Fps - 100.0f
      : ((animatedEyeMaterial || uMaterialMode > 30.5f)
             ? 0.0f
             : uMaterialFlipbook1Fps);
  if (uMaterialMode >= 1.5f && pbrDebugView > 0.5f) {
    float3 dbg = float3(0.0f, 0.0f, 0.0f);
    if (pbrDebugView < 1.5f) {
      // 1: Raw base-color texture sample.
      dbg = saturate(tex.rgb);
    } else if (pbrDebugView < 2.5f) {
      // 2: Authored tint-resolved albedo without lighting.
      dbg = (uMaterialMode > 33.5f && uMaterialMode < 34.5f)
          ? viewAngleLayerBase(outLinear)
          : saturate(outLinear);
    } else if (pbrDebugView < 3.5f) {
      // 3: Normal map sample.
      dbg = (materialDebugFlags & (1 << 0)) != 0
          ? sampleTextureWithWrap(
                gNormalTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).rgb
          : float3(0.5f, 0.5f, 1.0f);
    } else if (pbrDebugView < 4.5f) {
      // 4: Roughness channel.
      const float rgh = (materialDebugFlags & (1 << 1)) != 0
          ? sampleTextureWithWrap(
                gMetallicRoughnessTex,
                wrappedUv,
                uvDx,
                uvDy,
                uWrapS,
                uWrapT).g
          : 1.0f;
      dbg = float3(rgh, rgh, rgh);
    } else if (pbrDebugView < 5.5f) {
      // 5: Metallic channel.
      const float met = (materialDebugFlags & (1 << 1)) != 0
          ? sampleTextureWithWrap(
                gMetallicRoughnessTex,
                wrappedUv,
                uvDx,
                uvDy,
                uWrapS,
                uWrapT).b
          : 0.0f;
      dbg = float3(met, met, met);
    } else if (pbrDebugView < 6.5f) {
      // 6: AO channel.
      const float ao = (materialDebugFlags & (1 << 2)) != 0
          ? sampleTextureWithWrap(
                gOcclusionTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).r
          : 1.0f;
      dbg = float3(ao, ao, ao);
    } else if (pbrDebugView < 7.5f) {
      // 7: Emissive sample.
      dbg = (materialDebugFlags & (1 << 3)) != 0
          ? sampleTextureWithWrap(
                gEmissiveTex, wrappedUv, uvDx, uvDy, uWrapS, uWrapT).rgb
          : float3(0.0f, 0.0f, 0.0f);
    }
    return float4(resolveWorldSceneColor(dbg), 1.0f);
  }
  if (uMaterialMode >= 1.5f) {
    const bool layeredEyeCoat =
        (uMaterialMode > 27.5f && uMaterialMode < 28.5f) ||
        (uMaterialMode > 29.5f && uMaterialMode < 30.5f);
    const bool bakedEyeDiffuse =
        layeredEyeCoat && uProjectedShadowRowY.w < -0.5f;
    const bool separateEyeCoat =
        layeredEyeCoat && !bakedEyeDiffuse;
    const int pbrFlags = (int)(uMaterialFlags + 0.5f);
    const bool useNormalTexture = (pbrFlags & (1 << 0)) != 0;
    const bool useMetallicRoughnessTexture = (pbrFlags & (1 << 1)) != 0;
    const bool useOcclusionTexture = (pbrFlags & (1 << 2)) != 0;
    const bool useEmissiveTexture = (pbrFlags & (1 << 3)) != 0;
    const bool useSpecularStrengthTexture = (pbrFlags & (1 << 4)) != 0;
    const float normalScale = max(uMaterialAtlasWidth, 0.0f);
    const float metallicFactor = saturate(uMaterialAtlasHeight);
    const float roughnessFactor = saturate(uMaterialRect0U);
    const float occlusionStrength = max(uMaterialRect0V, 0.0f);
    const float3 emissiveFactor =
        max(float3(uMaterialRect0W, uMaterialRect0H, uMaterialRect1U), float3(0.0f, 0.0f, 0.0f));
    const float3 cameraPos = float3(uMaterialRect1V, uMaterialRect1W, uMaterialRect1H);
    const float3 cameraForward = float3(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows, uMaterialFlipbook0Frames);
    const float3 cameraTarget = float3(uMaterialFlipbook0Fps, uMaterialFlipbook1Cols, uMaterialFlipbook1Rows);
    const bool facialOverlay =
        uMaterialMode > 30.5f &&
        uMaterialFlipbook1Fps > 3.5f &&
        uMaterialFlipbook1Fps < 4.5f;
    const bool layeredCharacter =
        uMaterialMode > 31.5f && uMaterialMode < 32.5f;
    const bool subsurface =
        uMaterialMode > 32.5f && uMaterialMode < 33.5f;
    const bool viewAngleLayer =
        uMaterialMode > 33.5f && uMaterialMode < 34.5f;
    const bool refractiveEye =
        uMaterialMode > 34.5f && uMaterialMode < 35.5f;
    if (viewAngleLayer) {
      reviewAlbedo = viewAngleLayerBase(outLinear);
    }
    if (subsurface) {
      outLinear = applySubsurfaceSurface(
          i,
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
          roughnessFactor,
          occlusionStrength,
          emissiveFactor,
          cameraPos,
          cameraForward,
          uMaterialTimeSec);
    } else if (layeredCharacter || refractiveEye) {
      outLinear = applyLayeredCharacter(
          i,
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
          cameraTarget,
          refractiveEye);
    } else if (facialOverlay) {
      outLinear = applyFacialOverlay(
          i,
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
          occlusionStrength,
          cameraPos,
          cameraForward,
          cameraTarget,
          uMaterialTimeSec > 0.5f);
    } else if (viewAngleLayer) {
      float3 primaryColor = viewAngleLayerBase(outLinear);
      outLinear = applyWorldLitModel(i,
                                     isFrontFace,
                                     primaryColor,
                                     wrappedUv,
                                     uvDx,
                                     uvDy,
                                     useNormalTexture,
                                     false,
                                     false,
                                     useOcclusionTexture,
                                     false,
                                     normalScale,
                                     metallicFactor,
                                     roughnessFactor,
                                     uMaterialFlipbook1Frames,
                                     occlusionStrength,
                                     float3(0.0f, 0.0f, 0.0f),
                                     cameraPos,
                                     cameraForward,
                                     cameraTarget);
      float3 fresnelNormal = computeMappedFresnelLayerNormal(
          i,
          isFrontFace,
          wrappedUv,
          uvDx,
          uvDy,
          useMetallicRoughnessTexture,
          uLightProjectionUvRowU.w);
      outLinear = applyViewAngleLayerLayer(
          i,
          outLinear,
          primaryColor,
          fresnelNormal,
          wrappedUv,
          uvDx,
          uvDy,
          useOcclusionTexture,
          useEmissiveTexture,
          metallicFactor,
          roughnessFactor,
          occlusionStrength,
          cameraPos,
          cameraForward);
    } else {
      outLinear = applyWorldLitModel(i,
                                     isFrontFace,
                                     outLinear,
                                     wrappedUv,
                                     uvDx,
                                     uvDy,
                                     separateEyeCoat
                                         ? false
                                         : useNormalTexture,
                                     useMetallicRoughnessTexture,
                                     separateEyeCoat
                                         ? true
                                         : useSpecularStrengthTexture,
                                     useOcclusionTexture,
                                     useEmissiveTexture,
                                     bakedEyeDiffuse
                                         ? normalScale * 0.8f
                                         : normalScale,
                                     metallicFactor,
                                     roughnessFactor,
                                     separateEyeCoat
                                         ? 0.0f
                                         : uMaterialFlipbook1Frames,
                                     occlusionStrength,
                                     emissiveFactor,
                                     cameraPos,
                                     cameraForward,
                                     cameraTarget);
    }
    if (layeredEyeCoat) {
      float3 eyeNormal = computeMappedNormal(
          i,
          isFrontFace,
          wrappedUv,
          uvDx,
          uvDy,
          false,
          0.0f);
      outLinear = applyLayeredEyeCoat(
          i,
          outLinear,
          eyeNormal,
          cameraPos,
          cameraForward,
          cameraTarget);
    }
  }
  if (uMaterialMode >= 1.5f) {
    outLinear = applyReviewLightingProfile(
        outLinear,
        reviewAlbedo,
        i.worldNormal,
        reviewCameraForward);
  }
  const float toneMappingExposure = __PHLOSION_PBR_TONEMAP_EXPOSURE__;
  const float toneMappingMode = 1.0f;
  float3 mapped = applyViewerToneMapping(
      max(outLinear, float3(0.0f, 0.0f, 0.0f)),
      toneMappingMode,
      toneMappingExposure);
  float3 outSrgb = resolveWorldSceneColor(mapped);
  return float4(outSrgb, outA);
