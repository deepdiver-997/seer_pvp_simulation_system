#include <entities/skills.h>

#include <algorithm>

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

State default_register_state_for_effect(int effect_id) {
    switch (effect_id) {
        case 6:
        case 8:
            return State::BATTLE_FIRST_ATTACK_DAMAGE;
        default:
            return State::BATTLE_FIRST_SKILL_EFFECT;
    }
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
        Effect effect = clone_effect(effect_record.effect_id, build_effect_args_for_skill(effect_record));
        if (!effect.logic) {
            continue;
        }
        add_effect_node(
            default_branch_for_effect(effect_record.effect_id),
            SkillEffectNode(std::move(effect), default_register_state_for_effect(effect_record.effect_id))
        );
    }

    // 基值先制不走 selection_effects_：on_selected 里直接累加 priority 字段。
    // selection_effects_ 留给条件先制效果（如"对手有护盾则先制+1"），
    // 由数据/插件在技能构造后追加，on_selected 统一注册到 MOVE_RIGHT 时点。
    return true;
}

bool Skills::skill_usable() {
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
    (void)ctx;
    (void)owner;
    if (is_locked) {
        return SkillSelectionResult::LOCKED;
    }
    if (pp == 0) {
        // PP=0：只有存在 active 的 IgnorePP 效果（且无 ForceRespectPP 覆盖）才可选
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
    // 基值先制：立即累加到先手权（即使本回合被控导致出招失败也生效）
    ctx->ws.preemptive_level[owner] += priority;

    // 条件先制效果：注册到 MOVE_RIGHT 时点，由数据/插件填充
    for (const SkillEffectNode& node : selection_effects_) {
        Effect effect = node.effect;
        if (!effect.logic) {
            continue;
        }
        // left_round==0 的一次性效果归一化为本回合有效（同 continuousEffect.cpp 规则）
        const int left_round = effect.left_round;
        const int duration = (left_round < 0) ? -1 : (left_round == 0 ? 1 : left_round);
        ++ctx->active_round_effects[owner];
        auto ce = std::make_unique<ContinuousEffectFromEffect>(
            effect, State::BATTLE_FIRST_MOVE_RIGHT, owner, duration, ctx->roundCount
        );
        ce->valid_id_ = ctx->round_effect_valid_id[owner];
        ctx->skills_effects[State::BATTLE_FIRST_MOVE_RIGHT][owner].push_back(std::move(ce));
    }
}

SkillUsageResult Skills::query_usage(BattleContext* ctx, int owner) {
    if (!ctx || owner < 0 || owner > 1) {
        return SkillUsageResult::MISS;
    }

    // ① 命中判定（优先级最高）
    if (!must_hit) {
        const int accuracy = this->accuracy;
        const float dodge_chance = ctx->ws.dodge_rate[1 - owner];
        const int hit_chance = accuracy - static_cast<int>(dodge_chance * 100);
        if ((std::rand() % 100) >= hit_chance) {
            return SkillUsageResult::MISS;
        }
    }

    // ② 次数类拦截（封属性/封攻击）：匹配则消费次数
    auto& seals = ctx->skill_seals[owner];
    for (auto it = seals.begin(); it != seals.end(); ++it) {
        const bool is_attribute = (type == SkillType::Attribute);
        const bool matches = is_attribute ? it->seal_attribute : it->seal_attack;
        if (!matches) {
            continue;
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
