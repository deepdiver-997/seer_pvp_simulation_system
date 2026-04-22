#ifndef SOUL_MARK_MANAGER_H
#define SOUL_MARK_MANAGER_H

#include <unordered_map>
#include <shared_mutex>

#include <effects/effect.h>

// using std::function<void(std::shared_ptr<BattleContext>)> = std::function<void(std::shared_ptr<BattleContext>)>;
//魂印管理器
class SoulMarkManager {
public:
    // 获取单例实例（线程安全）
    static SoulMarkManager& getInstance() {
        static SoulMarkManager instance; // C++11保证静态局部变量初始化线程安全
        return instance;
    }

    // 根据魂印ID获取效果函数指针（线程安全）
    EffectFn getEffectFunc(int soulmarkId) {
        {
            std::shared_lock<std::shared_mutex> read_lock(cache_mutex_); // 读锁
            auto it = effect_cache_.find(soulmarkId);
            if (it != effect_cache_.end()) {
                return it->second; // 缓存命中，直接返回
            }
        }
        return &SoulMarkManager::noop_effect;
    }

    void registerEffect(int soulmarkId, EffectFn effect) {
        std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
        effect_cache_[soulmarkId] = effect;
    }

private:
    SoulMarkManager() = default; // 私有构造函数
    ~SoulMarkManager() = default; // 私有析构函数

    static EffectResult noop_effect(BattleContext* context, const EffectArgs* args) {
        (void)context;
        (void)args;
        return EffectResult::kOk;
    }

    // 缓存映射表：魂印ID -> 效果函数
    std::unordered_map<int, EffectFn> effect_cache_;
    mutable std::shared_mutex cache_mutex_; // 用于保护缓存映射表的读写锁

    // 禁止拷贝
    SoulMarkManager(const SoulMarkManager&) = delete;
    SoulMarkManager& operator=(const SoulMarkManager&) = delete;

};


#endif // SOUL_MARK_MANAGER_H