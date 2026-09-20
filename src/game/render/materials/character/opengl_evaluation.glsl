
            FragBlendAlpha = vec4(0.0);
            if (uMaterialMode > 2.5 && uMaterialMode < 3.5) {
                if (gl_FrontFacing) discard;
                FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                return;
            }
            if (uMaterialMode > 0.5 && uMaterialMode < 1.5) {
                FragColor = evalFireTailExact();
                return;
            }
            if (uMaterialMode > 26.5 && uMaterialMode < 27.5) {
                vec4 surface = evalNativeLayeredUnlitDisplaced();
                if (uMaterialFlags > 2.5 && uMaterialFlags < 3.5) {
                    bool useAuthoredShadowColor = uMaterialFlags > 3.3;
                    vec2 smokeUv = nativeLayeredMaterialUv();
                    vec4 authoredResponse = useAuthoredShadowColor
                        ? sampleTextureWithWrap(
                            uEmissiveTexture,
                            smokeUv,
                            dFdx(smokeUv),
                            dFdy(smokeUv))
                        : vec4(surface.rgb, 0.0);
                    surface.rgb = applyVolumeRimLighting(
                        surface.rgb,
                        authoredResponse,
                        useAuthoredShadowColor);
                }
                const float toneMappingExposure = __PHLOSION_PBR_TONEMAP_EXPOSURE__;
                // Preserve the hue separation in Scarlet's authored HDR
                // flame layers; the generic viewer ACES fit washes both into
                // nearly the same pale yellow.
                vec3 mapped = linearToneMapping(
                    max(surface.rgb, vec3(0.0)),
                    toneMappingExposure);
                FragColor = vec4(resolveWorldSceneColor(mapped), surface.a);
                return;
            }
