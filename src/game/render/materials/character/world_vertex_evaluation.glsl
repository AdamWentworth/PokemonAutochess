    // Scarlet's always-on loop01 channel scrolls UVScaleOffset3 independently
    // of the selected skeletal clip. Preserve that material animation before
    // applying source vertex red and DisplacementHeight.
    float materialMode = pushData.materialParams.w;
    if (materialMode > 26.5 && materialMode < 27.5 &&
        dot(localNormal, localNormal) > 1e-10) {
        bool exactSourceTrack =
            worldSpecializedMaterial.timingFlagsAtlas.y > 1.5;
        float displacementScrollHz =
            max(worldSpecializedMaterial.rect0.w, 0.0);
        float displacementScroll = !exactSourceTrack && displacementScrollHz > 0.0
            ? fract(worldSpecializedMaterial.timingFlagsAtlas.x *
                    displacementScrollHz)
            : 0.0;
        vec2 displacementOffset = exactSourceTrack
            ? worldSpecializedMaterial.rect1.zw
            : worldSpecializedMaterial.rect1.zw + vec2(displacementScroll);
        vec2 displacementUv = vec2(
            (inUv.x - displacementOffset.x) *
                worldSpecializedMaterial.rect1.x,
            1.0 - ((1.0 - inUv.y) - displacementOffset.y) *
                worldSpecializedMaterial.rect1.y);
        // Scarlet's displacement noise is authored as a periodic texture.
        // Explicit wrapping keeps the UVScaleOffset3 reset continuous even
        // when the extracted sampler metadata resolves to clamp-to-edge.
        displacementUv = fract(displacementUv);
        float displacement = sin(textureLod(
            normalTexture,
            displacementUv,
            0.0).r);
        localPosition += normalize(localNormal) *
            clamp(inColor.r, 0.0, 1.0) *
            max(worldSpecializedMaterial.rect0.x, 0.0) * displacement;
    }
