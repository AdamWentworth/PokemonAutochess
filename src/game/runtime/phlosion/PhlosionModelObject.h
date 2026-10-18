#pragma once
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "engine/models/PhlosionModelObject.h"

namespace game::runtime::phlosion {
using engine::models::cookModelObject;
using engine::models::kCookedRoot;
using engine::models::listModelObjectTextureDependencies;
using engine::models::loadModelObject;
using engine::models::ModelCookStats;
using engine::models::ModelTextureDependency;
using engine::models::objectPathForModel;
} // namespace game::runtime::phlosion
