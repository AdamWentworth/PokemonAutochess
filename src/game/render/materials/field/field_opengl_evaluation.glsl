if (uMaterialMode > 3.5 && uMaterialMode < 4.5) {
    vec3 groundLinear = evaluateFieldGroundSurface();
    FragColor = vec4(resolveWorldSceneColor(groundLinear), 1.0);
    return;
}
if (uMaterialMode > 4.5 && uMaterialMode < 5.5) {
    vec3 cliffLinear = evaluateFieldCliffSurface();
    FragColor = vec4(resolveWorldSceneColor(cliffLinear), 1.0);
    return;
}
if (uMaterialMode > 5.5 && uMaterialMode < 6.5) {
    vec4 treeSurface = evaluateCanopySurface();
    FragColor =
        vec4(resolveWorldSceneColor(treeSurface.rgb), treeSurface.a);
    return;
}
if (uMaterialMode > 6.5 && uMaterialMode < 7.5) {
    vec4 trunkSurface =
        evaluateTreeTrunkSurface();
    FragColor =
        vec4(resolveWorldSceneColor(trunkSurface.rgb), trunkSurface.a);
    return;
}
if (uMaterialMode > 7.5 && uMaterialMode < 8.5) {
    vec4 treeSurface =
        evaluateLayeredFoliageSurface(false, false);
    FragColor =
        vec4(resolveWorldSceneColor(treeSurface.rgb), treeSurface.a);
    return;
}
if (uMaterialMode > 8.5 && uMaterialMode < 9.5) {
    vec4 grassSurface =
        evaluateFieldGrassSurface(false);
    FragColor =
        vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
    return;
}
if (uMaterialMode > 9.5 && uMaterialMode < 10.5) {
    vec4 grassSurface =
        evaluateFieldGrassSurface(true);
    FragColor =
        vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
    return;
}
if (uMaterialMode > 10.5 && uMaterialMode < 11.5) {
    vec4 grassSurface =
        evaluateGroundCoverSurface();
    FragColor =
        vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
    return;
}
if (uMaterialMode > 11.5 && uMaterialMode < 12.5) {
    vec4 grassSurface =
        evaluateLayeredGroundCoverSurface();
    FragColor =
        vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
    return;
}
if (uMaterialMode > 12.5 && uMaterialMode < 13.5) {
    vec4 overlaySurface =
        evaluateRoadstoneOverlaySurface();
    FragColor =
        vec4(resolveWorldSceneColor(overlaySurface.rgb), overlaySurface.a);
    return;
}
if (uMaterialMode > 13.5 && uMaterialMode < 14.5) {
    vec4 overlaySurface =
        evaluateRockMaskOverlaySurface();
    FragColor =
        vec4(resolveWorldSceneColor(overlaySurface.rgb), overlaySurface.a);
    return;
}
if (uMaterialMode > 14.5 && uMaterialMode < 15.5) {
    vec4 flowerSurface =
        evaluateFieldFlowerSurface();
    FragColor =
        vec4(resolveWorldSceneColor(flowerSurface.rgb), flowerSurface.a);
    return;
}
if (uMaterialMode > 15.5 && uMaterialMode < 16.5) {
    vec4 rockSurface =
        evaluateFieldRockSurface();
    FragColor =
        vec4(resolveWorldSceneColor(rockSurface.rgb), rockSurface.a);
    return;
}
if (uMaterialMode > 16.5 && uMaterialMode < 17.5) {
    vec4 signSurface =
        evaluatePaintedSurfaceSurface();
    FragColor =
        vec4(resolveWorldSceneColor(signSurface.rgb), signSurface.a);
    return;
}
if (uMaterialMode > 17.5 && uMaterialMode < 18.5) {
    vec4 grassSurface =
        evaluateFieldEncounterGrassSurface();
    FragColor =
        vec4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
    return;
}
if (uMaterialMode > 18.5 && uMaterialMode < 19.5) {
    vec4 shrubSurface =
        evaluateLayeredFoliageSurface(true, false);
    FragColor =
        vec4(resolveWorldSceneColor(shrubSurface.rgb), shrubSurface.a);
    return;
}
if (uMaterialMode > 19.5 && uMaterialMode < 20.5) {
    vec4 flowerSurface =
        evaluateFieldFlowerSurface();
    FragColor =
        vec4(resolveWorldSceneColor(flowerSurface.rgb), flowerSurface.a);
    return;
}
if (uMaterialMode > 20.5 && uMaterialMode < 21.5) {
    vec4 foliageSurface =
        evaluateTunedCanopySurface(
            0.02072325, 1.08, 0.8807060431);
    FragColor =
        vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
    return;
}
if (uMaterialMode > 21.5 && uMaterialMode < 22.5) {
    vec4 foliageSurface =
        evaluateTunedCanopySurface(
            0.00049965, 1.0371891204, 0.9015603440);
    FragColor =
        vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
    return;
}
if (uMaterialMode > 22.5 && uMaterialMode < 23.5) {
    vec4 foliageSurface =
        evaluateTunedLayeredFoliageSurface(
            0.02271645, 1.0476480571, 0.82, false);
    FragColor =
        vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
    return;
}
if (uMaterialMode > 23.5 && uMaterialMode < 24.5) {
    vec4 foliageSurface =
        evaluateTunedLayeredFoliageSurface(
            0.00425595, 1.0114461323, 1.0689001800, true);
    FragColor =
        vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
    return;
}
if (uMaterialMode > 24.5 && uMaterialMode < 25.5) {
    vec4 foliageSurface =
        evaluateTunedCanopySurface(
            0.02981715, 0.9248157036, 0.9181899276);
    FragColor =
        vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
    return;
}
if (uMaterialMode > 25.5 && uMaterialMode < 26.5) {
    vec4 sourceSurface =
        evaluateLayeredFoliageSurface(true, true);
    vec4 foliageSurface = vec4(
        foliageAcceptedDisplayTransform(sourceSurface.rgb),
        sourceSurface.a);
    FragColor =
        vec4(resolveWorldSceneColor(foliageSurface.rgb), foliageSurface.a);
    return;
}
