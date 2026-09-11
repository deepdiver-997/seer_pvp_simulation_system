#include <effects/skill_invalid_center.h>

#include <algorithm>

namespace {

bool valid_owner(int owner) {
    return owner >= 0 && owner <= 1;
}

}  // namespace

void SkillInvalidCenter::register_armor(int owner, int source_slot, const SkillArmor& armor,
                                        InvalidBinding binding) {
    if (!valid_owner(owner)) {
        return;
    }
    // 同来源覆盖：键 = (source_slot, source_effect_id, kind)。
    // **不同来源绝不合并**——即使底层效果代码相同（用户定，2026-09-11）。
    for (InvalidEntry& e : entries_[owner]) {
        if (e.source_slot == source_slot
            && e.armor.source_effect_id == armor.source_effect_id
            && e.armor.kind == armor.kind) {
            e.armor = armor;  // 覆盖刷新（不累加）
            e.binding = binding;
            return;
        }
    }
    entries_[owner].push_back(InvalidEntry{owner, source_slot, armor, binding});
}

SkillInvalidNotifyResult SkillInvalidCenter::notify(BattleContext* ctx, int owner, int attacker,
                                                     bool is_attribute_skill, int power,
                                                     bool ignore_attack_immunity) {
    if (!valid_owner(owner)) {
        return SkillInvalidNotifyResult::NONE;
    }

    // 最终结论：无效类优先于命中失效类（同一条属性技能若同时被龙威与封属·命中失效命中，
    // 按"被无效"走 SKILL_INVALID 补偿；命中失效不覆盖无效）。两类条目的次数都在本次统一消费。
    bool any_invalid = false;
    bool any_hit_invalid = false;
    std::vector<InvalidEntry>& list = entries_[owner];
    for (auto it = list.begin(); it != list.end();) {
        InvalidEntry& entry = *it;
        if (!entry.armor.responds_to(is_attribute_skill)) {
            ++it;
            continue;  // 类别不匹配（如封属遇到攻击技能）→ 不响应、不动
        }
        if (entry.armor.condition && !entry.armor.condition(ctx, attacker, owner)) {
            ++it;
            continue;  // 条件不满足 → 跳过（保留）
        }
        if (entry.armor.penetrable && ignore_attack_immunity) {
            ++it;
            continue;  // 可穿盔被穿透 → 保留次数（文档 §5.2）
        }

        // 响应：记类 + 消费次数（次数类条目同归于"全部消费"语义，两类不互相豁免）。
        if (entry.armor.is_hit_invalid()) {
            any_hit_invalid = true;
        } else {
            any_invalid = true;
        }
        if (entry.armor.remaining_counts > 0) {
            --entry.armor.remaining_counts;
            if (entry.armor.remaining_counts <= 0) {
                it = list.erase(it);
                continue;
            }
        }
        // 回合类条目：响应但不消耗（靠减扣点 / 断回合结束）
        ++it;
    }
    if (any_invalid) {
        return SkillInvalidNotifyResult::INVALID;
    }
    if (any_hit_invalid) {
        return SkillInvalidNotifyResult::HIT_INVALID;
    }
    return SkillInvalidNotifyResult::NONE;
}

void SkillInvalidCenter::clear_self_for_slot(int owner, int source_slot) {
    if (!valid_owner(owner)) {
        return;
    }
    std::vector<InvalidEntry>& list = entries_[owner];
    std::erase_if(list, [source_slot](const InvalidEntry& e) {
        if (e.binding != InvalidBinding::SELF) {
            return false;  // TEAM 绑定：切换保留
        }
        // source_slot == -1（注册方未指定锚点）视作锚在"当前在场精灵"上
        // → 该方任何换宠都清（等价于重构前"被拦截方换宠洗掉自己身上的封属性"）。
        return e.source_slot == -1 || e.source_slot == source_slot;
    });
}

void SkillInvalidCenter::clear_round_type(int owner) {
    if (!valid_owner(owner)) {
        return;
    }
    std::erase_if(entries_[owner], [](const InvalidEntry& e) {
        return e.armor.is_round_type();
    });
}

void SkillInvalidCenter::tick_rounds() {
    for (int owner = 0; owner < 2; ++owner) {
        std::vector<InvalidEntry>& list = entries_[owner];
        std::erase_if(list, [](InvalidEntry& e) {
            if (!e.armor.is_round_type()) {
                return false;
            }
            --e.armor.remaining_rounds;
            return e.armor.remaining_rounds <= 0;
        });
    }
}

void SkillInvalidCenter::clear() {
    entries_[0].clear();
    entries_[1].clear();
}

bool SkillInvalidCenter::empty(int owner) const {
    return !valid_owner(owner) || entries_[owner].empty();
}

bool SkillInvalidCenter::has_round_type(int owner) const {
    if (!valid_owner(owner)) {
        return false;
    }
    for (const InvalidEntry& e : entries_[owner]) {
        if (e.armor.is_round_type()) {
            return true;
        }
    }
    return false;
}

std::size_t SkillInvalidCenter::size(int owner) const {
    return valid_owner(owner) ? entries_[owner].size() : 0;
}

const std::vector<InvalidEntry>& SkillInvalidCenter::entries(int owner) const {
    static const std::vector<InvalidEntry> kEmpty;
    return valid_owner(owner) ? entries_[owner] : kEmpty;
}
