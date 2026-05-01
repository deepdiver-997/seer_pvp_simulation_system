#include <effects/effect.h>
#include <fsm/battleContext.h>
#include <plugin/plugin_interface.h>

#include <cstdlib>

namespace {
bool is_valid_slot(int slot) {
    return slot >= 0 && slot < 4;
}

constexpr const char* kMarkNameEternalTear = "永恒之泪";
constexpr int kEternalTearSealThreshold = 4;

using DamageGuardAllowCheck = bool (*)(BattleContext*, int);

bool roll_percent(int percent) {
    if (percent <= 0) {
        return false;
    }
    if (percent >= 100) {
        return true;
    }
    return (std::rand() % 100) < percent;
}

int resolve_owner_from_args(BattleContext* ctx, const EffectArgs& args) {
    if (args.int_args && args.int_count >= 1) {
        const int owner = args.int_args[0];
        if (owner == 0 || owner == 1) {
            return owner;
        }
    }
    if (ctx && ctx->resolvedDamage.defenderId >= 0 && ctx->resolvedDamage.defenderId <= 1) {
        return ctx->resolvedDamage.defenderId;
    }
    return 0;
}

int resolve_target_from_args(BattleContext* ctx, const EffectArgs& args, int owner) {
    if (args.int_args && args.int_count >= 2) {
        const int target = args.int_args[1];
        if (target == 0 || target == 1) {
            return target;
        }
    }
    if (ctx && ctx->resolvedDamage.attackerId >= 0 && ctx->resolvedDamage.attackerId <= 1) {
        return ctx->resolvedDamage.attackerId;
    }
    return 1 - owner;
}

int resolve_pet_max_hp(const ElfPet& pet) {
    int max_hp = pet.numericalBase[NumericalPropertyIndex::HP];
    if (max_hp <= 0) {
        max_hp = pet.numericalProperties[NumericalPropertyIndex::HP];
    }
    if (max_hp <= 0) {
        max_hp = pet.hp;
    }
    return max_hp;
}

Mark* find_mark(ElfPet& pet, const std::string& name) {
    auto it = std::find_if(
        pet.marks.begin(),
        pet.marks.end(),
        [&](const Mark& mark) { return mark.name == name; }
    );
    if (it == pet.marks.end()) {
        return nullptr;
    }
    return &(*it);
}

const Mark* find_mark(const ElfPet& pet, const std::string& name) {
    auto it = std::find_if(
        pet.marks.begin(),
        pet.marks.end(),
        [&](const Mark& mark) { return mark.name == name; }
    );
    if (it == pet.marks.end()) {
        return nullptr;
    }
    return &(*it);
}

int get_mark_count(const ElfPet& pet, const std::string& name) {
    const Mark* mark = find_mark(pet, name);
    return mark ? mark->count : 0;
}

void add_mark_stacks(ElfPet& pet, const std::string& name, int stacks) {
    if (stacks <= 0) {
        return;
    }

    Mark* mark = find_mark(pet, name);
    if (!mark) {
        pet.marks.emplace_back(0, name);
        mark = &pet.marks.back();
    }
    mark->count += stacks;
}

void remove_mark(ElfPet& pet, const std::string& name) {
    std::erase_if(
        pet.marks,
        [&](const Mark& mark) { return mark.name == name; }
    );
}

bool guard_not_sealed_by_eternal_tears(BattleContext* ctx, int defender_id) {
    if (!ctx || defender_id < 0 || defender_id > 1) {
        return false;
    }
    const ElfPet& defender = ctx->seerRobot[defender_id].elfPets[ctx->on_stage[defender_id]];
    return get_mark_count(defender, kMarkNameEternalTear) < kEternalTearSealThreshold;
}

bool can_trigger_damage_guard(BattleContext* ctx, int defender_id) {
    static constexpr DamageGuardAllowCheck kAllowChecks[] = {
        &guard_not_sealed_by_eternal_tears,
    };

    for (DamageGuardAllowCheck check : kAllowChecks) {
        if (!check(ctx, defender_id)) {
            return false;
        }
    }
    return true;
}

bool try_block_current_red_damage(BattleContext* ctx, int defender_id) {
    if (!ctx || defender_id < 0 || defender_id > 1) {
        return false;
    }

    DamageSnapshot& damage = ctx->resolvedDamage;
    if (damage.defenderId != defender_id) {
        return false;
    }
    if (!damage.isRed || damage.final <= 0) {
        return false;
    }
    if (!can_trigger_damage_guard(ctx, defender_id)) {
        return false;
    }

    damage.base = 0;
    damage.afterAdd = 0;
    damage.afterMul = 0;
    damage.final = 0;
    damage.addPct = 0;
    damage.mulCoef = 0.0;
    return true;
}

void reflect_damage_to_attacker(BattleContext* ctx, int attacker_id, int amount) {
    if (!ctx || attacker_id < 0 || attacker_id > 1 || amount <= 0) {
        return;
    }

    ElfPet& attacker = ctx->seerRobot[attacker_id].elfPets[ctx->on_stage[attacker_id]];
    attacker.hp -= amount;
    if (attacker.hp < 0) {
        attacker.hp = 0;
    }
}

void heal_to_full(BattleContext* ctx, int robot_id) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return;
    }

    ElfPet& pet = ctx->seerRobot[robot_id].elfPets[ctx->on_stage[robot_id]];
    const int max_hp = resolve_pet_max_hp(pet);
    if (max_hp > 0) {
        pet.hp = max_hp;
    }
}

