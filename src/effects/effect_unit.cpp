#include <effects/effect_unit.h>

#include <cstdlib>

#include <fsm/battleContext.h>

namespace {

// 概率 roll：chance_value（字面）优先；否则 chance_arg 指向 EffectArgs.int_args 下标。
// -1 或参数缺失视为必定执行。
bool roll_chance(const EffectArgs& args, int chance_value, int chance_arg) {
    int percent = -1;
    if (chance_value >= 0) {
        percent = chance_value;
    } else if (chance_arg >= 0 && args.int_args && chance_arg < args.int_count) {
        percent = args.int_args[chance_arg];
    }
    if (percent < 0) {
        return true;  // 无概率 → 必定
    }
    if (percent <= 0) {
        return false;
    }
    if (percent >= 100) {
        return true;
    }
    return (std::rand() % 100) < percent;
}

// 解析真实 owner：优先 args.int_args[0]（bind_participants 绑定），缺省回退 unit.actor。
int resolve_actor(const EffectArgs& args, const EffectUnit& unit) {
    if (args.int_args && args.int_count >= 1
        && (args.int_args[0] == 0 || args.int_args[0] == 1)) {
        return args.int_args[0];
    }
    return unit.actor;
}

// 目标解析（相对语义）：0=自身, 1=对手。
int resolve_target(int actor, const EffectUnit& unit) {
    return unit.target == 1 ? 1 - actor : actor;
}

// 原语细码 → 分支键归一化（首次消费原语返回值）。
BranchKey anomaly_result_to_branch(ApplyAnomalyResult result) {
    switch (result) {
        case ApplyAnomalyResult::SUCCESS:
        case ApplyAnomalyResult::REPLACED_EXISTING:
        case ApplyAnomalyResult::DURATION_EXTENDED:
            return BranchKey::Success;
        case ApplyAnomalyResult::TARGET_IMMUNE:
            return BranchKey::Immune;
        case ApplyAnomalyResult::BLOCKED_BY_EFFECT:
            return BranchKey::Blocked;
        case ApplyAnomalyResult::TARGET_DEFEATED:
            return BranchKey::TargetDefeated;
        default:
            return BranchKey::Invalid;
    }
}

BranchKey stat_change_result_to_branch(StatChangeResult result) {
    switch (result) {
        case StatChangeResult::SUCCESS:
            return BranchKey::Success;
        case StatChangeResult::AT_CAP:
            return BranchKey::AtCap;
        default:
            return BranchKey::Invalid;
    }
}

BranchKey heal_result_to_branch(HealResult result) {
    switch (result) {
        case HealResult::SUCCESS:
            return BranchKey::Success;
        default:
            return BranchKey::Invalid;
    }
}

BranchKey fixed_damage_result_to_branch(FixedDamageResult result) {
    switch (result) {
        case FixedDamageResult::SUCCESS:
            return BranchKey::Success;
        case FixedDamageResult::TARGET_DEFEATED:
            return BranchKey::TargetDefeated;
        default:
            return BranchKey::Invalid;
    }
}

// 执行主动作（owner 相对语义解析），返回归一化分支键。
BranchKey run_primitive(BattleContext* ctx, const EffectArgs& args, const EffectUnit& unit) {
    const int actor = resolve_actor(args, unit);
    const int target = resolve_target(actor, unit);
    switch (unit.primary_tag) {
        case PrimitiveTag::Anomaly:
            return anomaly_result_to_branch(
                apply_anomaly(ctx, target, unit.param0, unit.param1, actor));
        case PrimitiveTag::StatChange:
            return stat_change_result_to_branch(
                stat_change(ctx, target, unit.param0, unit.param1));
        case PrimitiveTag::Heal:
            return heal_result_to_branch(heal(ctx, target, unit.param0));
        case PrimitiveTag::FixedDamage:
            return fixed_damage_result_to_branch(fixed_damage(ctx, target, unit.param0));
    }
    return BranchKey::Invalid;
}

}  // namespace

BranchKey execute_effect_unit(BattleContext* ctx, const EffectArgs& args, const EffectUnit& unit) {
    if (!ctx) {
        return BranchKey::Invalid;
    }
    // 概率前置：未触发 → on_other 兜底（或返回 Never）
    if (!roll_chance(args, unit.chance_value, unit.chance_arg)) {
        return unit.on_other ? execute_effect_unit(ctx, args, *unit.on_other) : BranchKey::Never;
    }
    // 主动作 → 归一化细码 → 选分支
    const BranchKey key = run_primitive(ctx, args, unit);
    const EffectUnit* branch = nullptr;
    switch (key) {
        case BranchKey::Success:
            branch = unit.on_success;
            break;
        case BranchKey::Immune:
            branch = unit.on_immune;  // Type B：被免疫 → 补偿
            break;
        case BranchKey::Blocked:
            branch = unit.on_blocked;
            break;
        default:
            branch = unit.on_other;
            break;
    }
    if (branch) {
        return execute_effect_unit(ctx, args, *branch);
    }
    return key;
}
