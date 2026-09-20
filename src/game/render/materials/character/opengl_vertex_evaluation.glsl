if (uMaterialMode > 26.5 && uMaterialMode < 27.5 &&
                dot(localNormal, localNormal) > 1e-10) {
                bool exactSourceTrack = uMaterialFlags > 1.5;
                float displacementScrollHz = max(uMaterialRect0.w, 0.0);
                float displacementScroll =
                    !exactSourceTrack && displacementScrollHz > 0.0
                        ? fract(uMaterialTimeSec * displacementScrollHz)
                        : 0.0;
                vec2 displacementOffset = exactSourceTrack
                    ? uMaterialRect1.zw
                    : uMaterialRect1.zw + vec2(displacementScroll);
                vec2 displacementUv = vec2(
                    (aUv.x - displacementOffset.x) * uMaterialRect1.x,
                    1.0 - ((1.0 - aUv.y) - displacementOffset.y) * uMaterialRect1.y);
                // The animated Scarlet fire maps are periodic on the axes
                // driven by UVScaleOffset3.  The extracted sampler metadata
                // reports clamp, but clamping turns each sawtooth reset into
                // a visible hitch.  Wrap the authored coordinates explicitly
                // so the nearly identical texture borders meet at the loop.
                displacementUv = fract(displacementUv);
                float displacement = sin(
                    textureLod(uNormalTexture, displacementUv, 0.0).r);
                localPos += normalize(localNormal) *
                    clamp(aColor.r, 0.0, 1.0) *
                    max(uMaterialRect0.x, 0.0) * displacement;
            }
