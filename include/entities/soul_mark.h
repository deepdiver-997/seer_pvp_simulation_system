#ifndef SOUL_MARK_H
#define SOUL_MARK_H

#include <stdexcept>
#include <string>
#include <vector>

#include <effects/effect.h>
#include <entities/soul_mark_manager.h>

class SoulMark {
public:
    SoulMark() = default;

    SoulMark(int id, std::string name, std::string description, EffectArgs args = {})
        : id(id)
        , name(std::move(name))
        , description(std::move(description))
        , effect(SoulMarkManager::getInstance().getEffectFunc(id))
        , args(std::move(args)) {}

    int id = 0;
    std::string name;
    std::string description;
    EffectFn effect = nullptr;
    EffectArgs args;
    // 官方 effect_icon.kind 分类标签（现代魂印链路填充；老链路为空）。
    std::vector<int> kind_tags;
    // 归属精灵 id（现代魂印链路填充；0 = 未知）。
    int monster_id = 0;
    void register_soul_effect(BattleContext* context);
    void unregister_soul_effect(BattleContext* context);
};

#endif // SOUL_MARK_H
