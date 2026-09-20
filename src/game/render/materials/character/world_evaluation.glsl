
    float alphaMode = pushData.materialParams.x;
    float alphaCutoff = pushData.materialParams.y;
    float materialMode = pushData.materialParams.w;

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (gl_FrontFacing) discard;
        writeWorldColor(vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    if (materialMode > 0.5 && materialMode < 1.5) {
        LayeredEffectMaterialState layeredEffectMaterial = LayeredEffectMaterialState(
            worldSpecializedMaterial.timingFlagsAtlas,
            worldSpecializedMaterial.rect0,
            worldSpecializedMaterial.rect1,
            worldSpecializedMaterial.flipbook0,
            worldSpecializedMaterial.flipbook1);
        writeWorldColor(evaluateFlameEffect(baseColorTexture, layeredEffectMaterial));
        return;
    }
    if (materialMode > 26.5 && materialMode < 27.5) {
        LayeredEffectMaterialState nativeUnlitMaterial = LayeredEffectMaterialState(
            worldSpecializedMaterial.timingFlagsAtlas,
            worldSpecializedMaterial.rect0,
            worldSpecializedMaterial.rect1,
            worldSpecializedMaterial.flipbook0,
            worldSpecializedMaterial.flipbook1);
        vec4 surface = evaluateNativeLayeredUnlitDisplaced(
            baseColorTexture,
            metallicRoughnessTexture,
            nativeUnlitMaterial);
        if (worldSpecializedMaterial.timingFlagsAtlas.y > 2.5 &&
            worldSpecializedMaterial.timingFlagsAtlas.y < 3.5) {
            bool useAuthoredShadowColor =
                worldSpecializedMaterial.timingFlagsAtlas.y > 3.3;
            vec4 authoredResponse = useAuthoredShadowColor
                ? texture(
                    emissiveTexture,
                    nativeLayeredMaterialUv(nativeUnlitMaterial))
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
        vec3 nativeResolved = pushData.shadingParams.w > 0.5
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
                      worldSpecializedMaterial.lightProjectionUvRowU.x +
                  worldSpecializedMaterial.lightProjectionUvRowU.z,
              vertexUv.y *
                      worldSpecializedMaterial.lightProjectionUvRowU.y +
                  worldSpecializedMaterial.lightProjectionUvRowU.w)
        : vertexUv;
    float textureDetailLodBias =
        ((materialMode >= 1.5 && materialMode < 2.5) ||
         (materialMode > 27.5 && materialMode < 30.5) ||
         (materialMode > 31.5 && materialMode < 35.5))
            ? worldSpecializedMaterial.flipbook1.z
            : 0.0;
    vec4 sampled = sampleWorldMaterialTexture(
        baseColorTexture, materialUv, textureDetailLodBias);
    vec3 linearColor = clamp(sampled.rgb, 0.0, 1.0) * clamp(vertexColor.rgb, 0.0, 1.0);
    vec3 reviewAlbedo = linearColor;
    float alpha = clamp(vertexColor.a * sampled.a, 0.0, 1.0);

    float alphaWindowMin = clamp(pushData.shadingParams.x, 0.0, 1.0);
    float alphaWindowMax = clamp(pushData.shadingParams.y, 0.0, 1.0);
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
        worldSpecializedMaterial.flipbook1.w < -100.5;
    float pbrDebugView = explicitMaterialDebug
        ? -worldSpecializedMaterial.flipbook1.w - 100.0
        : 0.0;
    int materialDebugFlags = int(
        worldSpecializedMaterial.timingFlagsAtlas.y + 0.5);
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
                      worldSpecializedMaterial.rect0,
                      worldSpecializedMaterial.flipbook1)
                : clamp(linearColor, 0.0, 1.0);
        } else if (pbrDebugView < 3.5) {
            // 3: Normal map sample.
            debugColor = (materialDebugFlags & (1 << 0)) != 0
                ? sampleWorldMaterialTexture(
                      normalTexture,
                      materialUv,
                      textureDetailLodBias).rgb
                : vec3(0.5, 0.5, 1.0);
        } else if (pbrDebugView < 4.5) {
            // 4: Roughness channel.
            float roughness = (materialDebugFlags & (1 << 1)) != 0
                ? sampleWorldMaterialTexture(
                      metallicRoughnessTexture,
                      materialUv,
                      textureDetailLodBias).g
                : 1.0;
            debugColor = vec3(roughness);
        } else if (pbrDebugView < 5.5) {
            // 5: Metallic channel.
            float metallic = (materialDebugFlags & (1 << 1)) != 0
                ? sampleWorldMaterialTexture(
                      metallicRoughnessTexture,
                      materialUv,
                      textureDetailLodBias).b
                : 0.0;
            debugColor = vec3(metallic);
        } else if (pbrDebugView < 6.5) {
            // 6: AO channel.
            float occlusion = (materialDebugFlags & (1 << 2)) != 0
                ? sampleWorldMaterialTexture(
                      occlusionTexture,
                      materialUv,
                      textureDetailLodBias).r
                : 1.0;
            debugColor = vec3(occlusion);
        } else if (pbrDebugView < 7.5) {
            // 7: Emissive sample.
            debugColor = (materialDebugFlags & (1 << 3)) != 0
                ? sampleWorldMaterialTexture(
                      emissiveTexture,
                      materialUv,
                      textureDetailLodBias).rgb
                : vec3(0.0);
        }
        vec3 resolvedDebugColor = pushData.shadingParams.w > 0.5
            ? debugColor
            : linearToSrgb(debugColor);
        writeWorldColor(vec4(resolvedDebugColor, 1.0));
        return;
    }

    // The transient preview profile is normally transported in the
    // camera-forward length. Vulkan can coalesce view state across the
    // editor's scene and inspector passes, so also recognize the exact
    // recovered Z-A diffuse carrier. It is attached only by the Inspector's
    // Z-A Source Stage profile.
    ivec2 stageDiffuseProbeSize = textureSize(lightProjectionTexture, 0);
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
            layeredEyeCoat &&
            worldSpecializedMaterial.rect1.w < -0.5;
        bool separateEyeCoat =
            layeredEyeCoat && !bakedEyeDiffuse;
        bool facialOverlay =
            materialMode > 30.5 &&
            worldSpecializedMaterial.timingFlagsAtlas.y > 3.5 &&
            worldSpecializedMaterial.timingFlagsAtlas.y < 4.5;
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
                worldSpecializedMaterial.rect0,
                worldSpecializedMaterial.flipbook1);
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
                normalTexture,
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                environmentTexture,
                textureDetailLodBias,
                worldSpecializedMaterial.timingFlagsAtlas.y,
                pushData.pbrFactors,
                pushData.emissiveAndCamera.rgb);
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
                baseColorTexture,
                normalTexture,
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                environmentTexture,
                lightProjectionTexture,
                textureDetailLodBias,
                pushData.pbrFactors,
                pushData.emissiveAndCamera.rgb,
                worldSpecializedMaterial.rect0,
                worldSpecializedMaterial.rect1,
                worldSpecializedMaterial.flipbook0,
                worldSpecializedMaterial.flipbook1,
                worldSpecializedMaterial.lightProjectionUvRowV.w,
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
                normalTexture,
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                textureDetailLodBias,
                pushData.pbrFactors.x,
                pushData.pbrFactors.w,
                worldSpecializedMaterial.rect0.w > 0.5);
        } else if (viewAngleLayer) {
            vec3 primaryColor = viewAngleLayerBase(
                linearColor,
                worldSpecializedMaterial.rect0,
                worldSpecializedMaterial.flipbook1);
            linearColor = evaluateWorldMaterial(
                primaryColor,
                materialUv,
                worldPosition,
                vertexNormal,
                vertexTangent,
                worldView.cameraPosition.xyz,
                worldView.cameraForward.xyz,
                worldView.cameraTarget.xyz,
                normalTexture,
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                environmentTexture,
                textureDetailLodBias,
                pushData.pbrFactors,
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
                metallicRoughnessTexture,
                occlusionTexture,
                emissiveTexture,
                environmentTexture,
                textureDetailLodBias,
                pushData.pbrFactors,
                worldSpecializedMaterial.rect1,
                worldSpecializedMaterial.flipbook0,
                worldSpecializedMaterial.flipbook1);
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
                  normalTexture,
                  metallicRoughnessTexture,
                  occlusionTexture,
                  emissiveTexture,
                  environmentTexture,
                  textureDetailLodBias,
                  separateEyeCoat
                      ? vec4(0.0, pushData.pbrFactors.yzw)
                      : bakedEyeDiffuse
                          ? vec4(
                                pushData.pbrFactors.x * 0.8,
                                pushData.pbrFactors.yzw)
                          : pushData.pbrFactors,
                  pushData.emissiveAndCamera.rgb,
                  separateEyeCoat
                      ? 0.0
                      : bakedEyeDiffuse
                          ? -2.0
                      : (materialMode > 1.5 && materialMode < 2.5 &&
                         worldSpecializedMaterial.timingFlagsAtlas.y > 4.5 &&
                         worldSpecializedMaterial.timingFlagsAtlas.y < 5.5)
                            ? clamp(worldSpecializedMaterial.rect0.x, 0.0, 1.0)
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
                normalTexture,
                environmentTexture,
                0.0,
                worldSpecializedMaterial.rect0,
                worldSpecializedMaterial.rect1,
                worldSpecializedMaterial.flipbook0.xyz);
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
    vec3 resolvedColor = pushData.shadingParams.w > 0.5
        ? mapped
        : linearToSrgb(mapped);
    writeWorldColor(vec4(resolvedColor, alpha));
