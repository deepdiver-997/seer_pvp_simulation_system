#ifndef SKILLS_H
#define SKILLS_H

#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <db/official_data_repository.h>
#include <effects/effect.h>

// Forward declare BattleContext and State
class BattleContext;
enum class State;
enum class SkillType {
    Physical = 0,
    Special = 1,
    Attribute = 2,
};

// 技能可用性修饰类型：用于处理“可无视PP/不可无视PP”等动态效果。
enum class SkillUsabilityEffectType {
    IgnorePP,
    ForceRespectPP,
};

struct SkillUsabilityEffectEntry {
    int effectId = -1;
    SkillUsabilityEffectType type = SkillUsabilityEffectType::IgnorePP;
    bool active = true;
};

// SkillEffectNode - 技能分支中的单个注册节点
//
// effect 做什么，由 Effect 模板本身负责；
// registerState / pendingObserveState 则属于 skill 调度层，决定“什么时候把 effect 挂出去”。
struct SkillEffectNode {
    Effect effect;
    State registerState;
    bool usePendingTrigger = false;
    State pendingObserveState;
    int pendingTtlRounds = -1;
    bool pendingConsumeOnTrigger = true;

    SkillEffectNode(Effect effect_, State registerState_)
        : effect(effect_)
        , registerState(registerState_)
        , usePendingTrigger(false)
        , pendingObserveState(registerState_) {}

    SkillEffectNode(Effect effect_,
                    State registerState_,
                    State pendingObserveState_,
                    int pendingTtlRounds_ = -1,
                    bool pendingConsumeOnTrigger_ = true)
        : effect(effect_)
        , registerState(registerState_)
        , usePendingTrigger(true)
        , pendingObserveState(pendingObserveState_)
        , pendingTtlRounds(pendingTtlRounds_)
        , pendingConsumeOnTrigger(pendingConsumeOnTrigger_) {}
};

class Skills {
public:
    Skills() = delete;
    Skills(int id) : id(id) {
        if (!loadSkills()) {
            throw std::runtime_error("Failed to load skill with id: " + std::to_string(id));
        }
    }
    Skills(int id, const official_data::MonsterRecord& monster);
    ~Skills() = default;
    // 计划从官方 SQLite 中加载技能静态层：
    // 1. 通过 official_data::OfficialDataStore / OfficialDataRepository 查 moves
    // 2. 读取 side_effect + effect_info，拆出本技能的效果序列与参数
    // 3. 再按本地映射规则生成 effectBranches
    bool loadSkills();
    bool skill_usable();
    void register_usability_effect(int effectId, SkillUsabilityEffectType type, bool active = true);
    void set_usability_effect_active(int effectId, bool active);
    void remove_usability_effect(int effectId);
    void clear_usability_effects();
    Effect clone_effect(int effectId, EffectArgs args = {}) const;
    void add_effect_node(SkillExecResult result, SkillEffectNode node);

    bool is_locked = false;
    int maxPP;
    int pp;  // pp == -1 -> 技能使用无限制
    int id = -1;
    std::string name;

    // 技能分类：与官方 moves.Category 对齐后再映射到本地枚举。
    SkillType type;
    int power;
    int accuracy;
    float critical_strike_rate;
    int priority;   // 先制等级：官方 priority + 本地调整值，数值越大越先行动
    int element[2];  // 元素属性
    std::vector<official_data::SkillEffectRecord> rawEffectRecords;

    // 效果分支表：
    // 官方 moves.SideEffect / SideEffectArg 先被解释成效果调用序列，
    // 再按技能层结果拆到不同分支。
    //
    // - HIT：正常技能效果
    // - SKILL_INVALID：miss / 技能无效类补偿
    // - EFFECT_INVALID：通常不注册任何技能效果，必要时仍可预留分支
    //
    // 每个分支中的节点再额外描述“注册时点”或“是否为未来触发器”。
    // Effect 逻辑函数指针的来源可以是动态库或 EffectFactory，
    // 但真正参与技能分支的 Effect 实例由 Skills 分支节点持有生命周期。
    std::map<SkillExecResult, std::vector<SkillEffectNode>> effectBranches;

    // 技能可用性修饰列表：
    // 典型用途：魂印/印记带来的“PP=0 仍可释放”或“禁止无视PP”。
    std::vector<SkillUsabilityEffectEntry> usabilityEffects;
};

#endif // SKILLS_H
