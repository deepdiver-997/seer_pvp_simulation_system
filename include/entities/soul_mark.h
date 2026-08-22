#ifndef SOUL_MARK_H
#define SOUL_MARK_H

#include <stdexcept>
#include <string>
#include <vector>

#include <effects/effect.h>
#include <entities/soul_mark_manager.h>

// 魂印 = "程序"（多时点效果序列，对等技能运行时组装的 effectBranches）。
//
// 单效果链路（旧）：effect = 一个 EffectFn，ROUND_START 注册，函数内部自行判断时机。
// 程序链路（新）：program_ = 多个 SoulMarkNodeRef{ trigger_state, effect_fn, once, early }，
//   每个节点注册到自己的时点桶（魂印容器，先于技能容器执行）。
//   两者共用底层 Effect/ContinuousEffect/桶/执行器。
class SoulMark {
public:
    SoulMark() = default;

    SoulMark(int id, std::string name, std::string description, EffectArgs args = {})
        : id(id)
        , name(std::move(name))
        , description(std::move(description))
        , args(std::move(args)) {
        SoulMarkManager& mgr = SoulMarkManager::getInstance();
        const std::vector<SoulMarkNodeRef>* program = mgr.getSoulMarkProgram(id);
        if (program && !program->empty()) {
            program_ = *program;
            has_program_ = true;
            effect = nullptr;
        } else {
            effect = mgr.getEffectFunc(id);
        }
    }

    int id = 0;
    std::string name;
    std::string description;
    // 程序链路：多时点节点（trigger_state/once/early 见 plugin_interface.h SoulMarkNodeRef）。
    // 注册时按节点 trigger_state 进魂印桶；once=回合限一次（触发后移除，下回合重注册）；
    // early=战斗开始立即执行一次（信号类，须在选择技能前就绪）。
    std::vector<SoulMarkNodeRef> program_;
    bool has_program_ = false;
    // 单效果链路（旧）：effect 函数指针（1001-1006 等未迁移的魂印）。
    EffectFn effect = nullptr;
    EffectArgs args;
    // 官方 effect_icon.kind 分类标签（现代魂印链路填充；老链路为空）。
    std::vector<int> kind_tags;
    // 归属精灵 id（现代魂印链路填充；0 = 未知）。
    int monster_id = 0;
    // 激活魂印效果：
    // - 程序链路：把每个节点按 trigger_state 注册进魂印桶；每回合（ROUND_START）重注册重断言。
    // - 单效果链路：把 effect 包成 ContinuousEffect 注册进魂印桶（BATTLE_ROUND_START）。
    void register_soul_effect(BattleContext* context, int owner);
    // 魂印激活（战斗开始 OPERATION_ENTER_EXIT_STAGE，早于首轮技能选择）：
    // 注册全部节点到对应时点桶 + 立即执行 early 节点（信号在选择前就绪）。
    // 与 register_soul_effect（每回合重断言）配合。
    void activate_soul_mark(BattleContext* context, int owner);
    void unregister_soul_effect(BattleContext* context);
};

#endif // SOUL_MARK_H
