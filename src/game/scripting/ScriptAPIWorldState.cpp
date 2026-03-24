#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "engine/core/EngineServices.h"
#include "game/GameWorld.h"

#include "game/config/GameDataDb.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/runtime/video/VideoPreferences.h"
#include "game/logging/FlowTrace.h"
#include "game/logging/LoggerUtil.h"

#include "LuaBindings_Internal.h"

namespace {

bool saveVideoPreferencesFromServices(const GameServices& services, std::string* outError) {
    game::video::Preferences prefs = game::video::loadPreferences();
    prefs.rendererBackend = services.requestedRendererBackend;
    prefs.vsync = services.vsyncEnabled;
    prefs.fpsCap = game::video::sanitizeFpsCap(services.fpsCap);
    prefs.graphicsQuality = game::video::sanitizeGraphicsQuality(services.graphicsQuality);
    prefs.requireDiscreteGpu = services.requireDiscreteGpu;
    prefs.preferredGpuAdapter = services.preferredGpuAdapter;
    prefs.characterInking = services.characterInkingEnabled;
    prefs.audioMasterVolume = game::video::sanitizeVolumePercent(services.audioMasterVolume);
    prefs.audioMusicVolume = game::video::sanitizeVolumePercent(services.audioMusicVolume);
    prefs.audioSfxVolume = game::video::sanitizeVolumePercent(services.audioSfxVolume);
    prefs.audioVoiceVolume = game::video::sanitizeVolumePercent(services.audioVoiceVolume);
    prefs.audioMute = services.audioMute;
    return game::video::savePreferences(prefs, game::video::defaultPreferencesPath(), outError);
}

void mirrorRuntimePreferencesToEngineServices(const GameServices& services) {
    if (!services.engineServices) return;
    services.engineServices->requestedRendererBackend = services.requestedRendererBackend;
    services.engineServices->vsyncEnabled = services.vsyncEnabled;
    services.engineServices->fpsCap = services.fpsCap;
    services.engineServices->graphicsQuality = services.graphicsQuality;
    services.engineServices->graphicsQualityGeneration = services.graphicsQualityGeneration;
    services.engineServices->requireDiscreteGpu = services.requireDiscreteGpu;
    services.engineServices->preferredGpuAdapter = services.preferredGpuAdapter;
    services.engineServices->characterInkingEnabled = services.characterInkingEnabled;
    services.engineServices->audioMasterVolume = services.audioMasterVolume;
    services.engineServices->audioMusicVolume = services.audioMusicVolume;
    services.engineServices->audioSfxVolume = services.audioSfxVolume;
    services.engineServices->audioVoiceVolume = services.audioVoiceVolume;
    services.engineServices->audioMute = services.audioMute;
}

void bumpGraphicsQualityGeneration(GameServices& services) {
    ++services.graphicsQualityGeneration;
    if (services.graphicsQualityGeneration == 0u) {
        services.graphicsQualityGeneration = 1u;
    }
}

std::string sanitizeMenuScreenToken(const std::string& token) {
    const std::string lower = toLowerCopy(token);
    if (lower == "display") return "video";
    if (lower == "video" ||
        lower == "graphics" ||
        lower == "advanced" ||
        lower == "settings" ||
        lower == "audio" ||
        lower == "controls" ||
        lower == "gameplay" ||
        lower == "accessibility" ||
        lower == "main") {
        return lower;
    }
    return "main";
}

} // namespace

int ScriptAPI::getMoney() const {
    return world_ ? world_->getMoney() : 0;
}

void ScriptAPI::addMoney(int amount) {
    if (!world_) return;
    world_->addMoney(amount);
}

bool ScriptAPI::spendMoney(int amount) {
    if (!world_) return false;
    return world_->spendMoney(amount);
}

int ScriptAPI::getItemCount(const std::string& item) const {
    if (!world_) return 0;
    return world_->getItemCount(item);
}

