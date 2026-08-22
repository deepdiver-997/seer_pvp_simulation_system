#include <effects/effect.h>
#include <fsm/battleContext.h>
#include <plugin/plugin_interface.h>

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#endif

#if !defined(_WIN32) && !defined(_WIN64)
    #include <dirent.h>
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
    constexpr std::array<const char*, 1> kExts = {".so"};
    for (const char* ext : kExts) {
        if (filename.size() >= std::strlen(ext)
            && filename.compare(filename.size() - std::strlen(ext), std::strlen(ext), ext) == 0) {
            return true;
        }
    }
    return false;
}
#endif

} // namespace

EffectFactory::EffectFactory() : initialized_(false) {
}

void EffectFactory::init() {
    // 效果现在从动态库加载，init()保持为空
    // 实际加载在 getInstance() 首次调用时通过 ensureInitialized() 完成
}

EffectFactory& EffectFactory::getInstance(const std::string& lib_dir) {
    static EffectFactory instance;
    instance.ensureInitialized(lib_dir);
    return instance;
}

void EffectFactory::ensureInitialized(const std::string& lib_dir) {
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
                    DLSymbol symbol = lib.getSymbol(kSkillPluginInitFn);
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
                DLSymbol symbol = lib.getSymbol(kSkillPluginInitFn);
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

void EffectFactory::loadFromDynamicLibraries(const std::string& lib_dir) {
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
                    DLSymbol symbol = lib.getSymbol(kSkillPluginInitFn);
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
                DLSymbol symbol = lib.getSymbol(kSkillPluginInitFn);
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

Effect EffectFactory::getEffect(int id, EffectArgs args) {
    EffectFn fn = nullptr;
    {
        std::shared_lock<std::shared_mutex> read_lock(cache_mutex_);
        auto it = effect_cache_.find(id);
        if (it != effect_cache_.end()) {
            fn = it->second;
        }
    }

    Effect effect(id, 0, 0, 0, std::move(args), fn);
    return effect;
}

EffectPtr EffectFactory::createEffect(int id, EffectArgs args) {
    Effect effect = getEffect(id, std::move(args));
    if (effect.logic == nullptr) {
        return nullptr;
    }
    return std::make_shared<Effect>(effect);
}

bool EffectFactory::no_confilict(int id1, int id2) {
    if ((id1 == 0 && id2 == 1) || (id1 == 1 && id2 == 0)) {
        return false;
    }
    return true;
}

bool EffectFactory::roll_percent(int percent) {
    if (percent <= 0) return false;
    if (percent >= 100) return true;
    return (std::rand() % 100) < percent;
}

void EffectFactory::registerEffect(int effectId, EffectFn effect) {
    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
    effect_cache_[effectId] = effect;
}

void EffectFactory::registerSoulMark(int soulmark_id, EffectFn effect_fn) {
    // EffectFactory doesn't handle soul marks
    (void)soulmark_id;
    (void)effect_fn;
}

void EffectFactory::registerSoulMarkProgram(
    int soulmark_id, const std::vector<SoulMarkNodeRef>& nodes) {
    // EffectFactory doesn't handle soul marks（魂印程序归 SoulMarkManager）
    (void)soulmark_id;
    (void)nodes;
}

void EffectFactory::registerSkillEffect(int effect_id, EffectFn effect_fn) {
    registerEffect(effect_id, effect_fn);
}

void EffectFactory::registerSoulMarks(
    const std::vector<std::pair<int, EffectFn>>& soulmarks) {
    // EffectFactory doesn't handle soul marks
    (void)soulmarks;
}

void EffectFactory::registerSkillEffects(
    const std::vector<std::pair<int, EffectFn>>& effects) {
    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
    for (const auto& [id, fn] : effects) {
        effect_cache_[id] = fn;
    }
}

size_t EffectFactory::getLoadedLibraryCount() const {
    return loaded_libraries_.size();
}