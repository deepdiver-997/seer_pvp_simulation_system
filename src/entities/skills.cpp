#include <entities/skills.h>

#include <algorithm>

#include <effects/effect_meta.h>
#include <fsm/battleContext.h>

namespace {

SkillType map_skill_type(int category) {
    switch (category) {
        case 1: return SkillType::Physical;
        case 2: return SkillType::Special;
        default: return SkillType::Attribute;
    }
}

EffectArgs build_effect_args_for_skill(const official_data::SkillEffectRecord& record) {
    std::vector<int> args;
    args.reserve(static_cast<std::size_t>(record.arg_count) + 2);
    args.push_back(0);
    args.push_back(1);
    args.insert(args.end(), record.args.begin(), record.args.end());
    return EffectArgs(std::move(args));
}

bool monster_has_skill(const official_data::MonsterRecord& monster, int skill_id) {
    return std::any_of(
        monster.learnable_moves.begin(),
        monster.learnable_moves.end(),
        [skill_id](const official_data::LearnableMoveRecord& move) {
            return move.move_id == skill_id;
        }
    );
}

// 效果注册时点（后续数据化：从 effect_info / side_effect 表查 register_state 列）
State effect_register_state(int effect_id) {
    switch (effect_id) {
        case 6:
        case 8:
            return State::BATTLE_FIRST_ATTACK_DAMAGE;
        default:
            return State::BATTLE_FIRST_SKILL_EFFECT;
    }
}

// 基值先制效果模板：MOVE_RIGHT 时点给 owner 的先手权累加 priority。
// Args: [owner(0), target(1), priority(2)]（bind_participants 会填 owner/target）
EffectResult effect_apply_base_priority(BattleContext* ctx, const EffectArgs& args) {
    if (!ctx || !args.int_args || args.int_count < 3) {
        return EffectResult::kOk;
    }
    const int owner = args.int_args[0];
    const int priority = args.int_args[2];
    if (owner < 0 || owner > 1) {
        return EffectResult::kOk;
    }
    ctx->ws.preemptive_level[owner] += priority;
    return EffectResult::kOk;
}

/**
 * 效果所属分支（HIT / SKILL_INVALID）。
 *
 * 官方 effect 描述决定效果何时触发：
 *   - 普通效果：技能命中才触发 → HIT 分支
 *   - "技能无效时XXX" 的被动效果：技能 miss/禁用时仍要执行 → SKILL_INVALID 分支
 *
 * 目前已知的"无效时触发"效果（数据可后续移入 DB 表）：
 *   - 2006：技能无效时，免疫下1次对手的攻击，免疫成功则令对手全属性+1（索杰德尔·无念归空净）
 *   - 2501：技能无效时，重新进行伤害结算且每260特攻威力翻倍1次（薇尔诗·乐园之初诞）
 */
SkillExecResult default_branch_for_effect(int effect_id) {
    switch (effect_id) {
        case 2006:
        case 2501:
            return SkillExecResult::SKILL_INVALID;
        default:
            return SkillExecResult::HIT;
    }
}

// 穿透类效果的凭证位（697"无视伤害限制"/699"无视攻击免疫"）。
// 与 effect_meta 的 Penetration 分类双保险：meta 负责"这是不是穿透类"，
// 此表负责"穿什么"；未列出的穿透效果返回全零（降级为无凭证）。
Skills::PenetrationFlags penetration_flags_for_effect(int effect_id) {
    Skills::PenetrationFlags flags;
    switch (effect_id) {
        case 697:  // 无视伤害限制效果
            flags.ignore_damage_limit = true;
            flags.level = 1;
            break;
        case 699:  // 无视攻击免疫效果
            flags.ignore_attack_immunity = true;
            flags.level = 1;
            break;
        default:
            break;
    }
    return flags;
}

// 把"技能自带穿透（697/699）+ 活跃次数授予"合并进 ws.attack_credential[owner]。
// 攻击时现算（每次攻击重新合并）而非每回合物化——额外行动/多攻击不会复用已消费凭证，
// 回合中段新获得的授予也能即时读到。凭证在 ws 里每回合 reset 自动清，无需手动销毁。
void materialize_attack_credential(BattleContext* ctx, int owner, const Skills& skill) {
    if (!ctx || owner < 0 || owner > 1) {
        return;
    }
    BattleWorkspace::AttackCredential& cred = ctx->ws.attack_credential[owner];
    cred = BattleWorkspace::AttackCredential{};  // 用默认成员清零
    cred.ignore_attack_immunity |= skill.penetration_flags.ignore_attack_immunity;
    cred.ignore_damage_limit |= skill.penetration_flags.ignore_damage_limit;
    cred.force_execute |= skill.penetration_flags.force_execute;
    cred.level = std::max(cred.level, skill.penetration_flags.level);
    for (const auto& grant : ctx->penetration_grants[owner]) {
        if (grant.remaining <= 0) {
            continue;
        }
        cred.ignore_attack_immunity |= grant.ignore_attack_immunity;
        cred.ignore_damage_limit |= grant.ignore_damage_limit;
        cred.level = std::max(cred.level, grant.level);
    }
    // 魂印条件凭证（SET 端）：使用 PP=0 技能 + force_execute_on_pp0 → 强制执行（无为觉者 2260）。
    if (ctx->force_execute_on_pp0[owner] && skill.pp == 0) {
        cred.force_execute = true;
    }
    cred.valid = cred.ignore_attack_immunity || cred.ignore_damage_limit || cred.force_execute;
}

} // namespace