void ScriptAPI::addItem(const std::string& item, int amount) {
    if (!world_) return;
    world_->addItem(item, amount);
}

bool ScriptAPI::consumeItem(const std::string& item, int amount) {
    if (!world_) return false;
    return world_->consumeItem(item, amount);
}

float ScriptAPI::getPokemonCatchRate(const std::string& name) const {
    const std::string key = toLowerCopy(name);
    const PokemonStats* ps = services_.dataDb.pokemon.getStats(key);
    if (!ps) return 0.0f;
    return std::clamp(ps->catchRate, 0.0f, 1.0f);
}

std::string ScriptAPI::getGameMode() const {
    return services_.gameMode;
}

void ScriptAPI::setGameMode(const std::string& mode) {
    const std::string key = toLowerCopy(mode);
    if (key == "classic" || key == "adventure") {
        services_.gameMode = key;
    }
}

bool ScriptAPI::getHasStartedGame() const {
    return services_.hasStartedGame;
}

void ScriptAPI::setHasStartedGame(bool started) {
    services_.hasStartedGame = started;
}

bool ScriptAPI::setVideoMode(int width, int height, bool fullscreen) {
    if (!services_.applyVideoMode) return false;
    if (width <= 0 || height <= 0) {
        return services_.applyVideoMode(width, height, fullscreen);
    }
    const int safeW = std::max(640, width);
    const int safeH = std::max(360, height);
    return services_.applyVideoMode(safeW, safeH, fullscreen);
}

GameServices::VideoMode ScriptAPI::getVideoMode() const {
    if (services_.queryVideoMode) return services_.queryVideoMode();
    return {};
}

std::string ScriptAPI::getRendererBackendPreference() const {
    return services_.requestedRendererBackend;
}

bool ScriptAPI::isRendererBackendImplemented(const std::string& backend) const {
    if (!game::video::isKnownRendererBackendToken(backend)) return false;
    return game::video::isRendererBackendImplemented(
        game::video::parseRendererBackend(backend));
}

bool ScriptAPI::setRendererBackendPreference(const std::string& backend) {
    if (!game::video::isKnownRendererBackendToken(backend)) return false;
    if (!isRendererBackendImplemented(backend)) return false;

    services_.requestedRendererBackend =
        game::video::rendererBackendName(game::video::parseRendererBackend(backend));
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Video] Failed to save renderer backend preference: ") + err);
        return false;
    }
    return true;
}

bool ScriptAPI::getVSyncPreference() const {
    return services_.vsyncEnabled;
}

bool ScriptAPI::setVSyncPreference(bool enabled) {
    services_.vsyncEnabled = enabled;
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Video] Failed to save VSync preference: ") + err);
        return false;
    }
    return true;
}

int ScriptAPI::getFpsCapPreference() const {
    return game::video::sanitizeFpsCap(services_.fpsCap);
}

bool ScriptAPI::setFpsCapPreference(int fpsCap) {
    services_.fpsCap = game::video::sanitizeFpsCap(fpsCap);
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Video] Failed to save FPS cap preference: ") + err);
        return false;
    }
    return true;
}

int ScriptAPI::getGraphicsQualityPreference() const {
    return game::video::sanitizeGraphicsQuality(services_.graphicsQuality);
}

bool ScriptAPI::setGraphicsQualityPreference(int quality) {
    const int sanitizedQuality = game::video::sanitizeGraphicsQuality(quality);
    if (services_.graphicsQuality != sanitizedQuality) {
        services_.graphicsQuality = sanitizedQuality;
        bumpGraphicsQualityGeneration(services_);
    }
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Video] Failed to save graphics quality preference: ") + err);
        return false;
    }
    return true;
}

bool ScriptAPI::getRequireDiscreteGpuPreference() const {
    return services_.requireDiscreteGpu;
}

