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
    priority = record->priority;
    maxPP = record->max_pp;
    pp = record->max_pp;
    critical_strike_rate = 1.0f;
    element[0] = record->type_id;
    element[1] = 0;
    rawEffectRecords = record->effects;
    effectBranches.clear();

    for (const auto& effect_record : rawEffectRecords) {
        Effect effect = clone_effect(effect_record.effect_id, build_effect_args_for_skill(effect_record));
        if (!effect.logic) {
            continue;
        }
        add_effect_node(
            SkillExecResult::HIT,
            SkillEffectNode(std::move(effect), default_register_state_for_effect(effect_record.effect_id))
        );
    }

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
