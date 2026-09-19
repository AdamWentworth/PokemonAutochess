if (materialMode > 3.5 && materialMode < 4.5) {
    vec3 groundLinear = evaluateFieldGroundSurface();
    writeWorldColor(vec4(encodeWorldSurfaceColor(groundLinear), 1.0));
    return;
}
if (materialMode > 4.5 && materialMode < 5.5) {
    vec3 cliffLinear = evaluateFieldCliffSurface();
    writeWorldColor(vec4(encodeWorldSurfaceColor(cliffLinear), 1.0));
    return;
}
if (materialMode > 5.5 && materialMode < 6.5) {
    vec4 treeSurface = evaluateCanopySurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(treeSurface.rgb), treeSurface.a));
    return;
}
if (materialMode > 6.5 && materialMode < 7.5) {
    vec4 trunkSurface = evaluateTreeTrunkSurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(trunkSurface.rgb), trunkSurface.a));
    return;
}
if (materialMode > 7.5 && materialMode < 8.5) {
    vec4 treeSurface =
        evaluateLayeredFoliageSurface(false, false);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(treeSurface.rgb), treeSurface.a));
    return;
}
if (materialMode > 8.5 && materialMode < 9.5) {
    vec4 grassSurface = evaluateFieldGrassSurface(false);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(grassSurface.rgb), grassSurface.a));
    return;
}
if (materialMode > 9.5 && materialMode < 10.5) {
    vec4 grassSurface = evaluateFieldGrassSurface(true);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(grassSurface.rgb), grassSurface.a));
    return;
}
if (materialMode > 10.5 && materialMode < 11.5) {
    vec4 grassSurface = evaluateGroundCoverSurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(grassSurface.rgb), grassSurface.a));
    return;
}
if (materialMode > 11.5 && materialMode < 12.5) {
    vec4 grassSurface = evaluateLayeredGroundCoverSurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(grassSurface.rgb), grassSurface.a));
    return;
}
if (materialMode > 12.5 && materialMode < 13.5) {
    vec4 overlaySurface = evaluateRoadstoneOverlaySurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(overlaySurface.rgb), overlaySurface.a));
    return;
}
if (materialMode > 13.5 && materialMode < 14.5) {
    vec4 overlaySurface = evaluateRockMaskOverlaySurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(overlaySurface.rgb), overlaySurface.a));
    return;
}
if (materialMode > 14.5 && materialMode < 15.5) {
    vec4 flowerSurface = evaluateFieldFlowerSurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(flowerSurface.rgb), flowerSurface.a));
    return;
}
if (materialMode > 15.5 && materialMode < 16.5) {
    vec4 rockSurface = evaluateFieldRockSurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(rockSurface.rgb), rockSurface.a));
    return;
}
if (materialMode > 16.5 && materialMode < 17.5) {
    vec4 signSurface = evaluatePaintedSurfaceSurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(signSurface.rgb), signSurface.a));
    return;
}
if (materialMode > 17.5 && materialMode < 18.5) {
    vec4 grassSurface =
        evaluateFieldEncounterGrassSurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(grassSurface.rgb), grassSurface.a));
    return;
}
if (materialMode > 18.5 && materialMode < 19.5) {
    vec4 shrubSurface =
        evaluateLayeredFoliageSurface(true, false);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(shrubSurface.rgb), shrubSurface.a));
    return;
}
if (materialMode > 19.5 && materialMode < 20.5) {
    vec4 flowerSurface = evaluateFieldFlowerSurface();
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(flowerSurface.rgb), flowerSurface.a));
    return;
}
if (materialMode > 20.5 && materialMode < 21.5) {
    vec4 foliageSurface =
        evaluateTunedCanopySurface(
            0.02072325, 1.08, 0.8807060431);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(foliageSurface.rgb), foliageSurface.a));
    return;
}
if (materialMode > 21.5 && materialMode < 22.5) {
    vec4 foliageSurface =
        evaluateTunedCanopySurface(
            0.00049965, 1.0371891204, 0.9015603440);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(foliageSurface.rgb), foliageSurface.a));
    return;
}
if (materialMode > 22.5 && materialMode < 23.5) {
    vec4 foliageSurface =
        evaluateTunedLayeredFoliageSurface(
            0.02271645, 1.0476480571, 0.82, false);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(foliageSurface.rgb), foliageSurface.a));
    return;
}
if (materialMode > 23.5 && materialMode < 24.5) {
    vec4 foliageSurface =
        evaluateTunedLayeredFoliageSurface(
            0.00425595, 1.0114461323, 1.0689001800, true);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(foliageSurface.rgb), foliageSurface.a));
    return;
}
if (materialMode > 24.5 && materialMode < 25.5) {
    vec4 foliageSurface =
        evaluateTunedCanopySurface(
            0.02981715, 0.9248157036, 0.9181899276);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(foliageSurface.rgb), foliageSurface.a));
    return;
}
if (materialMode > 25.5 && materialMode < 26.5) {
    vec4 sourceSurface =
        evaluateLayeredFoliageSurface(true, true);
    vec4 foliageSurface = vec4(
        foliageAcceptedDisplayTransform(sourceSurface.rgb),
        sourceSurface.a);
    writeWorldColor(
        vec4(encodeWorldSurfaceColor(foliageSurface.rgb), foliageSurface.a));
    return;
}