Skills::Skills(int id, const official_data::MonsterRecord& monster)
    : id(id) {
    if (id <= 0) {
        throw std::runtime_error("invalid skill id: " + std::to_string(id));
    }
    if (!monster_has_skill(monster, id)) {
        throw std::runtime_error(
            "skill " + std::to_string(id) + " does not belong to pet " +
            std::to_string(monster.id) + " (" + monster.name + ")"
        );
    }
    if (!loadSkills()) {
        throw std::runtime_error("Failed to load skill with id: " + std::to_string(id));
    }
}

bool Skills::loadSkills() {
    auto& store = official_data::OfficialDataStore::instance();
    if (!store.ready()) {
        if (!store.initialize()) {
            return false;
        }
    }

    const std::optional<official_data::SkillRecord> record = store.repository().load_skill(id);
    if (!record) {
        return false;
    }

    name = record->name;
    type = map_skill_type(record->category);
    power = record->power;
    accuracy = record->accuracy;
    must_hit = record->must_hit != 0;
    priority = record->priority;
    maxPP = record->max_pp;
    pp = record->max_pp;
    critical_strike_rate = 1.0f;
    element[0] = record->type_id;
    element[1] = 0;
    rawEffectRecords = record->effects;
    effectBranches.clear();
    selection_effects_.clear();

    for (const auto& effect_record : rawEffectRecords) {
        // 穿透类效果（697"无视伤害限制"/699"无视攻击免疫"）→ 并入本技能穿透凭证，
        // 不注册普通分支。理由：穿透在 query_usage 门判定（效果注册之前）就消费，
        // 697/699 是 args_num=0 的纯标记模板，注册成 HIT 分支既无函数可执行也时机太晚。
        const EffectMeta* meta = EffectMetaCatalog::instance().find(effect_record.effect_id);
        if (meta && meta->category == EffectCategory::Penetration) {
            const PenetrationFlags pf = penetration_flags_for_effect(effect_record.effect_id);
            penetration_flags.ignore_attack_immunity |= pf.ignore_attack_immunity;
            penetration_flags.ignore_damage_limit |= pf.ignore_damage_limit;
            penetration_flags.level = std::max(penetration_flags.level, pf.level);
            continue;
        }
        Effect effect = clone_effect(effect_record.effect_id, build_effect_args_for_skill(effect_record));
        if (!effect.logic) {
            continue;
        }
        add_effect_node(
            default_branch_for_effect(effect_record.effect_id),
            SkillEffectNode(std::move(effect), effect_register_state(effect_record.effect_id))
        );
    }

    // 基值先制也作为一条选择期效果数据放进 selection_effects_：
    // on_selected 统一遍历注册到 MOVE_RIGHT 时点（即使本回合被控导致出招失败也生效）。
    // 条件先制效果（如"对手有护盾则先制+1"）由数据/插件在此之后追加。
    Effect base_priority_effect;
    base_priority_effect.id = 0;
    base_priority_effect.logic = &effect_apply_base_priority;
    base_priority_effect.args = EffectArgs(std::vector<int>{0, 1, priority});
    selection_effects_.push_back(
        SkillEffectNode(std::move(base_priority_effect), State::BATTLE_FIRST_MOVE_RIGHT)
    );
    return true;
}

