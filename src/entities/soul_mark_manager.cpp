#include <entities/soul_mark_manager.h>
#include <entities/soul_mark.h>
#include <effects/continuousEffect.h>
#include <fsm/battleContext.h>
#include <utils/dynamic_library.h>
#include <plugin/plugin_interface.h>

#include <dirent.h>
#include <array>
#include <cstring>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#endif

namespace {

#if defined(_WIN32) || defined(_WIN64)
bool has_dynamic_library_extension(const std::string& filename) {
    constexpr const char* kExt = ".dll";
    return filename.size() >= std::strlen(kExt)
        && filename.compare(filename.size() - std::strlen(kExt), std::strlen(kExt), kExt) == 0;
}
#elif defined(__APPLE__)
bool has_dynamic_library_extension(const std::string& filename) {
    constexpr std::array<const char*, 2> kExts = {".dylib", ".so"};
    for (const char* ext : kExts) {
        if (filename.size() >= std::strlen(ext)
            && filename.compare(filename.size() - std::strlen(ext), std::strlen(ext), ext) == 0) {
            return true;
        }
    }
    return false;
}
#else
bool has_dynamic_library_extension(const std::string& filename) {
    constexpr const char* kExt = ".so";
    return filename.size() >= std::strlen(kExt)
        && filename.compare(filename.size() - std::strlen(kExt), std::strlen(kExt), kExt) == 0;
}
#endif

} // namespace

SoulMarkManager& SoulMarkManager::getInstance(const std::string& lib_dir) {
    static SoulMarkManager instance;
    instance.ensureInitialized(lib_dir);
    return instance;
}

void SoulMarkManager::ensureInitialized(const std::string& lib_dir) {
    if (initialized_) {
        return;
    }

    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
    // Double-check after acquiring write lock
    if (initialized_) {
        return;
    }

    if (lib_dir.empty()) {
        initialized_ = true;
        return;
    }

    // Collect libraries and their registration functions first (while holding lock)
    struct LibAndFn {
        DynamicLibrary lib;
        PluginRegisterFn reg_fn;
    };
    std::vector<LibAndFn> pending_libs;

#if defined(_WIN32) || defined(_WIN64)
    WIN32_FIND_DATAA find_data;
    std::string search_path = lib_dir + "\\*";
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string filename = find_data.cFileName;
            if (filename != "." && filename != "..") {
                if (!has_dynamic_library_extension(filename)) {
                    continue;
                }
                std::string full_path = lib_dir + "\\" + filename;
                DynamicLibrary lib(full_path);
                if (lib.isLoaded()) {
                    DLSymbol symbol = lib.getSymbol(kSoulMarkPluginInitFn);
                    if (symbol) {
                        pending_libs.push_back({std::move(lib), reinterpret_cast<PluginRegisterFn>(symbol)});
                    } else {
                        loaded_libraries_.push_back(std::move(lib));
                    }
                }
            }
        } while (FindNextFileA(hFind, &find_data));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(lib_dir.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename == "." || filename == "..") {
                continue;
            }
            if (!has_dynamic_library_extension(filename)) {
                continue;
            }
            std::string full_path = lib_dir + "/" + filename;
            DynamicLibrary lib(full_path);
            if (lib.isLoaded()) {
                DLSymbol symbol = lib.getSymbol(kSoulMarkPluginInitFn);
                if (symbol) {
                    pending_libs.push_back({std::move(lib), reinterpret_cast<PluginRegisterFn>(symbol)});
                } else {
                    loaded_libraries_.push_back(std::move(lib));
                }
            }
        }
        closedir(dir);
    }
#endif

    // Release lock before calling registration callbacks to avoid deadlock
    write_lock.unlock();

    // Now call registration functions (no locks held)
    for (auto& lib_fn : pending_libs) {
        lib_fn.reg_fn(this);
        loaded_libraries_.push_back(std::move(lib_fn.lib));
    }

    // Re-acquire lock to set initialized flag
    write_lock.lock();
    initialized_ = true;
}

