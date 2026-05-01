#ifndef SOUL_MARK_MANAGER_H
#define SOUL_MARK_MANAGER_H

#include <unordered_map>
#include <shared_mutex>
#include <string>
#include <vector>
#include <memory>

#include <effects/effect.h>
#include <plugin/plugin_interface.h>
#include <utils/dynamic_library.h>

// 魂印管理器 - 从动态库加载魂印效果函数
class SoulMarkManager : public IEffectRegistry {
public:
    // 获取单例实例（线程安全）
    // 首次调用时必须传入动态库所在目录路径
    // lib_dir: 动态库目录路径，目录中应包含 .so/.dylib/.dll 文件
    static SoulMarkManager& getInstance(const std::string& lib_dir = "");

    // 根据魂印ID获取效果函数指针（线程安全，带缓存）
    EffectFn getEffectFunc(int soulmarkId);

    // IEffectRegistry interface - for plugin use
    void registerSoulMark(int soulmark_id, EffectFn effect_fn) override;
    void registerSkillEffect(int effect_id, EffectFn effect_fn) override;
    void registerSoulMarks(
        const std::vector<std::pair<int, EffectFn>>& soulmarks) override;
    void registerSkillEffects(
        const std::vector<std::pair<int, EffectFn>>& effects) override;

    // 手动注册效果（不通过动态库）
    void registerEffect(int soulmarkId, EffectFn effect);

    // 检查是否已初始化
    bool isInitialized() const { return initialized_; }

    // 获取已加载的动态库数量（调试用）
    size_t getLoadedLibraryCount() const;

private:
    SoulMarkManager() = default;
    ~SoulMarkManager() = default;

    // 从动态库加载所有魂印效果
    void loadFromDynamicLibraries(const std::string& lib_dir);

    // 初始化单例（只执行一次）
    void ensureInitialized(const std::string& lib_dir);

    static EffectResult noop_effect(BattleContext* context, const EffectArgs& args) {
        (void)context;
        (void)args;
        return EffectResult::kOk;
    }

    // 缓存映射表：魂印ID -> 效果函数
    std::unordered_map<int, EffectFn> effect_cache_;
    mutable std::shared_mutex cache_mutex_;

    // 加载的动态库（保持加载状态，防止函数指针失效）
    std::vector<DynamicLibrary> loaded_libraries_;

    // 初始化标志
    bool initialized_ = false;

    // 禁止拷贝
    SoulMarkManager(const SoulMarkManager&) = delete;
    SoulMarkManager& operator=(const SoulMarkManager&) = delete;
};

#endif // SOUL_MARK_MANAGER_H