bool Skills::skill_usable(BattleContext* ctx, int owner) {
    if (is_locked) {
        return false;
    }

    if (pp == -1) {
        return true;
    }

    if (pp > 0) {
        return true;
    }

    if (pp < 0) {
        return false;
    }

    // PP=0：魂印 ignore_pp 信号（无为觉者 2260 等）→ 可选
    if (ctx && owner >= 0 && owner <= 1 && ctx->ignore_pp[owner]) {
        return true;
    }

    bool hasIgnorePPEffect = false;
    bool hasForceRespectPPEffect = false;
    for (const auto& entry : usabilityEffects) {
        if (!entry.active) {
            continue;
        }
        if (entry.type == SkillUsabilityEffectType::IgnorePP) {
            hasIgnorePPEffect = true;
        } else if (entry.type == SkillUsabilityEffectType::ForceRespectPP) {
            hasForceRespectPPEffect = true;
        }
    }

    return hasIgnorePPEffect && !hasForceRespectPPEffect;
}

SkillSelectionResult Skills::query_selectable(BattleContext* ctx, int owner) {
    if (is_locked) {
        return SkillSelectionResult::LOCKED;
    }
    if (pp == 0) {
        // PP=0：魂印 ignore_pp 信号优先；否则需 active 的 IgnorePP 效果（且无 ForceRespectPP 覆盖）
        if (ctx && owner >= 0 && owner <= 1 && ctx->ignore_pp[owner]) {
            return SkillSelectionResult::SELECTABLE;
        }
        bool hasIgnorePPEffect = false;
        bool hasForceRespectPPEffect = false;
        for (const auto& entry : usabilityEffects) {
            if (!entry.active) {
                continue;
            }
            if (entry.type == SkillUsabilityEffectType::IgnorePP) {
                hasIgnorePPEffect = true;
            } else if (entry.type == SkillUsabilityEffectType::ForceRespectPP) {
                hasForceRespectPPEffect = true;
            }
        }
        return (hasIgnorePPEffect && !hasForceRespectPPEffect)
            ? SkillSelectionResult::SELECTABLE
            : SkillSelectionResult::PP_EMPTY;
    }
    if (pp < 0) {
        return SkillSelectionResult::LOCKED;
    }
    return SkillSelectionResult::SELECTABLE;
}