bool ScriptAPI::setRequireDiscreteGpuPreference(bool required) {
    services_.requireDiscreteGpu = required;
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Video] Failed to save discrete GPU preference: ") + err);
        return false;
    }
    return true;
}

std::string ScriptAPI::getActiveRendererBackend() const {
    return services_.activeRendererBackend;
}

std::string ScriptAPI::getActiveGpuRenderer() const {
    return services_.gpuRenderer;
}

std::vector<std::string> ScriptAPI::getGpuAdapters() const {
    return services_.availableGpuAdapters;
}

std::string ScriptAPI::getPreferredGpuAdapterPreference() const {
    return services_.preferredGpuAdapter;
}

bool ScriptAPI::setPreferredGpuAdapterPreference(const std::string& adapterName) {
    if (!adapterName.empty()) {
        const auto it = std::find(services_.availableGpuAdapters.begin(),
                                  services_.availableGpuAdapters.end(),
                                  adapterName);
        if (it == services_.availableGpuAdapters.end()) {
            return false;
        }
    }

    services_.preferredGpuAdapter = adapterName;
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Video] Failed to save preferred GPU adapter: ") + err);
        return false;
    }
    return true;
}

bool ScriptAPI::getCharacterInkingPreference() const {
    return services_.characterInkingEnabled;
}

bool ScriptAPI::setCharacterInkingPreference(bool enabled) {
    services_.characterInkingEnabled = enabled;
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Video] Failed to save character inking preference: ") + err);
        return false;
    }
    return true;
}

int ScriptAPI::getAudioMasterVolumePreference() const {
    return game::video::sanitizeVolumePercent(services_.audioMasterVolume);
}

bool ScriptAPI::setAudioMasterVolumePreference(int volumePercent) {
    services_.audioMasterVolume = game::video::sanitizeVolumePercent(volumePercent);
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Audio] Failed to save master volume preference: ") + err);
        return false;
    }
    return true;
}

int ScriptAPI::getAudioMusicVolumePreference() const {
    return game::video::sanitizeVolumePercent(services_.audioMusicVolume);
}

bool ScriptAPI::setAudioMusicVolumePreference(int volumePercent) {
    services_.audioMusicVolume = game::video::sanitizeVolumePercent(volumePercent);
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Audio] Failed to save music volume preference: ") + err);
        return false;
    }
    return true;
}

int ScriptAPI::getAudioSfxVolumePreference() const {
    return game::video::sanitizeVolumePercent(services_.audioSfxVolume);
}

bool ScriptAPI::setAudioSfxVolumePreference(int volumePercent) {
    services_.audioSfxVolume = game::video::sanitizeVolumePercent(volumePercent);
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Audio] Failed to save SFX volume preference: ") + err);
        return false;
    }
    return true;
}

int ScriptAPI::getAudioVoiceVolumePreference() const {
    return game::video::sanitizeVolumePercent(services_.audioVoiceVolume);
}

bool ScriptAPI::setAudioVoiceVolumePreference(int volumePercent) {
    services_.audioVoiceVolume = game::video::sanitizeVolumePercent(volumePercent);
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Audio] Failed to save voice volume preference: ") + err);
        return false;
    }
    return true;
}

bool ScriptAPI::getAudioMutePreference() const {
    return services_.audioMute;
}

bool ScriptAPI::setAudioMutePreference(bool enabled) {
    services_.audioMute = enabled;
    mirrorRuntimePreferencesToEngineServices(services_);

    std::string err;
    if (!saveVideoPreferencesFromServices(services_, &err)) {
        game::log::warn(&services_.log, std::string("[Audio] Failed to save mute preference: ") + err);
        return false;
    }
    return true;
}

bool ScriptAPI::isActiveGpuDiscrete() const {
    return services_.gpuDiscrete;
}

