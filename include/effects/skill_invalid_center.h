#ifndef SKILL_INVALID_CENTER_H
#define SKILL_INVALID_CENTER_H

// 技能无效中心：盔 / 威 / 封属 的注册与"响应即消费"查询。
//
// 权威口径：docs/02-效果系统/技能判定流程与无效效果体系.md §5.1-5.4。
//
// 定位：**独立容器**。既不是 TimedBucket（那个是"到点执行自己"），也不走事件中心
//（事件中心是"通知 watcher"，这里是"查询并消费"，语义不同）。
// 它挂在**被无效方**身上，由使用技能的一方来查询（"你的技能被我的盔挡了"）。
//
// 三条承重语义：
//   ① 来源粒度（2026-09-11 用户定）：**同来源（同精灵同来源效果）覆盖，不同来源绝不合并**
//      ——即使底层效果代码完全相同。例：技能A 与技能B 都给"每回合吸取200体力"，
//      来源不同 → 两条独立存在，不能覆盖成一条。
//      键 = (被无效方 owner, 注册精灵槽位 source_slot, 来源效果 id)。
//   ② 全部消费（文档 §2.3/§5.2）：一次技能使用会消耗**所有**响应它的次数类条目，
//      不因某个盔挡了另一个就保留次数。回合类条目**响应但不消耗**（靠减扣点/断回合结束）。
//   ③ 响应判据：miss 也照常 notify（"一旦本次技能命中失败，会消耗所有可响应的次数类效果"）。

#include <cstddef>
#include <functional>
#include <vector>

class BattleContext;

// 一条无效记录的内容部分。
struct SkillArmor {
    enum class Kind {
        SEAL_ATTACK,     // 狮盔：只封攻击技能
        SEAL_ALL,        // 龙威：封攻击 + 属性技能
        SEAL_ATTRIBUTE,  // 封属：只封属性技能
    };

    Kind kind = Kind::SEAL_ATTACK;
    int remaining_rounds = 0;   // 回合类窗口（>0；回合减扣点统一减；响应不消耗）
    int remaining_counts = 0;   // 次数类（>0；响应即减，减到 0 注销）
    // false = 条件盔 / 龙威，不可被"无视攻击免疫"穿透（文档 §2.2 盔侧等级：可穿盔<条件盔<龙威）
    bool penetrable = true;
    int source_effect_id = -1;  // 来源效果 id（覆盖键的一部分）
    // 条件盔条件（如"威力>120"）；nullptr = 无条件。
    // 判定用技能**面板威力**，在操作阶段即可判定（文档 §5.1）。
    std::function<bool(BattleContext*, int attacker, int defender)> condition;

    // 本条是否"响应"该技能类别的使用。
    bool responds_to(bool is_attribute_skill) const {
        switch (kind) {
            case Kind::SEAL_ATTACK:    return !is_attribute_skill;
            case Kind::SEAL_ATTRIBUTE: return is_attribute_skill;
            case Kind::SEAL_ALL:       return true;
        }
        return false;
    }

    bool is_round_type() const { return remaining_rounds > 0; }
};

// 绑定方：决定换宠时是否清理（与免疫内核的 binding 同一概念）。
enum class InvalidBinding {
    SELF,   // 绑定自己，下场时清理（如南霜"免疫下1次受到攻击"）
    TEAM,   // 绑定全队，切换保留（队伍被动）
};

// 中心里的一条完整记录。
struct InvalidEntry {
    int owner = -1;            // 被无效方 (0/1)：这条挂在谁身上
    int source_slot = -1;      // 注册精灵的槽位（绑定自身时下场清理用；-1 = 未知）
    SkillArmor armor;
    InvalidBinding binding = InvalidBinding::SELF;
};

class SkillInvalidCenter {
public:
    // 注册一条无效记录。同 (owner, source_slot, source_effect_id) → **覆盖**（刷新，不累加）。
    void register_armor(int owner, int source_slot, const SkillArmor& armor,
                        InvalidBinding binding);

    // 使用技能时通知：遍历被无效方的所有条目，让"响应此技能"的条目各减一次次数。
    // 返回**是否有任一响应**（true = 本次技能无效 → 走 SKILL_INVALID 分支与补偿）。
    // - 条件不满足 → 跳过（保留）
    // - 可穿盔 && 技能带"无视攻击免疫" → 穿透跳过（保留次数）
    // - 回合类 → 响应但不消耗
    bool notify(BattleContext* ctx, int owner, int attacker, bool is_attribute_skill,
                int power, bool ignore_attack_immunity);

    // 换宠清理：清掉 owner 方由 source_slot 注册的 SELF 条目（TEAM 的保留）。
    void clear_self_for_slot(int owner, int source_slot);

    // 断回合：清掉 owner 方所有回合类条目（次数类保留）。
    void clear_round_type(int owner);

    // 回合减扣点：所有回合类条目 -1，归零则注销。
    void tick_rounds();

    void clear();
    bool empty(int owner) const;
    // 是否还有回合类条目（断回合原语的"有无可清除物"门用）。
    bool has_round_type(int owner) const;
    std::size_t size(int owner) const;
    const std::vector<InvalidEntry>& entries(int owner) const;

private:
    std::vector<InvalidEntry> entries_[2];
};

#endif  // SKILL_INVALID_CENTER_H
