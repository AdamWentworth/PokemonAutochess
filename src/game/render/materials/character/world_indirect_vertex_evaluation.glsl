    // The controller's continuously enabled loop01 channel animates
    // UVScaleOffset3 in parallel with the selected skeletal clip.
    float materialMode = drawState.materialParams.w;
    if (materialMode > 26.5 && materialMode < 27.5 &&
        dot(localNormal, localNormal) > 1e-10) {
        bool exactSourceTrack =
            drawState.specializedTimingFlagsAtlas.y > 1.5;
        float displacementScrollHz =
            max(drawState.specializedRect0.w, 0.0);
        float displacementScroll = !exactSourceTrack && displacementScrollHz > 0.0
            ? fract(drawState.specializedTimingFlagsAtlas.x *
                    displacementScrollHz)
            : 0.0;
        vec2 displacementOffset = exactSourceTrack
            ? drawState.specializedRect1.zw
            : drawState.specializedRect1.zw + vec2(displacementScroll);
        vec2 displacementUv = vec2(
            (inUv.x - displacementOffset.x) *
                drawState.specializedRect1.x,
            1.0 - ((1.0 - inUv.y) - displacementOffset.y) *
                drawState.specializedRect1.y);
        displacementUv = fract(displacementUv);
        float displacement = sin(textureLod(
            normalTextures[nonuniformEXT(drawState.drawParams.x)],
            displacementUv,
            0.0).r);
        localPosition += normalize(localNormal) *
            clamp(inColor.r, 0.0, 1.0) *
            max(drawState.specializedRect0.x, 0.0) * displacement;
    }
