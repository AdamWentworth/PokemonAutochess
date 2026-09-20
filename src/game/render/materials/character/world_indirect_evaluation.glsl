
    WorldIndirectDrawState drawState = worldIndirectDraws.states[drawStateIndex];
    sceneColorPostEnabled = drawState.shadingParams.w > 0.5;
    uint materialIndex = drawState.drawParams.x;
    float alphaMode = drawState.materialParams.x;
    float alphaCutoff = drawState.materialParams.y;
    float materialMode = drawState.materialParams.w;
    LayeredEffectMaterialState layeredEffectMaterial = LayeredEffectMaterialState(
        drawState.specializedTimingFlagsAtlas,
        drawState.specializedRect0,
        drawState.specializedRect1,
        drawState.specializedFlipbook0,
        drawState.specializedFlipbook1);

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (gl_FrontFacing) discard;
        writeWorldColor(vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    if (materialMode > 0.5 && materialMode < 1.5) {
        writeWorldColor(evaluateFlameEffect(
            baseColorTextures[nonuniformEXT(materialIndex)],
            layeredEffectMaterial));
        return;
    }
    if (materialMode > 26.5 && materialMode < 27.5) {
        vec4 surface = evaluateNativeLayeredUnlitDisplaced(
            baseColorTextures[nonuniformEXT(materialIndex)],
            metallicRoughnessTextures[nonuniformEXT(materialIndex)],
            layeredEffectMaterial);
        if (drawState.specializedTimingFlagsAtlas.y > 2.5 &&
            drawState.specializedTimingFlagsAtlas.y < 3.5) {
            bool useAuthoredShadowColor =
                drawState.specializedTimingFlagsAtlas.y > 3.3;
            vec4 authoredResponse = useAuthoredShadowColor
                ? texture(
                    emissiveTextures[nonuniformEXT(materialIndex)],
                    nativeLayeredMaterialUv(layeredEffectMaterial))
                : vec4(surface.rgb, 0.0);
            surface.rgb = applyVolumeRimLighting(
                surface.rgb,
                authoredResponse,
                useAuthoredShadowColor);
        }
        const float nativeToneMappingExposure = 1.15;
        vec3 nativeMapped = clamp(
            max(surface.rgb, vec3(0.0)) * nativeToneMappingExposure,
            vec3(0.0),
            vec3(1.0));
        vec3 nativeResolved = sceneColorPostEnabled
            ? nativeMapped
            : linearToSrgb(nativeMapped);
        writeWorldColor(vec4(nativeResolved, surface.a));
        return;
    }
__AUTOCHESS_FIELD_EVALUATION__

    bool animatedEyeMaterial =
        materialMode > 28.5 && materialMode < 30.5;
    vec2 materialUv = animatedEyeMaterial
        ? vec2(
              vertexUv.x *
                      drawState.specializedLightProjectionUvRowU.x +
                  drawState.specializedLightProjectionUvRowU.z,
              vertexUv.y *
                      drawState.specializedLightProjectionUvRowU.y +
                  drawState.specializedLightProjectionUvRowU.w)
        : vertexUv;
    float textureDetailLodBias =
        ((materialMode >= 1.5 && materialMode < 2.5) ||
         (materialMode > 27.5 && materialMode < 30.5) ||
         (materialMode > 31.5 && materialMode < 35.5))
            ? drawState.specializedFlipbook1.z
            : 0.0;
    vec4 sampled = sampleWorldMaterialTexture(
        baseColorTextures[nonuniformEXT(materialIndex)],
        materialUv,
        textureDetailLodBias);
    vec3 linearColor = clamp(sampled.rgb, 0.0, 1.0) * clamp(vertexColor.rgb, 0.0, 1.0);
    vec3 reviewAlbedo = linearColor;
    float alpha = clamp(vertexColor.a * sampled.a, 0.0, 1.0);

    float alphaWindowMin = clamp(drawState.shadingParams.x, 0.0, 1.0);
    float alphaWindowMax = clamp(drawState.shadingParams.y, 0.0, 1.0);
    if ((alphaWindowMax < 1.0 || alphaWindowMin > 0.0) &&
        (alpha < alphaWindowMin || alpha >= alphaWindowMax)) {
        discard;
    }
    if (alphaMode < 0.5) {
        alpha = clamp(vertexColor.a, 0.0, 1.0);
    } else if (alphaMode < 1.5) {
        if (alpha < alphaCutoff) discard;
        alpha = clamp(vertexColor.a, 0.0, 1.0);
    }

    bool explicitMaterialDebug =
        drawState.specializedFlipbook1.w < -100.5;
    float pbrDebugView = explicitMaterialDebug
        ? -drawState.specializedFlipbook1.w - 100.0
        : 0.0;
    int materialDebugFlags = int(
        drawState.specializedTimingFlagsAtlas.y + 0.5);
    if (materialMode >= 1.5 && pbrDebugView > 0.5) {
        vec3 debugColor = vec3(0.0);
        if (pbrDebugView < 1.5) {
            // 1: Raw base-color texture sample.
            debugColor = clamp(sampled.rgb, 0.0, 1.0);
        } else if (pbrDebugView < 2.5) {
            // 2: Authored tint-resolved albedo without lighting.
            debugColor = (materialMode > 33.5 && materialMode < 34.5)
                ? viewAngleLayerBase(
                      linearColor,
                      drawState.specializedRect0,
                      drawState.specializedFlipbook1)
                : clamp(linearColor, 0.0, 1.0);
        } else if (pbrDebugView < 3.5) {
            // 3: Normal map sample.
            debugColor = (materialDebugFlags & (1 << 0)) != 0
                ? sampleWorldMaterialTexture(
                      normalTextures[nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).rgb
                : vec3(0.5, 0.5, 1.0);
        } else if (pbrDebugView < 4.5) {
            // 4: Roughness channel.
            float roughness = (materialDebugFlags & (1 << 1)) != 0
                ? sampleWorldMaterialTexture(
                      metallicRoughnessTextures[
                          nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).g
                : 1.0;
            debugColor = vec3(roughness);
        } else if (pbrDebugView < 5.5) {
            // 5: Metallic channel.
            float metallic = (materialDebugFlags & (1 << 1)) != 0
                ? sampleWorldMaterialTexture(
                      metallicRoughnessTextures[
                          nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).b
                : 0.0;
            debugColor = vec3(metallic);
        } else if (pbrDebugView < 6.5) {
            // 6: AO channel.
            float occlusion = (materialDebugFlags & (1 << 2)) != 0
                ? sampleWorldMaterialTexture(
                      occlusionTextures[nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).r
                : 1.0;
            debugColor = vec3(occlusion);
        } else if (pbrDebugView < 7.5) {
            // 7: Emissive sample.
            debugColor = (materialDebugFlags & (1 << 3)) != 0
                ? sampleWorldMaterialTexture(
                      emissiveTextures[nonuniformEXT(materialIndex)],
                      materialUv,
                      textureDetailLodBias).rgb
                : vec3(0.0);
        }
        vec3 resolvedDebugColor = drawState.shadingParams.w > 0.5
            ? debugColor
            : linearToSrgb(debugColor);
        writeWorldColor(vec4(resolvedDebugColor, 1.0));
        return;
    }

    // See the direct world path: the exact 384x128 probe carrier is a
    // stable, profile-private Vulkan marker when editor view-state coalescing
    // removes the redundant camera-vector length lane.
    ivec2 stageDiffuseProbeSize = textureSize(
        lightProjectionTextures[nonuniformEXT(materialIndex)], 0);
    bool authoredStageCarrier =
        stageDiffuseProbeSize.x == 384 && stageDiffuseProbeSize.y == 128;
    vec3 reviewCameraForward = authoredStageCarrier
        ? safeNormalize(
              worldView.cameraForward.xyz,
              vec3(0.0, -0.6139406, -0.7893522)) * 5.0
        : worldView.cameraForward.xyz;
    if ((materialMode >= 1.5 && materialMode < 2.5) ||
        (materialMode > 27.5 && materialMode < 35.5)) {
        bool layeredEyeCoat =
            (materialMode > 27.5 && materialMode < 28.5) ||
            (materialMode > 29.5 && materialMode < 30.5);
        bool bakedEyeDiffuse =
            layeredEyeCoat && drawState.specializedRect1.w < -0.5;
        bool separateEyeCoat =
            layeredEyeCoat && !bakedEyeDiffuse;
        bool facialOverlay =
            materialMode > 30.5 &&
            drawState.specializedTimingFlagsAtlas.y > 3.5 &&
            drawState.specializedTimingFlagsAtlas.y < 4.5;
        bool layeredCharacter =
            materialMode > 31.5 && materialMode < 32.5;
        bool subsurface =
            materialMode > 32.5 && materialMode < 33.5;
        bool viewAngleLayer =
            materialMode > 33.5 && materialMode < 34.5;
        bool refractiveEye =
            materialMode > 34.5 && materialMode < 35.5;
        if (viewAngleLayer) {
            reviewAlbedo = viewAngleLayerBase(
                linearColor,
                drawState.specializedRect0,
                drawState.specializedFlipbook1);
        }
        if (subsurface) {
            linearColor = evaluateSubsurfaceSurface(
                linearColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                reviewCameraForward,
                normalTextures[nonuniformEXT(materialIndex)],
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.specializedTimingFlagsAtlas.y,
                drawState.pbrFactors,
                drawState.emissiveAndCamera.rgb);
        } else if (layeredCharacter || refractiveEye) {
            linearColor = evaluateLayeredCharacter(
                linearColor,
                vertexColor.rgb,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                reviewCameraForward,
                worldView.cameraTarget.xyz,
                baseColorTextures[nonuniformEXT(materialIndex)],
                normalTextures[nonuniformEXT(materialIndex)],
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                lightProjectionTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.pbrFactors,
                drawState.emissiveAndCamera.rgb,
                drawState.specializedRect0,
                drawState.specializedRect1,
                drawState.specializedFlipbook0,
                drawState.specializedFlipbook1,
                drawState.specializedLightProjectionUvRowV.w,
                refractiveEye);
        } else if (facialOverlay) {
            linearColor = evaluateFacialOverlay(
                linearColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                worldView.cameraTarget.xyz,
                normalTextures[nonuniformEXT(materialIndex)],
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.pbrFactors.x,
                drawState.pbrFactors.w,
                drawState.specializedRect0.w > 0.5);
        } else if (viewAngleLayer) {
            vec3 primaryColor = viewAngleLayerBase(
                linearColor,
                drawState.specializedRect0,
                drawState.specializedFlipbook1);
            linearColor = evaluateWorldMaterial(
                primaryColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                worldView.cameraTarget.xyz,
                normalTextures[nonuniformEXT(materialIndex)],
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.pbrFactors,
                vec3(0.0),
                -1.0,
                1.0,
                false);
            linearColor = evaluateViewAngleLayerLayer(
                linearColor,
                primaryColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                occlusionTextures[nonuniformEXT(materialIndex)],
                emissiveTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                textureDetailLodBias,
                drawState.pbrFactors,
                drawState.specializedRect1,
                drawState.specializedFlipbook0,
                drawState.specializedFlipbook1);
        } else {
            linearColor = evaluateWorldMaterial(
                  linearColor,
                  materialUv,
                  worldPosition,
                  vertexNormal,
                  vertexTangent,
                  worldView.cameraPosition.xyz,
                  worldView.cameraForward.xyz,
                  worldView.cameraTarget.xyz,
                  normalTextures[nonuniformEXT(materialIndex)],
                  metallicRoughnessTextures[nonuniformEXT(materialIndex)],
                  occlusionTextures[nonuniformEXT(materialIndex)],
                  emissiveTextures[nonuniformEXT(materialIndex)],
                  environmentTextures[nonuniformEXT(materialIndex)],
                  textureDetailLodBias,
                  separateEyeCoat
                      ? vec4(0.0, drawState.pbrFactors.yzw)
                      : bakedEyeDiffuse
                          ? vec4(
                                drawState.pbrFactors.x * 0.8,
                                drawState.pbrFactors.yzw)
                          : drawState.pbrFactors,
                  drawState.emissiveAndCamera.rgb,
                  separateEyeCoat
                      ? 0.0
                      : bakedEyeDiffuse
                          ? -2.0
                      : (materialMode > 1.5 && materialMode < 2.5 &&
                         drawState.specializedTimingFlagsAtlas.y > 4.5 &&
                         drawState.specializedTimingFlagsAtlas.y < 5.5)
                            ? clamp(drawState.specializedRect0.x, 0.0, 1.0)
                            : -1.0,
                  separateEyeCoat ? 0.0 : 1.0,
                  true);
        }
        if (layeredEyeCoat) {
            linearColor = evaluateLayeredEyeCoat(
                linearColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                worldView.cameraTarget.xyz,
                normalTextures[nonuniformEXT(materialIndex)],
                environmentTextures[nonuniformEXT(materialIndex)],
                0.0,
                layeredEffectMaterial.rect0,
                layeredEffectMaterial.rect1,
                layeredEffectMaterial.flipbook0.xyz);
        }
    }
    if (materialMode >= 1.5) {
        linearColor = applyReviewLightingProfile(
            linearColor,
            reviewAlbedo,
            vertexNormal,
            reviewCameraForward);
    }
    const float toneMappingExposure = 1.15;
    vec3 mapped = tonemapACESFilmic(max(linearColor, vec3(0.0)), toneMappingExposure);
    vec3 resolvedColor = sceneColorPostEnabled
        ? mapped
        : linearToSrgb(mapped);
    writeWorldColor(vec4(resolvedColor, alpha));