bool ScriptAPI::requestRestartToMenu(const std::string& menuScreen) {
    game::video::Preferences prefs = game::video::loadPreferences();
    prefs.rendererBackend = services_.requestedRendererBackend;
    prefs.vsync = services_.vsyncEnabled;
    prefs.fpsCap = game::video::sanitizeFpsCap(services_.fpsCap);
    prefs.graphicsQuality = game::video::sanitizeGraphicsQuality(services_.graphicsQuality);
    prefs.requireDiscreteGpu = services_.requireDiscreteGpu;
    prefs.preferredGpuAdapter = services_.preferredGpuAdapter;
    prefs.characterInking = services_.characterInkingEnabled;
    prefs.audioMasterVolume = game::video::sanitizeVolumePercent(services_.audioMasterVolume);
    prefs.audioMusicVolume = game::video::sanitizeVolumePercent(services_.audioMusicVolume);
    prefs.audioSfxVolume = game::video::sanitizeVolumePercent(services_.audioSfxVolume);
    prefs.audioVoiceVolume = game::video::sanitizeVolumePercent(services_.audioVoiceVolume);
    prefs.audioMute = services_.audioMute;
    prefs.restartOnExit = true;
    prefs.bootMenuScreen = sanitizeMenuScreenToken(menuScreen);

    std::string err;
    if (!game::video::savePreferences(prefs, game::video::defaultPreferencesPath(), &err)) {
        game::log::warn(&services_.log, std::string("[Video] Failed to queue restart: ") + err);
        return false;
    }

    if (services_.requestQuit) {
        services_.requestQuit();
    }
    return true;
}

std::string ScriptAPI::consumeBootMenuScreen() {
    if (services_.bootMenuScreen.empty()) return {};
    const std::string out = services_.bootMenuScreen;
    services_.bootMenuScreen.clear();
    return out;
}

void ScriptAPI::requestQuit() {
    if (services_.requestQuit) {
        services_.requestQuit();
    }
}

ScriptAPI::ClassicIncomeResult ScriptAPI::awardClassicRoundIncome(bool playerWon) {
    ClassicIncomeResult out{};
    if (!world_) return out;
    if (services_.gameMode != "classic") return out;

    const auto r = world_->awardClassicRoundIncome(playerWon);
    out.baseIncome = r.baseIncome;
    out.interestIncome = r.interestIncome;
    out.streakIncome = r.streakIncome;
    out.totalIncome = r.totalIncome;
    out.winStreak = r.winStreak;
    out.lossStreak = r.lossStreak;
    out.roundIndex = r.roundIndex;
    out.won = r.won;
    return out;
}

std::vector<ScriptAPI::ClassicShopCardSnapshot> ScriptAPI::getClassicShopCards() const {
    std::vector<ClassicShopCardSnapshot> out;
    if (!world_) return out;

    const auto& src = world_->getClassicShopCards();
    out.reserve(src.size());
    for (const auto& c : src) {
        ClassicShopCardSnapshot s;
        s.name = c.name;
        s.level = c.level;
        s.cost = c.cost;
        out.push_back(std::move(s));
    }
    return out;
}

void ScriptAPI::setClassicShopCards(const std::vector<ClassicShopCardSnapshot>& cards) {
    if (!world_) return;
    std::vector<GameWorld::ClassicShopCard> out;
    out.reserve(cards.size());
    for (const auto& c : cards) {
        if (c.name.empty()) continue;
        GameWorld::ClassicShopCard gc;
        gc.name = c.name;
        gc.level = c.level;
        gc.cost = c.cost;
        out.push_back(std::move(gc));
    }
    world_->setClassicShopCards(out);
}

void ScriptAPI::clearClassicShopCards() {
    if (!world_) return;
    world_->clearClassicShopCards();
}

void ScriptAPI::startNewGame(const std::string& mode) {
    game::logging::flow::noteStartNewGameQueued(mode);
    StartNewGameCommand cmd;
    cmd.mode = mode;
    enqueue(cmd);
}