void Skills::on_selected(BattleContext* ctx, int owner) {
    if (!ctx || owner < 0 || owner > 1) {
        return;
    }
    // 选择期效果统一注册到 MOVE_RIGHT 时点（含基值先制 + 条件先制）。
    // 走 registerEffect 统一处理（valid_id 绑定 + 同源去重），source_id = 技能 id。
    for (const SkillEffectNode& node : selection_effects_) {
        Effect effect = node.effect;
        if (!effect.logic) {
            continue;
        }
        // 绑定参与者：args[0]=owner, args[1]=1-owner
        if (effect.args.owned_int_args.size() >= 2) {
            effect.args.owned_int_args[0] = owner;
            effect.args.owned_int_args[1] = 1 - owner;
            effect.args.refresh_views();
        }
        // left_round==0 的一次性效果归一化为本回合有效（同 continuousEffect.cpp 规则）
        const int left_round = effect.left_round;
        const int duration = (left_round < 0) ? -1 : (left_round == 0 ? 1 : left_round);
        auto ce = std::make_unique<ContinuousEffect>(
            effect, State::BATTLE_FIRST_MOVE_RIGHT, owner, duration, ctx->roundCount
        );
        ce->source_id_ = id;  // 技能 id 作为来源，同源去重
        ctx->registerEffect(State::BATTLE_FIRST_MOVE_RIGHT, owner, std::move(ce), EffectContainer::Skill);
    }
}

SkillUsageResult Skills::query_usage(BattleContext* ctx, int owner) {
    if (!ctx || owner < 0 || owner > 1) {
        return SkillUsageResult::MISS;
    }

    // 0) 物化本次请求凭证（穿透 697/699 + 次数授予 + 强制执行），供 ①miss 与 ②门判定读。
    materialize_attack_credential(ctx, owner, *this);

    // ① 命中判定（优先级最高）：强制执行隐含必定命中 → 跳过 miss 计算。
    if (!must_hit && !ctx->ws.attack_credential[owner].force_execute) {
        const int accuracy = this->accuracy;
        const float dodge_chance = ctx->ws.dodge_rate[1 - owner];
        const int hit_chance = accuracy - static_cast<int>(dodge_chance * 100);
        if ((std::rand() % 100) >= hit_chance) {
            return SkillUsageResult::MISS;
        }
    }

    // ② 门判定（次数类拦截：封属性/封攻击）：穿透感知消费。
    // 穿透只绕"封攻击"的可穿盔（seal_attack && penetrable）：封属性不被 699 穿透；
    // 条件盔/龙威（penetrable=false）即使有凭证也照旧被挡。miss 已在 ① 提前 return。
    auto& seals = ctx->skill_seals[owner];
    for (auto it = seals.begin(); it != seals.end(); ++it) {
        const bool is_attribute = (type == SkillType::Attribute);
        const bool matches = is_attribute ? it->seal_attribute : it->seal_attack;
        if (!matches) {
            continue;
        }
        if (it->penetrable && ctx->ws.attack_credential[owner].ignore_attack_immunity) {
            continue;  // 可穿盔被穿透 → 保留次数（文档 5.2），继续看下一条
        }
        --it->remaining;
        if (it->remaining <= 0) {
            seals.erase(it);
        }
        return SkillUsageResult::SEALED;
    }

    return SkillUsageResult::OK;
}

void Skills::register_usability_effect(int effectId, SkillUsabilityEffectType type, bool active) {
    for (auto& entry : usabilityEffects) {
        if (entry.effectId == effectId) {
            entry.type = type;
            entry.active = active;
            return;
        }
    }
    usabilityEffects.push_back(SkillUsabilityEffectEntry{effectId, type, active});
}

void Skills::set_usability_effect_active(int effectId, bool active) {
    for (auto& entry : usabilityEffects) {
        if (entry.effectId == effectId) {
            entry.active = active;
            return;
        }
    }
}

void Skills::remove_usability_effect(int effectId) {
    for (auto it = usabilityEffects.begin(); it != usabilityEffects.end(); ++it) {
        if (it->effectId == effectId) {
            usabilityEffects.erase(it);
            return;
        }
    }
}

void Skills::clear_usability_effects() {
    usabilityEffects.clear();
}

Effect Skills::clone_effect(int effectId, EffectArgs args) const {
    return EffectFactory::getInstance().getEffect(effectId, std::move(args));
}

void Skills::add_effect_node(SkillExecResult result, SkillEffectNode node) {
    effectBranches[result].push_back(std::move(node));
}
