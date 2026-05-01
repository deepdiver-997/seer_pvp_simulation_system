#include <effects/effect.h>
#include <fsm/battleContext.h>
#include <plugin/plugin_interface.h>

#include <cstdlib>
#include <algorithm>

namespace {

// Helper: clamp level change to valid range [-6, +6]
int clamp_level_delta(int current, int delta) {
    return std::clamp(current + delta, -6, 6);
}

// Helper: roll percentage check
bool roll_percent(int percent) {
    if (percent <= 0) return false;
    if (percent >= 100) return true;
    return (std::rand() % 100) < percent;
}

/**
 * Effect 4 - Self stat modification
 * Info: "技能使用成功时，{1}%改变自身{0}等级{2}"
 * Args: [attackerId(0), targetId(1), statIndex, probability, levelChange]
 *
 * Used by: 高速移动 (20017) - self speed +2
 */
EffectResult effect_skill_4(BattleContext* context, const EffectArgs& args) {
    if (!context || !args.int_args || args.int_count < 5) {
        return EffectResult::kOk;
    }

    int attacker_id = args.int_args[0];
    if (attacker_id < 0 || attacker_id > 1) {
        return EffectResult::kOk;
    }

    int stat_index = args.int_args[2];
    int probability = args.int_args[3];
    int level_change = args.int_args[4];

    if (stat_index < 0 || stat_index >= 6) {
        return EffectResult::kOk;
    }

    if (!roll_percent(probability)) {
        return EffectResult::kOk;
    }

    ElfPet& pet = context->seerRobot[attacker_id].elfPets[context->on_stage[attacker_id]];
    pet.levels[stat_index] = clamp_level_delta(pet.levels[stat_index], level_change);

    return EffectResult::kOk;
}

/**
 * Effect 5 - Opponent stat modification
 * Info: "技能使用成功时，{1}%改变对手{0}等级{2}"
 * Args: [attackerId(0), targetId(1), statIndex, probability, levelChange]
 *
 * Used by:
 *   鸣叫 (20002) - opponent attack -1
 *   诱惑 (20015) - opponent ??? (stat 5) -1 (info says "命中降低")
 *   吹飞 (20016) - opponent speed -1
 */
EffectResult effect_skill_5(BattleContext* context, const EffectArgs& args) {
    if (!context || !args.int_args || args.int_count < 5) {
        return EffectResult::kOk;
    }

    int target_id = args.int_args[1];
    if (target_id < 0 || target_id > 1) {
        return EffectResult::kOk;
    }

    int stat_index = args.int_args[2];
    int probability = args.int_args[3];
    int level_change = args.int_args[4];

    if (stat_index < 0 || stat_index >= 6) {
        return EffectResult::kOk;
    }

    if (!roll_percent(probability)) {
        return EffectResult::kOk;
    }

    ElfPet& pet = context->seerRobot[target_id].elfPets[context->on_stage[target_id]];
    pet.levels[stat_index] = clamp_level_delta(pet.levels[stat_index], level_change);

    return EffectResult::kOk;
}

/**
 * Effect 6 - Recoil damage (疲惫)
 * Info: "对方所受伤害的1/{0}会反弹给自己"
 * Args: [attackerId(0), targetId(1), divisor]
 *
 * Used by: 突进 (10038), 猛禽 (10039)
 * Note: This effect is applied after damage is dealt, reflecting 1/divisor of damage taken back to attacker
 */
EffectResult effect_skill_6(BattleContext* context, const EffectArgs& args) {
    if (!context || !args.int_args || args.int_count < 3) {
        return EffectResult::kOk;
    }

    int attacker_id = args.int_args[0];  // The one who deals damage (but takes recoil)
    int divisor = args.int_args[2];

    if (attacker_id < 0 || attacker_id > 1 || divisor <= 0) {
        return EffectResult::kOk;
    }

    const DamageSnapshot& damage = context->resolvedDamage;
    if (damage.final <= 0) {
        return EffectResult::kOk;
    }

    int recoil = std::max(1, damage.final / divisor);
    ElfPet& attacker = context->seerRobot[attacker_id].elfPets[context->on_stage[attacker_id]];
    attacker.hp -= recoil;
    if (attacker.hp < 0) {
        attacker.hp = 0;
    }

    return EffectResult::kOk;
}

/**
 * Effect 7 - Fear (害怕)
 * Info: "对方体力高于自己时才能命中，将对方体力减到和自己相同"
 * Args: none
 *
 * Used by: 同生共死 (10036)
 * Note: This is a special move that only works when opponent HP > self HP,
 * and sets opponent HP to equal self HP (deals fixed HP-equalizing damage)
 */
EffectResult effect_skill_7(BattleContext* context, const EffectArgs& args) {
    if (!context || !args.int_args || args.int_count < 2) {
        return EffectResult::kOk;
    }

    int attacker_id = args.int_args[0];
    int target_id = args.int_args[1];

    if (attacker_id < 0 || attacker_id > 1 || target_id < 0 || target_id > 1) {
        return EffectResult::kOk;
    }

    ElfPet& attacker = context->seerRobot[attacker_id].elfPets[context->on_stage[attacker_id]];
    ElfPet& target = context->seerRobot[target_id].elfPets[context->on_stage[target_id]];

    // Can only hit if target HP > self HP
    if (target.hp <= attacker.hp) {
        return EffectResult::kOk;
    }

    // Set target HP equal to attacker HP
    target.hp = attacker.hp;

    return EffectResult::kOk;
}

/**
 * Effect 8 - Sleep (睡眠)
 * Info: "对方体力高于自己时才能命中，将对方体力减到和自己相同" (same as effect 7 description, but different effect)
 * Args: none
 *
 * Used by: 手下留情 (10057)
 * Note: Leaves opponent at 1 HP when damage would exceed opponent HP
 * Actually: Looking at effect_info id=8, it says "伤害大于对方体力时，对方会余下1体力"
 * This is a "leave 1 HP" effect
 */
EffectResult effect_skill_8(BattleContext* context, const EffectArgs& args) {
    if (!context || !args.int_args || args.int_count < 2) {
        return EffectResult::kOk;
    }

    int attacker_id = args.int_args[0];
    int target_id = args.int_args[1];

    if (attacker_id < 0 || attacker_id > 1 || target_id < 0 || target_id > 1) {
        return EffectResult::kOk;
    }

    const ElfPet& target = context->seerRobot[target_id].elfPets[context->on_stage[target_id]];

    DamageSnapshot& damage = context->resolvedDamage;
    if (damage.final >= target.hp) {
        // Keep final damage aligned with the FSM apply_resolved_damage() pipeline.
        damage.final = std::max(0, target.hp - 1);
    }

    return EffectResult::kOk;
}

// 技能效果注册函数 - 由动态库加载器调用
void register_skill_effects_impl(IEffectRegistry* registry) {
    // 波克尔 (Monster 12) 技能效果:
    //
    // 物理攻击 (category=1):
    //   10001 撞击 - 无附加效果
    //   10008 电光火石 - 无附加效果 (先制+1在技能层处理)
    //   10009 飞翼拍击 - 无附加效果
    //   10033 全力一击 - 无附加效果
    //   10036 同生共死 - Effect 7 (害怕/同生共死)
    //   10037 燕返 - 无附加效果
    //   10038 突进 - Effect 6 (疲惫/反弹)
    //   10039 猛禽 - Effect 6 (疲惫/反弹)
    //   10057 手下留情 - Effect 8 (睡眠/留1HP)
    //
    // 属性技能 (category=4):
    //   20002 鸣叫 - Effect 5 (对手攻击-1)
    //   20015 诱惑 - Effect 5 (对手命中-1, stat 5)
    //   20016 吹飞 - Effect 5 (对手速度-1)
    //   20017 高速移动 - Effect 4 (自身速度+2)

    // 注册效果函数 (按效果ID注册)
    registry->registerSkillEffect(4, &effect_skill_4);   // 自身属性变化
    registry->registerSkillEffect(5, &effect_skill_5);   // 对手属性变化
    registry->registerSkillEffect(6, &effect_skill_6);   // 反弹伤害
    registry->registerSkillEffect(7, &effect_skill_7);   // 同生共死
    registry->registerSkillEffect(8, &effect_skill_8);   // 手下留情
}

}  // namespace

// 动态库导出函数 - 技能插件注册入口
// 该函数由 EffectFactory 在加载动态库时调用
extern "C" void skill_plugin_register(void* registry) {
    register_skill_effects_impl(static_cast<IEffectRegistry*>(registry));
}