__AUTOCHESS_FIELD_EVALUATION__
            vec4 tex = vec4(1.0);
            vec3 outLinear = clamp(vColor.rgb * uVertexColorMul.rgb, 0.0, 1.0);
            bool animatedEyeMaterial =
                (uMaterialMode > 28.5 && uMaterialMode < 30.5);
            vec2 rawUv = animatedEyeMaterial
                ? vec2(
                      vUv.x * uLightProjectionUvRowU.x +
                          uLightProjectionUvRowU.z,
                      vUv.y * uLightProjectionUvRowU.y +
                          uLightProjectionUvRowU.w)
                : vUv;
            vec2 wrappedUv = vec2(applyWrap(rawUv.x, uWrapS), applyWrap(rawUv.y, uWrapT));
            bool clampS = abs(uWrapS - 33071.0) < 0.5;
            bool clampT = abs(uWrapT - 33071.0) < 0.5;
            if (clampS || clampT) {
                wrappedUv = clampWrappedUvToTexelCenter(wrappedUv);
            }
            // Keep derivative source aligned with D3D12 path for exact sampler parity.
            vec2 uvDx = dFdx(wrappedUv);
            vec2 uvDy = dFdy(wrappedUv);
            bool explicitMaterialDebug =
                uMaterialFlipbook1.w < -100.5;
            float pbrDebugView = explicitMaterialDebug
                ? -uMaterialFlipbook1.w - 100.0
                : (animatedEyeMaterial ? 0.0 : uMaterialFlipbook1.w);
            if (uUseTexture > 0.5) {
                tex = sampleTextureWithWrap(uTexture, wrappedUv, uvDx, uvDy);
                outLinear = clamp(tex.rgb, 0.0, 1.0) * outLinear;
            }
            vec3 reviewAlbedo = outLinear;
            float outA = clamp(vColor.a * uVertexColorMul.a * tex.a, 0.0, 1.0);
            float alphaWindowMin = clamp(uAlphaWindowMin, 0.0, 1.0);
            float alphaWindowMax = clamp(uAlphaWindowMax, 0.0, 1.0);
            if (alphaWindowMax < 1.0 || alphaWindowMin > 0.0) {
                if (outA < alphaWindowMin || outA >= alphaWindowMax) discard;
            }
            if (uAlphaMode < 0.5) {
                outA = clamp(vColor.a * uVertexColorMul.a, 0.0, 1.0);
            } else if (uAlphaMode < 1.5) {
                if (outA < clamp(uAlphaCutoff, 0.0, 1.0)) discard;
                outA = clamp(vColor.a * uVertexColorMul.a, 0.0, 1.0);
            }
            if (uMaterialMode >= 1.5 && pbrDebugView > 0.5) {
                vec3 dbg = vec3(0.0);
                if (pbrDebugView < 1.5) {
                    // 1: Raw base-color texture sample.
                    dbg = clamp(tex.rgb, 0.0, 1.0);
                } else if (pbrDebugView < 2.5) {
                    // 2: Authored tint-resolved albedo without lighting.
                    dbg = (uMaterialMode > 33.5 && uMaterialMode < 34.5)
                        ? viewAngleLayerBase(outLinear)
                        : clamp(outLinear, 0.0, 1.0);
                } else if (pbrDebugView < 3.5) {
                    // 3: Normal map sample.
                    dbg = uUseNormalTexture > 0.5
                        ? sampleTextureWithWrap(
                              uNormalTexture, wrappedUv, uvDx, uvDy).rgb
                        : vec3(0.5, 0.5, 1.0);
                } else if (pbrDebugView < 4.5) {
                    // 4: Roughness channel.
                    float rgh = uUseMetallicRoughnessTexture > 0.5
                        ? sampleTextureWithWrap(
                              uMetallicRoughnessTexture,
                              wrappedUv,
                              uvDx,
                              uvDy).g
                        : 1.0;
                    dbg = vec3(rgh);
                } else if (pbrDebugView < 5.5) {
                    // 5: Metallic channel.
                    float met = uUseMetallicRoughnessTexture > 0.5
                        ? sampleTextureWithWrap(
                              uMetallicRoughnessTexture,
                              wrappedUv,
                              uvDx,
                              uvDy).b
                        : 0.0;
                    dbg = vec3(met);
                } else if (pbrDebugView < 6.5) {
                    // 6: AO channel.
                    float ao = uUseOcclusionTexture > 0.5
                        ? sampleTextureWithWrap(
                              uOcclusionTexture, wrappedUv, uvDx, uvDy).r
                        : 1.0;
                    dbg = vec3(ao);
                } else if (pbrDebugView < 7.5) {
                    // 7: Emissive sample.
                    dbg = uUseEmissiveTexture > 0.5
                        ? sampleTextureWithWrap(
                              uEmissiveTexture, wrappedUv, uvDx, uvDy).rgb
                        : vec3(0.0);
                }
                FragColor = vec4(resolveWorldSceneColor(dbg), 1.0);
                return;
            }
            if (uMaterialMode >= 1.5) {
                bool layeredEyeCoat =
                    (uMaterialMode > 27.5 && uMaterialMode < 28.5) ||
                    (uMaterialMode > 29.5 && uMaterialMode < 30.5);
                bool bakedEyeDiffuse =
                    layeredEyeCoat && uMaterialRect1.w < -0.5;
                bool separateEyeCoat =
                    layeredEyeCoat && !bakedEyeDiffuse;
                bool facialOverlay =
                    uMaterialMode > 30.5 &&
                    uMaterialFlags > 3.5 &&
                    uMaterialFlags < 4.5;
                bool layeredCharacter =
                    uMaterialMode > 31.5 && uMaterialMode < 32.5;
                bool subsurface =
                    uMaterialMode > 32.5 && uMaterialMode < 33.5;
                bool viewAngleLayer =
                    uMaterialMode > 33.5 && uMaterialMode < 34.5;
                bool refractiveEye =
                    uMaterialMode > 34.5 && uMaterialMode < 35.5;
                if (viewAngleLayer) {
                    reviewAlbedo = viewAngleLayerBase(outLinear);
                }
                // The generic PBR path deliberately boosts normal XY by
                // 1.25. Z-A IkCharacter's NormalHeight is already authored
                // at final strength, so cancel only that path's boost.
                vec3 n = computeMappedNormal(
                    wrappedUv,
                    uvDx,
                    uvDy,
                    (layeredCharacter || refractiveEye ||
                     bakedEyeDiffuse) ? 0.8 : 1.0);
                // NormalMap1 is the source EyeClearCoat highlight-normal
                // input, not a replacement for the eye shell's base surface
                // normal. Applying it to generic PBR turns its small authored
                // highlight sphere into full-eye white striping. The cook
                // already resolves that footprint into EyeFinal; retain a
                // stable shell normal until the projected/shadow/environment
                // scene resources can reproduce the source combination.
                vec3 eyeSurfaceNormal = safeNormalize(
                    vWorldNormal,
                    vec3(0.0, 1.0, 0.0));
                if (subsurface) {
                    outLinear = applySubsurfaceSurface(
                        outLinear,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy,
                        uMaterialFlags);
                } else if (layeredCharacter || refractiveEye) {
                    outLinear = applyLayeredCharacter(
                        outLinear,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy,
                        refractiveEye);
                } else if (facialOverlay) {
                    outLinear = applyFacialOverlay(
                        outLinear,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy);
                } else if (viewAngleLayer) {
                    vec3 primaryColor = viewAngleLayerBase(outLinear);
                    outLinear = applyWorldLitModel(
                        primaryColor,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy);
                    outLinear = applyViewAngleLayerLayer(
                        outLinear,
                        primaryColor,
                        n,
                        wrappedUv,
                        uvDx,
                        uvDy);
                } else {
                    outLinear = applyWorldLitModel(
                        outLinear,
                        separateEyeCoat ? eyeSurfaceNormal : n,
                        wrappedUv,
                        uvDx,
                        uvDy);
                }
                if (layeredEyeCoat) {
                    outLinear = applyLayeredEyeCoat(
                        outLinear,
                        eyeSurfaceNormal);
                }
            }
            if (uMaterialMode >= 1.5) {
                outLinear = applyReviewLightingProfile(
                    outLinear,
                    reviewAlbedo,
                    vWorldNormal,
                    uCameraForward);
            }
            const float toneMappingExposure = __PHLOSION_PBR_TONEMAP_EXPOSURE__;
            const float toneMappingMode = 1.0;
            vec3 mapped = applyViewerToneMapping(max(outLinear, vec3(0.0)), toneMappingMode, toneMappingExposure);
            vec3 outSrgb = resolveWorldSceneColor(mapped);
            float mainOutA = outA;
            if (uDualSourceBlendEnabled > 0.5) {
                mainOutA = floor(clamp(outA, 0.0, 1.0) * 63.0 + 0.5) / 63.0;
            }
            FragColor = vec4(outSrgb, mainOutA);
            FragBlendAlpha = (uDualSourceBlendEnabled > 0.5)
                ? vec4(0.0, 0.0, 0.0, clamp(outA, 0.0, 1.0))
                : vec4(0.0);