void SoulMarkManager::loadFromDynamicLibraries(const std::string& lib_dir) {
    // Scan directory for dynamic libraries and load them
#if defined(_WIN32) || defined(_WIN64)
    WIN32_FIND_DATAA find_data;
    std::string search_path = lib_dir + "\\*";
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string filename = find_data.cFileName;
            if (filename != "." && filename != "..") {
                if (!has_dynamic_library_extension(filename)) {
                    continue;
                }
                std::string full_path = lib_dir + "\\" + filename;
                DynamicLibrary lib(full_path);
                if (lib.isLoaded()) {
                    // Try to get the plugin registration function
                    DLSymbol symbol = lib.getSymbol(kSoulMarkPluginInitFn);
                    if (symbol) {
                        auto register_fn = reinterpret_cast<PluginRegisterFn>(symbol);
                        register_fn(this);  // Pass 'this' as the registry
                    }
                    // Keep library loaded by moving into our vector
                    loaded_libraries_.push_back(std::move(lib));
                }
            }
        } while (FindNextFileA(hFind, &find_data));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(lib_dir.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename == "." || filename == "..") {
                continue;
            }
            if (!has_dynamic_library_extension(filename)) {
                continue;
            }
            std::string full_path = lib_dir + "/" + filename;
            DynamicLibrary lib(full_path);
            if (lib.isLoaded()) {
                // Try to get the plugin registration function
                DLSymbol symbol = lib.getSymbol(kSoulMarkPluginInitFn);
                if (symbol) {
                    auto register_fn = reinterpret_cast<PluginRegisterFn>(symbol);
                    register_fn(this);  // Pass 'this' as the registry
                }
                // Keep library loaded by moving into our vector
                loaded_libraries_.push_back(std::move(lib));
            }
        }
        closedir(dir);
    }
#endif
}

EffectFn SoulMarkManager::getEffectFunc(int soulmarkId) {
    {
        std::shared_lock<std::shared_mutex> read_lock(cache_mutex_);
        auto it = effect_cache_.find(soulmarkId);
        if (it != effect_cache_.end()) {
            return it->second;
        }
    }
    // Not found, return noop
    return &SoulMarkManager::noop_effect;
}

void SoulMarkManager::registerEffect(int soulmarkId, EffectFn effect) {
    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
    effect_cache_[soulmarkId] = effect;
}

void SoulMarkManager::registerSoulMark(int soulmark_id, EffectFn effect_fn) {
    registerEffect(soulmark_id, effect_fn);
}

void SoulMarkManager::registerSkillEffect(int effect_id, EffectFn effect_fn) {
    // SoulMarkManager only handles soul marks, not skill effects
    // This is a no-op for soul mark manager
    (void)effect_id;
    (void)effect_fn;
}

void SoulMarkManager::registerSoulMarks(
    const std::vector<std::pair<int, EffectFn>>& soulmarks) {
    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
    for (const auto& [id, fn] : soulmarks) {
        effect_cache_[id] = fn;
    }
}

void SoulMarkManager::registerSkillEffects(
    const std::vector<std::pair<int, EffectFn>>& effects) {
    // SoulMarkManager only handles soul marks, not skill effects
    (void)effects;
}

size_t SoulMarkManager::getLoadedLibraryCount() const {
    return loaded_libraries_.size();
}

// SoulMark::register_soul_effect — 激活魂印效果链（此前只声明未实现，魂印函数从未执行）。
// 把魂印 effect 包成 ContinuousEffect（BATTLE_ROUND_START 每回合执行、ON_STAGE 作用域、
// source_id=魂印 id 同源去重），注册进魂印桶。每回合重注册（FSM handle_BattleRoundStart 调），
// 幂等信号类魂印（如 2260 设 force_execute_on_pp0/ignore_pp）天然正确；
// 一次性/条件激活语义留"激活谓词"任务。
void SoulMark::register_soul_effect(BattleContext* context, int owner) {
    if (!context || owner < 0 || owner > 1 || !effect) {
        return;
    }
    // 绑定参与者：args[0]=owner, args[1]=1-owner（魂印函数用 resolve_owner_from_args 读）。
    std::vector<int> merged;
    merged.push_back(owner);
    merged.push_back(1 - owner);
    if (args.owned_int_args.size() >= 2) {
        for (std::size_t i = 2; i < args.owned_int_args.size(); ++i) {
            merged.push_back(args.owned_int_args[i]);
        }
    }
    Effect wrapper(id, 0, owner, /*left_round=*/-1, EffectArgs(std::move(merged)), effect);
    auto ce = std::make_unique<ContinuousEffect>(
        wrapper, State::BATTLE_ROUND_START, owner, /*duration=*/-1, context->roundCount
    );
    ce->source_id_ = id;
    ce->scope_ = EffectScope::ON_STAGE;
    context->registerEffect(State::BATTLE_ROUND_START, owner, std::move(ce), EffectContainer::SoulMark);
}