void clear_mark_from_all_pets(SeerRobot& robot, const std::string& name) {
    for (ElfPet& pet : robot.elfPets) {
        remove_mark(pet, name);
    }
}

// 魂印效果函数定义

EffectResult soulmark_damage_guard_sorensen(BattleContext* ctx, const EffectArgs& args) {
    const int owner = resolve_owner_from_args(ctx, args);
    const int chance = (args.int_args && args.int_count >= 3) ? args.int_args[2] : 30;

    if (!roll_percent(chance)) {
        return EffectResult::kOk;
    }

    try_block_current_red_damage(ctx, owner);
    return EffectResult::kOk;
}

EffectResult soulmark_damage_guard_lancelot_crit_reflect(BattleContext* ctx, const EffectArgs& args) {
    if (!ctx) {
        return EffectResult::kOk;
    }

    const int owner = resolve_owner_from_args(ctx, args);
    const int attacker = resolve_target_from_args(ctx, args, owner);
    DamageSnapshot& damage = ctx->resolvedDamage;

    if (damage.defenderId != owner || !damage.isRed || !damage.isCrit || damage.final <= 0) {
        return EffectResult::kOk;
    }

    const int reflected = damage.final;
    if (try_block_current_red_damage(ctx, owner)) {
        reflect_damage_to_attacker(ctx, attacker, reflected);
    }
    return EffectResult::kOk;
}

EffectResult soulmark_after_damage_sarika_full_heal(BattleContext* ctx, const EffectArgs& args) {
    if (!ctx) {
        return EffectResult::kOk;
    }

    const int owner = resolve_owner_from_args(ctx, args);
    const int threshold = (args.int_args && args.int_count >= 3) ? args.int_args[2] : 350;
    const DamageSnapshot& damage = ctx->resolvedDamage;

    if (damage.defenderId != owner || !damage.isRed || damage.final <= threshold) {
        return EffectResult::kOk;
    }

    heal_to_full(ctx, owner);
    return EffectResult::kOk;
}

EffectResult soulmark_canglan_apply_eternal_tears(BattleContext* ctx, const EffectArgs& args) {
    if (!ctx) {
        return EffectResult::kOk;
    }

    const int owner = resolve_owner_from_args(ctx, args);
    const int target = resolve_target_from_args(ctx, args, owner);
    const int stacks = (args.int_args && args.int_count >= 3) ? args.int_args[2] : 4;
    ElfPet& defender = ctx->seerRobot[target].elfPets[ctx->on_stage[target]];

    add_mark_stacks(defender, kMarkNameEternalTear, stacks);
    return EffectResult::kOk;
}

EffectResult soulmark_canglan_on_death_cleanup_tears(BattleContext* ctx, const EffectArgs& args) {
    if (!ctx) {
        return EffectResult::kOk;
    }

    const int owner = resolve_owner_from_args(ctx, args);
    const int target = 1 - owner;
    clear_mark_from_all_pets(ctx->seerRobot[target], kMarkNameEternalTear);
    return EffectResult::kOk;
}

// 魂印注册函数 - 由动态库加载器调用
void register_soul_mark_effects_impl(IEffectRegistry* registry) {
    // 这几个 id 只是当前文件里的草案示例：
    // 1001: 索伦森概率挡红伤
    // 1002: 兰斯洛特免疫并反弹暴击伤害
    // 1003: 萨瑞卡受到超过 350 红伤后回满
    // 1004: 沧岚给对手叠永恒之泪
    // 1005: 沧岚死亡时清除对手所有永恒之泪
    registry->registerSoulMark(1001, &soulmark_damage_guard_sorensen);
    registry->registerSoulMark(1002, &soulmark_damage_guard_lancelot_crit_reflect);
    registry->registerSoulMark(1003, &soulmark_after_damage_sarika_full_heal);
    registry->registerSoulMark(1004, &soulmark_canglan_apply_eternal_tears);
    registry->registerSoulMark(1005, &soulmark_canglan_on_death_cleanup_tears);
}

}  // namespace

// 动态库导出函数 - 魂印插件注册入口
// 该函数由 SoulMarkManager 在加载动态库时调用
extern "C" void soulmark_plugin_register(void* registry) {
    register_soul_mark_effects_impl(static_cast<IEffectRegistry*>(registry));
}