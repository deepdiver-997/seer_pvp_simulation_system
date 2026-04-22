#include <effects/continuousEffect.h>
#include <fsm/battleContext.h>
#include <entities/elf-pet.h>
#include <entities/skills.h>
#include <cstdlib>

// ContinuousEffectFromEffect
ContinuousEffectFromEffect::ContinuousEffectFromEffect(Effect* e, State trigger, int owner, int rounds, int validId)
    : effect_(e), triggerState_(trigger), remainingRounds_(rounds), createdInId_(validId) {}

bool ContinuousEffectFromEffect::operator()(BattleContext* ctx) {
    if (isExpired() || !effect_ || !effect_->logic) return false;

    EffectArgs args = effect_->args ? *effect_->args : EffectArgs{};
    if (args.int_count >= 2) {
        args.int_args[0] = owner_;
        args.int_args[1] = 1 - owner_;
    }
    effect_->logic(ctx, &args);
    return true;
}

bool ContinuousEffectFromEffect::isExpired() const {
    if (remainingRounds_ < 0) return false;
    return remainingRounds_ <= 0;
}

void ContinuousEffectFromEffect::onRoundEnd() {
    if (remainingRounds_ > 0) {
        --remainingRounds_;
    }
}

// SkillExecutionEffect
SkillExecutionEffect::SkillExecutionEffect(int owner, int skillIndex, State trigger)
    : owner_(owner), skillIndex_(skillIndex), triggerState_(trigger), lastSkillResult_(0) {}

bool SkillExecutionEffect::operator()(BattleContext* ctx) {
    ElfPet& pet = ctx->seerRobot[owner_].elfPets[ctx->on_stage[owner_]];
    Skills& skill = pet.skills[skillIndex_];

    if (!skill.skill_usable()) {
        lastSkillResult_ = static_cast<int>(SkillExecResult::SKILL_INVALID);
        registerBranch(ctx, SkillExecResult::SKILL_INVALID, skill);
        return false;
    }

    bool hit = calculateHit(ctx, owner_, skillIndex_);

    if (!hit) {
        lastSkillResult_ = static_cast<int>(SkillExecResult::MISS);
        registerBranch(ctx, SkillExecResult::MISS, skill);
        return true;
    }

    lastSkillResult_ = static_cast<int>(SkillExecResult::HIT);
    registerBranch(ctx, SkillExecResult::HIT, skill);

    if (skill.pp > 0) skill.pp--;

    return true;
}

bool SkillExecutionEffect::calculateHit(BattleContext* ctx, int attackerId, int skillIndex) {
    ElfPet& attacker = ctx->seerRobot[attackerId].elfPets[ctx->on_stage[attackerId]];
    Skills& skill = attacker.skills[skillIndex];

    int accuracy = skill.accuracy;
    float dodgeChance = ctx->ws.dodge_rate[1 - attackerId];
    int hitChance = accuracy - static_cast<int>(dodgeChance * 100);

    return (std::rand() % 100) < hitChance;
}

void SkillExecutionEffect::registerBranch(BattleContext* ctx, SkillExecResult result, const Skills& skill) {
    auto it = skill.effectBranches.find(result);
    if (it == skill.effectBranches.end()) return;

    for (Effect* effect : it->second) {
        if (!effect) continue;
        ctx->registerEffect(
            State::BATTLE_FIRST_SKILL_EFFECT,
            owner_,
            std::make_unique<ContinuousEffectFromEffect>(
                effect,
                State::BATTLE_FIRST_SKILL_EFFECT,
                owner_,
                effect->left_round
            )
        );
    }
}

// RoundContinuousEffect
template<int EffectId, State Trigger, int InitRounds>
RoundContinuousEffect<EffectId, Trigger, InitRounds>::RoundContinuousEffect(int owner, int rounds, int validId)
    : owner_(owner), remainingRounds_(rounds), createdInId_(validId) {}

template<int EffectId, State Trigger, int InitRounds>
bool RoundContinuousEffect<EffectId, Trigger, InitRounds>::operator()(BattleContext* ctx) {
    if (isExpired()) return false;
    auto effect = EffectFactory::getInstance().getEffect(EffectId, nullptr);
    if (effect.logic) {
        effect.logic(ctx, effect.args);
        if (remainingRounds_ > 0) {
            --remainingRounds_;
        }
        return true;
    }
    return false;
}

template<int EffectId, State Trigger, int InitRounds>
bool RoundContinuousEffect<EffectId, Trigger, InitRounds>::isExpired() const {
    if (remainingRounds_ < 0) return false;
    return remainingRounds_ <= 0;
}

template<int EffectId, State Trigger, int InitRounds>
void RoundContinuousEffect<EffectId, Trigger, InitRounds>::onRoundEnd() {
    if (remainingRounds_ > 0) {
        --remainingRounds_;
    }
}

// CountContinuousEffect
template<int EffectId, State Trigger, int InitCount>
CountContinuousEffect<EffectId, Trigger, InitCount>::CountContinuousEffect(int owner, int count, bool decrementOnTrigger)
    : owner_(owner), remainingCount_(count), decrementOnTrigger_(decrementOnTrigger) {}

template<int EffectId, State Trigger, int InitCount>
bool CountContinuousEffect<EffectId, Trigger, InitCount>::operator()(BattleContext* ctx) {
    if (isExpired()) return false;
    auto effect = EffectFactory::getInstance().getEffect(EffectId, nullptr);
    if (effect.logic) {
        effect.logic(ctx, effect.args);
        if (decrementOnTrigger_) {
            --remainingCount_;
        }
        return true;
    }
    return false;
}

template<int EffectId, State Trigger, int InitCount>
bool CountContinuousEffect<EffectId, Trigger, InitCount>::check(BattleContext* ctx) const {
    (void)ctx;
    return remainingCount_ > 0;
}

template<int EffectId, State Trigger, int InitCount>
bool CountContinuousEffect<EffectId, Trigger, InitCount>::consume(BattleContext* ctx) {
    (void)ctx;
    if (remainingCount_ <= 0) return false;
    --remainingCount_;
    return true;
}

template<int EffectId, State Trigger, int InitCount>
bool CountContinuousEffect<EffectId, Trigger, InitCount>::isExpired() const {
    if (remainingCount_ < 0) return false;
    return remainingCount_ <= 0;
}

template<int EffectId, State Trigger, int InitCount>
void CountContinuousEffect<EffectId, Trigger, InitCount>::onRoundEnd() {
    if (!decrementOnTrigger_ && remainingCount_ > 0) {
        --remainingCount_;
    }
}
