if (uMaterialMode > 3.5f && uMaterialMode < 4.5f) {
  float3 groundLinear = evaluateFieldGroundSurface(i);
  return float4(resolveWorldSceneColor(groundLinear), 1.0f);
}
if (uMaterialMode > 4.5f && uMaterialMode < 5.5f) {
  float3 cliffLinear = evaluateFieldCliffSurface(i);
  return float4(resolveWorldSceneColor(cliffLinear), 1.0f);
}
if (uMaterialMode > 5.5f && uMaterialMode < 6.5f) {
  float4 treeSurface = evaluateCanopySurface(i);
  return float4(resolveWorldSceneColor(treeSurface.rgb), treeSurface.a);
}
if (uMaterialMode > 6.5f && uMaterialMode < 7.5f) {
  float4 trunkSurface =
      evaluateTreeTrunkSurface(i);
  return float4(resolveWorldSceneColor(trunkSurface.rgb), trunkSurface.a);
}
if (uMaterialMode > 7.5f && uMaterialMode < 8.5f) {
  float4 treeSurface =
      evaluateLayeredFoliageSurface(i, false, false);
  return float4(resolveWorldSceneColor(treeSurface.rgb), treeSurface.a);
}
if (uMaterialMode > 8.5f && uMaterialMode < 9.5f) {
  float4 grassSurface =
      evaluateFieldGrassSurface(i, false);
  return float4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
}
if (uMaterialMode > 9.5f && uMaterialMode < 10.5f) {
  float4 grassSurface =
      evaluateFieldGrassSurface(i, true);
  return float4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
}
if (uMaterialMode > 10.5f && uMaterialMode < 11.5f) {
  float4 grassSurface =
      evaluateGroundCoverSurface(i);
  return float4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
}
if (uMaterialMode > 11.5f && uMaterialMode < 12.5f) {
  float4 grassSurface =
      evaluateLayeredGroundCoverSurface(i);
  return float4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
}
if (uMaterialMode > 12.5f && uMaterialMode < 13.5f) {
  float4 overlaySurface =
      evaluateRoadstoneOverlaySurface(i);
  return float4(resolveWorldSceneColor(overlaySurface.rgb), overlaySurface.a);
}
if (uMaterialMode > 13.5f && uMaterialMode < 14.5f) {
  float4 overlaySurface =
      evaluateRockMaskOverlaySurface(i);
  return float4(resolveWorldSceneColor(overlaySurface.rgb), overlaySurface.a);
}
if (uMaterialMode > 14.5f && uMaterialMode < 15.5f) {
  float4 flowerSurface = evaluateFieldFlowerSurface(i);
  return float4(resolveWorldSceneColor(flowerSurface.rgb), flowerSurface.a);
}
if (uMaterialMode > 15.5f && uMaterialMode < 16.5f) {
  float4 rockSurface = evaluateFieldRockSurface(i);
  return float4(resolveWorldSceneColor(rockSurface.rgb), rockSurface.a);
}
if (uMaterialMode > 16.5f && uMaterialMode < 17.5f) {
  float4 signSurface = evaluatePaintedSurfaceSurface(i);
  return float4(resolveWorldSceneColor(signSurface.rgb), signSurface.a);
}
if (uMaterialMode > 17.5f && uMaterialMode < 18.5f) {
  float4 grassSurface =
      evaluateFieldEncounterGrassSurface(i);
  return float4(resolveWorldSceneColor(grassSurface.rgb), grassSurface.a);
}
if (uMaterialMode > 18.5f && uMaterialMode < 19.5f) {
  float4 shrubSurface =
      evaluateLayeredFoliageSurface(i, true, false);
  return float4(resolveWorldSceneColor(shrubSurface.rgb), shrubSurface.a);
}
if (uMaterialMode > 19.5f && uMaterialMode < 20.5f) {
  float4 flowerSurface = evaluateFieldFlowerSurface(i);
  return float4(resolveWorldSceneColor(flowerSurface.rgb), flowerSurface.a);
}
if (uMaterialMode > 20.5f && uMaterialMode < 21.5f) {
  float4 foliageSurface =
      evaluateTunedCanopySurface(
          i, 0.02072325f, 1.08f, 0.8807060431f);
  return float4(
      resolveWorldSceneColor(foliageSurface.rgb),
      foliageSurface.a);
}
if (uMaterialMode > 21.5f && uMaterialMode < 22.5f) {
  float4 foliageSurface =
      evaluateTunedCanopySurface(
          i, 0.00049965f, 1.0371891204f, 0.9015603440f);
  return float4(
      resolveWorldSceneColor(foliageSurface.rgb),
      foliageSurface.a);
}
if (uMaterialMode > 22.5f && uMaterialMode < 23.5f) {
  float4 foliageSurface =
      evaluateTunedLayeredFoliageSurface(
          i, 0.02271645f, 1.0476480571f, 0.82f, false);
  return float4(
      resolveWorldSceneColor(foliageSurface.rgb),
      foliageSurface.a);
}
if (uMaterialMode > 23.5f && uMaterialMode < 24.5f) {
  float4 foliageSurface =
      evaluateTunedLayeredFoliageSurface(
          i, 0.00425595f, 1.0114461323f, 1.0689001800f, true);
  return float4(
      resolveWorldSceneColor(foliageSurface.rgb),
      foliageSurface.a);
}
if (uMaterialMode > 24.5f && uMaterialMode < 25.5f) {
  float4 foliageSurface =
      evaluateTunedCanopySurface(
          i, 0.02981715f, 0.9248157036f, 0.9181899276f);
  return float4(
      resolveWorldSceneColor(foliageSurface.rgb),
      foliageSurface.a);
}
if (uMaterialMode > 25.5f && uMaterialMode < 26.5f) {
  float4 sourceSurface =
      evaluateLayeredFoliageSurface(i, true, true);
  float4 foliageSurface = float4(
      foliageAcceptedDisplayTransform(sourceSurface.rgb),
      sourceSurface.a);
  return float4(
      resolveWorldSceneColor(foliageSurface.rgb),
      foliageSurface.a);
}
