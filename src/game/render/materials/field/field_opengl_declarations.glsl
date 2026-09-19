vec4 sampleGroundTexture(sampler2D tex, vec2 uv) {
        return texture(tex, uv, -2.0);
    }
    vec2 fieldCloudTextureUv(vec3 worldPosition);
    float evaluateFieldProjectedCloud();
    float evaluateFieldProjectedShadow();
    float evaluateFieldProjectedLighting(float toon);
    vec3 applyGroundCliffSharedLighting(vec3 surface) {
        const vec3 shadowColor = vec3(0.235, 0.361, 0.391);
        float light =
            evaluateFieldProjectedLighting(1.0);
        return mix(shadowColor, vec3(1.0), light) * surface;
    }
    vec3 evaluateFieldGroundSurface() {
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 blendUv = vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
        vec2 uv2 = vec2(vSourceUv2.x, 1.0 - vSourceUv2.y);
        vec4 ground01 = sampleGroundTexture(uTexture, uv0);
        vec4 ground02 = sampleGroundTexture(uNormalTexture, uv0);
        vec4 grass02 =
            sampleGroundTexture(uMetallicRoughnessTexture, uv0);
        vec4 grass01 = sampleGroundTexture(uOcclusionTexture, uv0);
        float blend =
            clamp(sampleGroundTexture(uEnvTexture, blendUv).r,
                  0.0,
                  1.0);
        vec4 grassMask =
            sampleGroundTexture(uEmissiveTexture, uv2);
        vec3 ground = mix(ground01.rgb, ground02.rgb, blend);
        vec3 grass = mix(grass02.rgb, grass01.rgb, blend);
        vec3 surface =
            mix(ground, grass, clamp(grassMask.a, 0.0, 1.0));
        vec4 authoredVertexColor = vColor * uVertexColorMul;
        vec3 sourceSurface =
            grassMask.rgb * authoredVertexColor.rgb * surface +
            max(uEmissiveFactor, vec3(0.0)) *
                (1.0 - clamp(authoredVertexColor.a, 0.0, 1.0));
        return applyGroundCliffSharedLighting(sourceSurface);
    }
    vec3 evaluateFieldCliffSurface() {
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 blendUv = vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
        vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
        vec2 uv2 = vec2(vSourceUv2.x, 1.0 - vSourceUv2.y);
        vec4 cliffTex = sampleGroundTexture(uTexture, uv1);
        vec4 ground02 = sampleGroundTexture(uNormalTexture, uv0);
        vec4 ground01 =
            sampleGroundTexture(uMetallicRoughnessTexture, uv0);
        float blend =
            clamp(sampleGroundTexture(uOcclusionTexture, blendUv).r,
                  0.0,
                  1.0);
        vec4 borderTex = sampleGroundTexture(uEmissiveTexture, uv2);
        vec3 normal = normalize(vWorldNormal);
        vec3 viewDirection = normalize(uCameraPos - vWorldPos);
        float rimSpan =
            max(uMetallicFactor, uNormalScale) - uNormalScale;
        float rim = rimSpan > 0.0
            ? clamp(
                  ((1.0 - dot(normal, viewDirection)) - uNormalScale) /
                      rimSpan,
                  0.0,
                  1.0) *
                  uRoughnessFactor
            : 0.0;
        vec3 cliff =
            cliffTex.rgb +
            max(uEmissiveFactor, vec3(0.0)) * rim * cliffTex.a;
        vec3 grass = mix(ground02.rgb, ground01.rgb, blend);
        vec3 surface =
            mix(cliff, grass, clamp(borderTex.a, 0.0, 1.0));
        vec4 authoredVertexColor = vColor * uVertexColorMul;
        vec3 sourceSurface =
            borderTex.rgb * authoredVertexColor.rgb * surface;
        return applyGroundCliffSharedLighting(sourceSurface);
    }


    vec3 foliageColorBalance(
        vec3 color,
        float hueShift,
        float saturation,
        float value) {
        vec3 hsv = rgbToHsv(max(color, vec3(0.0)));
        hsv.x = fract(hsv.x + hueShift);
        hsv.y = clamp(hsv.y * saturation, 0.0, 1.0);
        hsv.z *= value;
        return hsvToRgb(hsv);
    }
    float foliageAcceptedLightCoordinate(vec3 normal) {
        const vec3 acceptedLightDirection =
            vec3(0.32, -0.42, 0.85);
        float sourceDot =
            dot(normalize(normal), acceptedLightDirection);
        return mix(
            0.12,
            0.96,
            clamp((sourceDot + 0.15) / 1.0, 0.0, 1.0));
    }
    vec3 foliageProjectionCompensation(vec3 shadowColor) {
        float cloud = evaluateFieldProjectedLighting(1.0);
        vec3 projectedLighting =
            mix(max(shadowColor, vec3(0.0)), vec3(1.0), cloud);
        return mix(vec3(1.0), projectedLighting, 0.25);
    }
    vec3 foliageColorize(
        vec3 baseColor,
        vec3 paletteColor,
        float factor) {
        const vec3 luminanceWeights =
            vec3(0.2126, 0.7152, 0.0722);
        float baseLuminance =
            dot(max(baseColor, vec3(0.0)), luminanceWeights);
        float paletteLuminance =
            max(dot(max(paletteColor, vec3(0.0)), luminanceWeights),
                0.0001);
        vec3 paletteAtBaseLuminance =
            paletteColor * (baseLuminance / paletteLuminance);
        return mix(
            baseColor,
            paletteAtBaseLuminance,
            clamp(factor, 0.0, 1.0));
    }
    vec3 foliageAcceptedDisplayTransform(vec3 color) {
        // The accepted Route 1 Blender checkpoint was judged through its
        // AgX high-contrast display transform.  A restrained filmic
        // shoulder here translates that material calibration into the
        // engine's otherwise direct-sRGB LGPE path without changing the
        // route's already-qualified ground, cliff, grass, or flower
        // presentation.
        vec3 exposed = max(color, vec3(0.0)) * 0.72;
        vec3 mapped = clamp(
            (exposed * (2.51 * exposed + 0.03)) /
                (exposed * (2.43 * exposed + 0.59) + 0.14),
            0.0,
            1.0);
        float luminance =
            dot(mapped, vec3(0.2126, 0.7152, 0.0722));
        float highlight =
            smoothstep(0.35, 0.85, luminance);
        float saturationRetention =
            mix(0.9, 0.55, highlight);
        return mix(
            vec3(luminance),
            mapped,
            saturationRetention);
    }
    vec4 evaluateTunedCanopySurface(
        float hueShift,
        float saturation,
        float value) {
        // Canonical cache rows are uploaded in source top-down order.
        // As with the reviewed BuildModel flowers and sign, raw source
        // UV0 is the presentation-space coordinate; applying the source
        // program's vertical convention again selects the opposite atlas
        // rows.
        vec2 uv0 = vUv;
        vec4 texture01 = texture(uTexture, uv0, 0.0);
        if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) {
            discard;
        }
        float lightCoordinate =
            foliageAcceptedLightCoordinate(vWorldNormal);
        vec3 texture02 =
            texture(
                uNormalTexture,
                vec2(0.5, 1.0 - lightCoordinate),
                0.0).rgb;
        float texture03 =
            texture(uMetallicRoughnessTexture, uv0, 0.0).r;
        vec3 acceptedLocalSurface =
            clamp(texture01.rgb + texture02 * texture03, 0.0, 1.0);
        vec3 acceptedColor = foliageColorBalance(
            acceptedLocalSurface,
            hueShift,
            saturation,
            value);
        vec3 shadowColor =
            max(
                vec3(
                    uNormalScale,
                    uMetallicFactor,
                    uRoughnessFactor),
                vec3(0.0));
        vec3 finalColor =
            acceptedColor *
            foliageProjectionCompensation(shadowColor);
        return vec4(
            foliageAcceptedDisplayTransform(finalColor),
            texture01.a);
    }
    vec4 evaluateTunedLayeredFoliageSurface(
        float hueShift,
        float saturation,
        float value,
        bool darkConifer) {
        vec2 uv0 = vUv;
        vec4 texture01 = texture(uTexture, uv0, 0.0);
        if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) {
            discard;
        }
        vec3 texture02 = texture(uNormalTexture, uv0, 0.0).rgb;
        float lightCoordinate =
            foliageAcceptedLightCoordinate(vWorldNormal);
        float lightToon =
            texture(
                uEmissiveTexture,
                vec2(lightCoordinate, 0.5),
                0.0).r;
        vec3 directionalLightColor =
            vec3(
                uMaterialRect0.z,
                uMaterialRect0.w,
                uMaterialRect1.x);
        vec3 acceptedLocalSurface = clamp(
            texture01.rgb +
                texture02 * directionalLightColor *
                    clamp(lightToon, 0.0, 1.0),
            0.0,
            1.0);
        if (darkConifer) {
            vec3 greenColor =
                vec3(uNormalScale, uMetallicFactor, uRoughnessFactor);
            float inverseShade =
                1.0 -
                dot(
                    clamp(texture02, 0.0, 1.0),
                    vec3(0.2126, 0.7152, 0.0722));
            acceptedLocalSurface = foliageColorize(
                acceptedLocalSurface,
                greenColor,
                inverseShade);
        }
        vec3 acceptedColor = foliageColorBalance(
            acceptedLocalSurface,
            hueShift,
            saturation,
            value);
        vec3 finalColor =
            acceptedColor *
            foliageProjectionCompensation(uEmissiveFactor);
        vec3 displayedColor =
            foliageAcceptedDisplayTransform(finalColor);
        return vec4(
            displayedColor,
            texture01.a);
    }
    vec4 evaluateCanopySurface() {
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
        vec4 texture01 = texture(uTexture, uv0, 0.0);
        if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) discard;
        vec3 texture02 = texture(uNormalTexture, uv1, 0.0).rgb;
        float texture03 =
            texture(uMetallicRoughnessTexture, uv0, 0.0).r;
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float toon = clamp(
            texture(
                uOcclusionTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                0.0).r,
            0.0,
            1.0);
        vec3 viewDirection = normalize(uCameraPos - vWorldPos);
        float rimMin = uMaterialTimeSec;
        float rimMax = uMaterialFlags;
        float rimStrength = uMaterialAtlasSize.x;
        float rimSpan = max(rimMax, rimMin) - rimMin;
        float rim = rimSpan > 0.0
            ? clamp(
                  ((1.0 - dot(normal, viewDirection)) - rimMin) /
                      rimSpan,
                  0.0,
                  1.0) *
                  rimStrength
            : 0.0;
        float lightGate =
            1.0 -
            clamp(
                (1.0 - normalDotLight) * 12.7408008575,
                0.0,
                1.0);
        vec3 secondaryDirection =
            mix(
                viewDirection,
                -sourceSunRay,
                uMaterialFlipbook0.w);
        float secondaryMin = uMaterialFlipbook1.z;
        float secondaryMax = uMaterialFlipbook1.w;
        float secondarySpan =
            max(secondaryMax, secondaryMin) - secondaryMin;
        float secondaryCoordinate =
            clamp(
                1.0 - dot(normal, secondaryDirection),
                0.0,
                1.0);
        float secondary = secondarySpan > 0.0
            ? clamp(
                  (secondaryCoordinate - secondaryMin) /
                      secondarySpan,
                  0.0,
                  1.0)
            : 0.0;
        vec3 shadowColor =
            max(vec3(uNormalScale, uMetallicFactor, uRoughnessFactor),
                vec3(0.0));
        vec3 rimColor =
            max(
                vec3(
                    uMaterialAtlasSize.y,
                    uMaterialRect0.x,
                    uMaterialRect0.y),
                vec3(0.0));
        vec3 rimColor02 =
            max(
                vec3(
                    uMaterialRect0.z,
                    uMaterialRect0.w,
                    uMaterialRect1.x),
                vec3(0.0));
        vec3 surface =
            texture01.rgb +
            texture02 * rim * rimColor +
            max(uEmissiveFactor, vec3(0.0)) *
                (1.0 - secondary) +
            texture03 * lightGate * rimColor02;
        return vec4(
            mix(
                shadowColor,
                vec3(1.0),
                evaluateFieldProjectedLighting(toon)) *
                surface,
            texture01.a);
    }
    vec4 evaluateLayeredFoliageSurface(
        bool useProjectedCloud,
        bool useCanonicalTextureUv) {
        // Exact modes retain the decoded 1-V operation. Reviewed
        // BuildModel grass02 uses canonical presentation UVs, matching
        // its exact Blender checkpoint's direct model-texcoord links.
        vec2 uv0 = useCanonicalTextureUv
            ? vUv
            : vec2(vUv.x, 1.0 - vUv.y);
        vec4 texture01 = texture(uTexture, uv0, 0.0);
        if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) discard;
        vec3 texture02 = texture(uNormalTexture, uv0, 0.0).rgb;
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        vec2 toonUv =
            vec2(toonCoordinate, 1.0 - toonCoordinate);
        float toon = clamp(
            texture(uOcclusionTexture, toonUv, 0.0).r,
            0.0,
            1.0);
        float lightToon = clamp(
            texture(uEmissiveTexture, toonUv, 0.0).r,
            0.0,
            1.0);
        vec3 viewDirection = normalize(uCameraPos - vWorldPos);
        float edge =
            clamp(1.0 - dot(normal, viewDirection), 0.0, 1.0);
        float rimMin = uMaterialTimeSec;
        float rimMax = uMaterialFlags;
        float rimStrength = uMaterialAtlasSize.x;
        float rimSpan = max(rimMax, rimMin) - rimMin;
        float rim = rimSpan > 0.0
            ? clamp((edge - rimMin) / rimSpan, 0.0, 1.0) *
                  rimStrength
            : 0.0;
        float directional = clamp(edge * (5.0 / 3.0), 0.0, 1.0);
        vec3 greenColor =
            vec3(uNormalScale, uMetallicFactor, uRoughnessFactor);
        vec3 shadowColor = uEmissiveFactor;
        vec3 rimColor =
            vec3(
                uMaterialAtlasSize.y,
                uMaterialRect0.x,
                uMaterialRect0.y);
        vec3 directionalLightColor =
            vec3(
                uMaterialRect0.z,
                uMaterialRect0.w,
                uMaterialRect1.x);
        vec3 rimColor02 = uMaterialRect1.yzw;
        vec3 secondary =
            rim * rimColor +
            (1.0 - directional) * rimColor02 +
            lightToon * directionalLightColor;
        vec3 surface = texture01.rgb + texture02 * secondary;
        vec3 authored = surface * vColor.rgb;
        vec3 tinted =
            mix(greenColor, authored, clamp(vColor.a, 0.0, 1.0));
        // The source uses a shared ten-tap projected-depth PCF. Until its
        float light = useProjectedCloud
            ? evaluateFieldProjectedLighting(toon)
            : toon * evaluateFieldProjectedShadow();
        vec3 lighting =
            mix(shadowColor, vec3(1.0), light);
        return vec4(lighting * tinted, texture01.a);
    }


    vec4 evaluateFieldGrassSurface(bool withRim) {
        float sourceMipBias = uMaterialFlipbook0.w;
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
        vec2 blendUv =
            vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
        bool floorFoliageCard =
            !withRim && vSourceUv2.x < -2048.0;
        vec3 textureMap01 =
            texture(
                uTexture,
                uv0,
                sourceMipBias).rgb;
        vec3 textureMap02 =
            texture(uNormalTexture, uv0, sourceMipBias).rgb;
        vec4 greenHikari =
            texture(
                uMetallicRoughnessTexture,
                floorFoliageCard
                    ? vSourceUv1
                    : uv1,
                sourceMipBias);
        if (floorFoliageCard) {
            if (greenHikari.r >= clamp(uAlphaCutoff, 0.0, 1.0)) {
                discard;
            }
        } else if (
            greenHikari.a <= clamp(uAlphaCutoff, 0.0, 1.0)) {
            discard;
        }
        float greenBlend = clamp(
            texture(
                uOcclusionTexture,
                blendUv,
                sourceMipBias).r,
            0.0,
            1.0);
        float highlight = clamp(
            texture(
                uEmissiveTexture,
                uv1,
                sourceMipBias).r,
            0.0,
            1.0);
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float toon = clamp(
            texture(
                uEnvTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                sourceMipBias).r,
            0.0,
            1.0);
        vec3 sourceColor =
            vec3(uNormalScale, uMetallicFactor, uRoughnessFactor);
        vec3 decoration =
            mix(textureMap02, textureMap01, greenBlend) +
            sourceColor * (1.0 - highlight);
        vec3 authoredColor = vColor.rgb;
        if (!withRim && sourceMipBias > -1.0 &&
            normalize(vWorldNormal).y > 0.9) {
            // grass01_com_001 floor carriers use branch-coded Color0
            // values. The accepted source-geometry audit resolves their
            // horizontal lawn branch to this continuous raised-lawn tint.
            authoredColor =
                vec3(0.180392161, 0.482352942, 0.431372553);
        }
        vec3 surface =
            decoration * greenHikari.rgb * authoredColor;
        if (withRim) {
            vec3 viewDirection = normalize(uCameraPos - vWorldPos);
            float edge =
                clamp(
                    1.0 - dot(normal, viewDirection),
                    0.0,
                    1.0);
            float rimMin = uMaterialTimeSec;
            float rimMax = uMaterialFlags;
            float rimSpan = max(rimMax, rimMin) - rimMin;
            float rim = rimSpan > 0.0
                ? clamp((edge - rimMin) / rimSpan, 0.0, 1.0) *
                      uMaterialAtlasSize.x
                : 0.0;
            vec3 rimColor =
                vec3(
                    uMaterialAtlasSize.y,
                    uMaterialRect0.x,
                    uMaterialRect0.y);
            surface += rimColor * rim;
        }
        // The depth PCF remains held lit; the captured stationary
        // LightProjMap projection is represented separately.
        vec3 lighting = mix(
            uEmissiveFactor,
            vec3(1.0),
            evaluateFieldProjectedLighting(toon));
        vec3 result = lighting * surface;
        float alpha = greenHikari.a;
        if (!withRim) {
            vec3 onGameColor =
                vec3(
                    uMaterialTimeSec,
                    uMaterialFlags,
                    uMaterialAtlasSize.x);
            float onGameValue =
                clamp(uMaterialAtlasSize.y, 0.0, 1.0);
            result *=
                mix(vec3(1.0), onGameColor, onGameValue);
            alpha *= clamp(uMaterialRect0.x, 0.0, 1.0);
        }
        return vec4(result, alpha);
    }
    vec2 fieldCloudTextureUv(vec3 worldPosition) {
        vec4 position = vec4(worldPosition, 1.0);
        float sourceU = dot(position, uLightProjectionUvRowU);
        float sourceV = dot(position, uLightProjectionUvRowV);
        return vec2(sourceU, 1.0 - sourceV);
    }
    float evaluateFieldProjectedCloud() {
        return clamp(
            texture(
                uLightProjectionTexture,
                fieldCloudTextureUv(vWorldPos),
                0.0).r,
            0.0,
            1.0);
    }
    float evaluateFieldProjectedShadow() {
        if (uProjectedShadowParams.x < 0.5) return 1.0;
        vec4 shadowClip =
            uProjectedShadowMatrix * vec4(vWorldPos, 1.0);
        if (abs(shadowClip.w) <= 1e-8) return 1.0;
        vec3 shadowNdc = shadowClip.xyz / shadowClip.w;
        vec2 shadowUv = shadowNdc.xy * 0.5 + 0.5;
        if (any(lessThan(shadowUv, vec2(0.0))) ||
            any(greaterThan(shadowUv, vec2(1.0)))) {
            return 1.0;
        }
        float reference =
            shadowNdc.z * 0.5 + 0.5 -
            uProjectedShadowParams.z / shadowClip.w;
        const vec2 poisson[10] = vec2[10](
            vec2(-0.8405, -0.0740),
            vec2(-0.3262, -0.4058),
            vec2(-0.2034,  0.4573),
            vec2(-0.6985,  0.6206),
            vec2( 0.9635, -0.1944),
            vec2( 0.4734, -0.4800),
            vec2( 0.5195,  0.7670),
            vec2( 0.1855, -0.8945),
            vec2( 0.5074,  0.0650),
            vec2(-0.3219,  0.5954));
        ivec2 textureExtent =
            max(textureSize(uProjectedShadowTexture, 0), ivec2(1));
        float projectedShadow = 0.0;
        for (int tap = 0; tap < 10; ++tap) {
            vec2 tapUv =
                shadowUv + poisson[tap] *
                (0.0004 * uProjectedShadowParams.y);
            vec2 texelPosition =
                tapUv * vec2(textureExtent) - vec2(0.5);
            ivec2 baseTexel = ivec2(floor(texelPosition));
            vec2 blend = fract(texelPosition);
            float comparisons[4];
            for (int corner = 0; corner < 4; ++corner) {
                ivec2 cornerOffset =
                    ivec2(corner & 1, (corner >> 1) & 1);
                ivec2 texel = baseTexel + cornerOffset;
                float storedDepth = 1.0;
                if (all(greaterThanEqual(texel, ivec2(0))) &&
                    all(lessThan(texel, textureExtent))) {
                    vec3 packedDepth =
                        texelFetch(
                            uProjectedShadowTexture,
                            texel,
                            0).rgb * 255.0;
                    storedDepth =
                        dot(
                            packedDepth,
                            vec3(65536.0, 256.0, 1.0)) /
                        16777215.0;
                }
                comparisons[corner] =
                    reference <= storedDepth ? 1.0 : 0.0;
            }
            float row0 = mix(comparisons[0], comparisons[1], blend.x);
            float row1 = mix(comparisons[2], comparisons[3], blend.x);
            projectedShadow += mix(row0, row1, blend.y) * 0.1;
        }
        return projectedShadow;
    }
    float evaluateFieldProjectedLighting(float toon) {
        return min(
            clamp(toon, 0.0, 1.0) *
                evaluateFieldProjectedShadow(),
            evaluateFieldProjectedCloud());
    }
    vec4 evaluateGroundCoverSurface() {
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
        vec4 texture01 = texture(uTexture, uv0, 0.0);
        vec4 texture02 = texture(uNormalTexture, uv1, 0.0);
        float texture03 = clamp(
            texture(uMetallicRoughnessTexture, uv1, 0.0).r,
            0.0,
            1.0);
        vec4 base = mix(texture01, texture02, texture03);
        float alpha =
            base.a * vColor.a *
            clamp(uMaterialTimeSec, 0.0, 1.0) *
            clamp(uMaterialAtlasSize.x, 0.0, 1.0);
        if (alpha <= clamp(uAlphaCutoff, 0.0, 1.0)) discard;
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float toon = clamp(
            texture(
                uOcclusionTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                0.0).r,
            0.0,
            1.0);
        float projectedLight =
            evaluateFieldProjectedLighting(toon);
        vec3 onGameColor =
            vec3(
                uMaterialAtlasSize.y,
                uMaterialRect0.x,
                uMaterialRect0.y);
        vec3 surface =
            base.rgb * vColor.rgb *
            mix(
                vec3(1.0),
                onGameColor,
                clamp(uMaterialFlags, 0.0, 1.0));
        vec3 lighting =
            mix(
                uEmissiveFactor,
                vec3(1.0),
                projectedLight);
        return vec4(lighting * surface, alpha);
    }
    vec4 evaluateLayeredGroundCoverSurface() {
        float scrollU = uMaterialRect0.y;
        float scrollV = uMaterialRect0.z;
        vec2 maskUv =
            vec2(vUv.x + scrollU, 1.0 - (vUv.y + scrollV));
        vec2 uv1Primary =
            vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
        vec2 uv1Secondary =
            vec2(vSourceUv1.x + 1.0, 0.5 - vSourceUv1.y);
        float lightLine =
            clamp(texture(uNormalTexture, maskUv, 0.0).r, 0.0, 1.0);
        vec4 alpha01Primary =
            texture(uTexture, uv1Primary, 0.0);
        vec4 alpha01Secondary =
            texture(uTexture, uv1Secondary, 0.0);
        vec4 base = mix(
            alpha01Primary,
            alpha01Secondary,
            lightLine);
        float alpha =
            base.a * vColor.a *
            clamp(uMaterialFlags, 0.0, 1.0);
        if (alpha <= clamp(uAlphaCutoff, 0.0, 1.0)) discard;
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 blendUv =
            vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
        float greenBlend =
            clamp(texture(uEmissiveTexture, blendUv, 0.0).r, 0.0, 1.0);
        vec3 textureMap01 =
            texture(uMetallicRoughnessTexture, uv0, 0.0).rgb;
        vec3 textureMap02 =
            texture(uOcclusionTexture, uv0, 0.0).rgb;
        vec3 decoration =
            mix(textureMap02, textureMap01, greenBlend);
        float projectedLight =
            evaluateFieldProjectedLighting(1.0);
        vec3 onGameColor =
            vec3(
                uMaterialAtlasSize.x,
                uMaterialAtlasSize.y,
                uMaterialRect0.x);
        vec3 surface =
            (base.rgb + decoration) * vColor.rgb *
            mix(
                vec3(1.0),
                onGameColor,
                clamp(uMaterialTimeSec, 0.0, 1.0));
        vec3 lighting =
            mix(uEmissiveFactor, vec3(1.0), projectedLight);
        return vec4(lighting * surface, alpha);
    }


    vec4 evaluateRoadstoneOverlaySurface() {
        float sourceMipBias = uMaterialFlipbook0.w;
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec4 texture01 = texture(uTexture, uv0, sourceMipBias);
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float toon = clamp(
            texture(
                uOcclusionTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                sourceMipBias).r,
            0.0,
            1.0);
        vec3 onGameColor =
            vec3(
                uMaterialTimeSec,
                uMaterialFlags,
                uMaterialAtlasSize.x);
        float alpha =
            texture01.a * vColor.a *
            clamp(uMaterialRect0.y, 0.0, 1.0) *
            clamp(uMaterialRect0.x, 0.0, 1.0);
        vec3 surface =
            texture01.rgb * vColor.rgb *
            mix(
                vec3(1.0),
                onGameColor,
                clamp(uMaterialAtlasSize.y, 0.0, 1.0));
        vec3 lighting = mix(
            uEmissiveFactor,
            vec3(1.0),
            evaluateFieldProjectedLighting(toon));
        return vec4(lighting * surface * alpha, alpha);
    }
    vec4 evaluateRockMaskOverlaySurface() {
        float sourceMipBias = uMaterialFlipbook0.w;
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
        vec2 blendUv =
            vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
        vec3 textureMap01 =
            texture(uTexture, uv0, sourceMipBias).rgb;
        vec3 textureMap02 =
            texture(uNormalTexture, uv0, sourceMipBias).rgb;
        vec4 greenHikari =
            texture(
                uMetallicRoughnessTexture,
                uv1,
                sourceMipBias);
        float greenBlend = clamp(
            texture(
                uOcclusionTexture,
                blendUv,
                sourceMipBias).r,
            0.0,
            1.0);
        float highlight = clamp(
            texture(
                uEmissiveTexture,
                uv1,
                sourceMipBias).r,
            0.0,
            1.0);
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float toon = clamp(
            texture(
                uEnvTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                sourceMipBias).r,
            0.0,
            1.0);
        vec3 sourceColor =
            vec3(uNormalScale, uMetallicFactor, uRoughnessFactor);
        vec3 decoration =
            mix(textureMap02, textureMap01, greenBlend) +
            sourceColor * (1.0 - highlight);
        vec3 onGameColor =
            vec3(
                uMaterialTimeSec,
                uMaterialFlags,
                uMaterialAtlasSize.x);
        float alpha =
            greenHikari.a *
            clamp(uMaterialRect0.x, 0.0, 1.0);
        vec3 surface =
            decoration * greenHikari.rgb * vColor.rgb *
            mix(
                vec3(1.0),
                onGameColor,
                clamp(uMaterialAtlasSize.y, 0.0, 1.0));
        vec3 lighting = mix(
            uEmissiveFactor,
            vec3(1.0),
            evaluateFieldProjectedLighting(toon));
        return vec4(lighting * surface * alpha, alpha);
    }


    float evaluateFieldRockLightToon(float toonCoordinate) {
        const float sourceValues[54] = float[54](
            1.0, 3.0, 5.0, 7.0, 10.0, 13.0, 16.0, 19.0,
            22.0, 26.0, 30.0, 34.0, 38.0, 43.0, 47.0, 53.0,
            58.0, 63.0, 68.0, 73.0, 80.0, 85.0, 91.0, 97.0,
            103.0, 110.0, 116.0, 122.0, 129.0, 135.0, 142.0,
            148.0, 155.0, 161.0, 168.0, 174.0, 181.0, 188.0,
            193.0, 200.0, 207.0, 211.0, 218.0, 223.0, 227.0,
            232.0, 236.0, 239.0, 243.0, 247.0, 249.0, 253.0,
            255.0, 255.0);
        float sourceTexel =
            clamp(toonCoordinate, 0.0, 1.0) * 512.0 - 0.5;
        int lower = int(floor(sourceTexel));
        int upper = lower + 1;
        float lowerValue = lower < 458
            ? 0.0
            : (lower >= 512
                ? 255.0
                : sourceValues[lower - 458]);
        float upperValue = upper < 458
            ? 0.0
            : (upper >= 512
                ? 255.0
                : sourceValues[upper - 458]);
        return mix(
                   lowerValue,
                   upperValue,
                   fract(sourceTexel)) /
               255.0;
    }
    float lgpeFlowerCoverage(float alpha) {
        float coverageT =
            clamp((alpha - 0.55) / (0.85 - 0.55), 0.0, 1.0);
        return coverageT * coverageT * (3.0 - 2.0 * coverageT);
    }
    float lgpeFlowerDitherThreshold(vec2 fragmentPosition) {
        ivec2 pixel = ivec2(mod(floor(fragmentPosition), 4.0));
        int index = pixel.x + pixel.y * 4;
        const float bayer[16] = float[16](
             0.5,  8.5,  2.5, 10.5,
            12.5,  4.5, 14.5,  6.5,
             3.5, 11.5,  1.5,  9.5,
            15.5,  7.5, 13.5,  5.5);
        return bayer[index] / 16.0;
    }
    vec3 lgpeFlowerFieldHighlight(vec3 sourceColor) {
        float maximum = max(max(sourceColor.r, sourceColor.g), sourceColor.b);
        float minimum = min(min(sourceColor.r, sourceColor.g), sourceColor.b);
        float chroma = maximum - minimum;
        float sourceSaturation =
            maximum > 0.000001 ? chroma / maximum : 0.0;
        float saturation = clamp(sourceSaturation * 1.14, 0.0, 1.0);
        float value = maximum * 1.12;
        float adjustedMinimum = value * (1.0 - saturation);
        float adjustedChroma = value * saturation;
        vec3 hueComponent =
            chroma > 0.000001
                ? (sourceColor - vec3(minimum)) / chroma
                : vec3(0.0);
        return vec3(adjustedMinimum) + hueComponent * adjustedChroma;
    }
    vec4 evaluateFieldFlowerSurface() {
        float sourceMipBias = uMaterialFlipbook0.w;
        bool buildmodelReview =
            uMaterialMode > 19.5 && uMaterialMode < 20.5;
        // Canonical BuildModel texture rows remain in source top-down
        // order when uploaded. Their raw source UV0 therefore samples
        // directly here; the road-scene texture retains the older
        // bottom-origin upload contract.
        vec2 uv0 =
            buildmodelReview
                ? vUv
                : vec2(vUv.x, 1.0 - vUv.y);
        vec4 texture01 = texture(uTexture, uv0, sourceMipBias);
        vec4 authoredVertexColor = vColor * uVertexColorMul;
        float sourceAlpha =
            texture01.a * authoredVertexColor.a *
            clamp(uMaterialRect0.y, 0.0, 1.0) *
            clamp(uMaterialRect0.x, 0.0, 1.0);
        float alpha = sourceAlpha;
        if (buildmodelReview) {
            alpha = lgpeFlowerCoverage(sourceAlpha);
            if (alpha <= lgpeFlowerDitherThreshold(gl_FragCoord.xy)) {
                discard;
            }
        } else if (alpha <= clamp(uAlphaCutoff, 0.0, 1.0)) {
            discard;
        }

        if (buildmodelReview) {
            float projectedLight =
                evaluateFieldProjectedLighting(1.0);
            vec3 normal = normalize(vWorldNormal);
            const vec3 sourceSunRay =
                vec3(0.5533391237, 0.2078260481, -0.8066127300);
            float normalDotLight = dot(normal, -sourceSunRay);
            float toonCoordinate = normalDotLight * 0.5 + 0.5;
            float toon = clamp(
                texture(
                    uOcclusionTexture,
                    vec2(toonCoordinate, 1.0 - toonCoordinate),
                    sourceMipBias).r,
                0.0,
                1.0);
            vec3 projectedLighting = mix(
                uEmissiveFactor,
                vec3(1.0),
                projectedLight);
            vec3 projectionCompensation = mix(
                vec3(1.0),
                projectedLighting,
                0.25);
            vec3 accepted = lgpeFlowerFieldHighlight(texture01.rgb);
            vec3 exact =
                texture01.rgb * authoredVertexColor.rgb *
                projectionCompensation;
            vec3 restrained = mix(accepted, exact, 0.35);
            vec3 fieldLighting = mix(
                uEmissiveFactor,
                vec3(1.0),
                evaluateFieldProjectedLighting(toon));
            return vec4(
                restrained * (fieldLighting + vec3(0.12)),
                alpha);
        }

        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float toon = clamp(
            texture(
                uOcclusionTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                sourceMipBias).r,
            0.0,
            1.0);
        vec3 onGameColor =
            vec3(
                uMaterialTimeSec,
                uMaterialFlags,
                uMaterialAtlasSize.x);
        vec3 surface =
            texture01.rgb * authoredVertexColor.rgb *
            mix(
                vec3(1.0),
                onGameColor,
                clamp(uMaterialAtlasSize.y, 0.0, 1.0));
        vec3 lighting = mix(
            uEmissiveFactor,
            vec3(1.0),
            evaluateFieldProjectedLighting(toon));
        return vec4(lighting * surface, alpha);
    }
    vec4 evaluateFieldRockSurface() {
        float sourceMipBias = uMaterialFlipbook0.w;
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 uv1 =
            vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
        vec2 uv2 =
            vec2(vSourceUv2.x, 1.0 - vSourceUv2.y);
        vec2 blendUv =
            vec2(vUv.x * 0.3, 1.0 - vUv.y * 0.3);
        vec4 rockTexture =
            texture(uTexture, uv1, sourceMipBias);
        vec3 groundTexture02 =
            texture(uNormalTexture, uv0, sourceMipBias).rgb;
        vec3 groundTexture01 =
            texture(
                uMetallicRoughnessTexture,
                uv0,
                sourceMipBias).rgb;
        float blend = clamp(
            texture(
                uOcclusionTexture,
                blendUv,
                sourceMipBias).r,
            0.0,
            1.0);
        vec4 borderTexture =
            texture(uEmissiveTexture, uv2, sourceMipBias);

        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float shadowToon = clamp(
            texture(
                uEnvTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                sourceMipBias).r,
            0.0,
            1.0);
        float lightToon =
            evaluateFieldRockLightToon(toonCoordinate);
        vec3 viewDirection =
            normalize(uCameraPos - vWorldPos);
        float rimMin = uOcclusionStrength;
        float rimMax = uMaterialTimeSec;
        float rimStrength = uMaterialFlags;
        float rimSpan = max(rimMax, rimMin) - rimMin;
        float rim = rimSpan > 0.0
            ? clamp(
                  ((1.0 - dot(normal, viewDirection)) - rimMin) /
                      rimSpan,
                  0.0,
                  1.0) *
                  rimStrength
            : 0.0;
        vec3 rock =
            rockTexture.rgb +
            uEmissiveFactor * lightToon +
            vec3(
                uNormalScale,
                uMetallicFactor,
                uRoughnessFactor) *
                rim * rockTexture.a;
        vec3 ground =
            mix(groundTexture02, groundTexture01, blend);
        vec3 surface =
            mix(rock, ground, clamp(borderTexture.a, 0.0, 1.0));
        vec3 shadowColor =
            vec3(
                uMaterialAtlasSize.x,
                uMaterialAtlasSize.y,
                uMaterialRect0.x);
        vec3 onGameColor =
            vec3(
                uMaterialRect0.y,
                uMaterialRect0.z,
                uMaterialRect0.w);
        vec3 lighting = mix(
            shadowColor,
            vec3(1.0),
            evaluateFieldProjectedLighting(shadowToon));
        vec4 authoredVertexColor = vColor * uVertexColorMul;
        return vec4(
            lighting * borderTexture.rgb *
                authoredVertexColor.rgb * surface *
                mix(
                    vec3(1.0),
                    onGameColor,
                    clamp(uMaterialRect1.x, 0.0, 1.0)),
            clamp(uMaterialRect1.y, 0.0, 1.0));
    }
    vec4 evaluatePaintedSurfaceSurface() {
        float sourceMipBias = uMaterialFlipbook0.w;
        // Canonical BNTX RGBA rows are top-down. The source program's
        // 1-V convention is therefore already represented by the decode.
        vec2 uv0 = vUv;
        vec4 texture01 = texture(uTexture, uv0, sourceMipBias);
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        vec2 toonUv =
            vec2(toonCoordinate, 1.0 - toonCoordinate);
        float shadowToon = clamp(
            texture(
                uOcclusionTexture,
                toonUv,
                sourceMipBias).r,
            0.0,
            1.0);
        float lightToon =
            evaluateFieldRockLightToon(toonCoordinate);
        vec3 viewDirection =
            normalize(uCameraPos - vWorldPos);
        float rimMin = uMaterialAtlasSize.y;
        float rimMax = uMaterialRect0.x;
        float rimStrength = uMaterialRect0.y;
        float rimSpan = max(rimMax, rimMin) - rimMin;
        float rim = rimSpan > 0.0
            ? clamp(
                  ((1.0 - dot(normal, viewDirection)) - rimMin) /
                      rimSpan,
                  0.0,
                  1.0) *
                  rimStrength
            : 0.0;
        vec3 rimColor =
            vec3(
                uMaterialTimeSec,
                uMaterialFlags,
                uMaterialAtlasSize.x);
        // Source ShadowColor and OnGameColor are exactly white, so the
        // recovered AutoShadow and OnGame mixes are neutral for this
        // Route 1 sign material.
        vec3 surface =
            texture01.rgb +
            uEmissiveFactor * lightToon +
            rimColor * rim;
        vec3 shadowColor =
            vec3(
                uNormalScale,
                uMetallicFactor,
                uRoughnessFactor);
        vec3 lighting = mix(
            shadowColor,
            vec3(1.0),
            evaluateFieldProjectedLighting(shadowToon));
        vec4 authoredVertexColor = vColor * uVertexColorMul;
        return vec4(
            lighting * surface * authoredVertexColor.rgb,
            texture01.a * authoredVertexColor.a);
    }
    vec4 evaluateFieldEncounterGrassSurface() {
        float sourceMipBias = uMaterialFlipbook0.w;
        // The canonical top-down decode already accounts for the
        // Switch-to-runtime texture-origin convention.
        vec2 uv0 = vUv;
        vec4 texture01 = texture(uTexture, uv0, sourceMipBias);
        if (texture01.a <= clamp(uAlphaCutoff, 0.0, 1.0)) {
            discard;
        }
        float rimMask = clamp(
            texture(uNormalTexture, uv0, sourceMipBias).r,
            0.0,
            1.0);
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float toonCoordinate =
            dot(normal, -sourceSunRay) * 0.5 + 0.5;
        float shadowToon = clamp(
            texture(
                uOcclusionTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                sourceMipBias).r,
            0.0,
            1.0);
        vec3 viewDirection =
            normalize(uCameraPos - vWorldPos);
        float rimMin = uMaterialAtlasSize.y;
        float rimMax = uMaterialRect0.x;
        float rimStrength = uMaterialRect0.y;
        float rim = smoothstep(
                        rimMin,
                        max(rimMax, rimMin + 1e-5),
                        clamp(
                            1.0 - abs(dot(normal, viewDirection)),
                            0.0,
                            1.0)) *
                    rimStrength * rimMask;
        vec3 shadowColor =
            vec3(
                uNormalScale,
                uMetallicFactor,
                uRoughnessFactor);
        vec3 rimColor =
            vec3(
                uMaterialTimeSec,
                uMaterialFlags,
                uMaterialAtlasSize.x);
        vec4 authoredVertexColor = vColor * uVertexColorMul;
        vec3 base =
            texture01.rgb * authoredVertexColor.rgb;
        vec3 lighting = mix(
            shadowColor,
            vec3(1.0),
            evaluateFieldProjectedLighting(shadowToon));
        return vec4(
            (base + rimColor * rim) * lighting,
            texture01.a * authoredVertexColor.a);
    }
    vec4 evaluateTreeTrunkSurface() {
        vec2 uv0 = vec2(vUv.x, 1.0 - vUv.y);
        vec2 uv1 = vec2(vSourceUv1.x, 1.0 - vSourceUv1.y);
        vec4 texture01 = texture(uTexture, uv0, 0.0);
        float highlightAlpha =
            texture(uNormalTexture, uv1, 0.0).a;
        vec3 normal = normalize(vWorldNormal);
        const vec3 sourceSunRay =
            vec3(0.5533391237, 0.2078260481, -0.8066127300);
        float normalDotLight = dot(normal, -sourceSunRay);
        float toonCoordinate = normalDotLight * 0.5 + 0.5;
        float toon = clamp(
            texture(
                uOcclusionTexture,
                vec2(toonCoordinate, 1.0 - toonCoordinate),
                0.0).r,
            0.0,
            1.0);
        vec3 viewDirection = normalize(uCameraPos - vWorldPos);
        float rimMin = uMaterialTimeSec;
        float rimMax = uMaterialFlags;
        float rimStrength = uMaterialAtlasSize.x;
        float rimSpan = max(rimMax, rimMin) - rimMin;
        float rim = rimSpan > 0.0
            ? clamp(
                  (clamp(
                       1.0 - dot(normal, viewDirection),
                       0.0,
                       1.0) -
                   rimMin) /
                      rimSpan,
                  0.0,
                  1.0) *
                  rimStrength
            : 0.0;
        vec3 shadowColor =
            max(vec3(uNormalScale, uMetallicFactor, uRoughnessFactor),
                vec3(0.0));
        vec3 rimColor =
            max(
                vec3(
                    uMaterialAtlasSize.y,
                    uMaterialRect0.x,
                    uMaterialRect0.y),
                vec3(0.0));
        vec3 lighting = mix(shadowColor, vec3(1.0), toon);
        vec3 surface =
            texture01.rgb + rimColor * rim * highlightAlpha;
        vec4 authoredVertexColor = vColor * uVertexColorMul;
        return vec4(
            lighting * surface * authoredVertexColor.rgb,
            texture01.a * authoredVertexColor.a);
    }
