#include <effects/continuousEffect.h>
#include <fsm/battleContext.h>
#include <entities/elf-pet.h>
#include <entities/skills.h>
#include <cstdlib>

namespace {

SkillResolutionFlags resolution_flags_for(SkillExecResult result) {
    switch (result) {
        case SkillExecResult::HIT:
            return SkillResolutionFlags{true, true};
        case SkillExecResult::SKILL_INVALID:
            return SkillResolutionFlags{true, false};
        case SkillExecResult::EFFECT_INVALID:
            return SkillResolutionFlags{false, true};
        default:
            return SkillResolutionFlags{};
    }
}

int first_mover_id(const BattleContext* ctx) {
    if (!ctx) {
        return 0;
    }
    if (ctx->preemptive_right == PreemptiveRight::SEER_ROBOT_2) {
        return 1;
    }
    return 0;
}

State state_for_owner(State state, int owner, const BattleContext* ctx) {
    const bool owner_is_first = owner == first_mover_id(ctx);
    if (owner_is_first) {
        return state;
    }

    switch (state) {
        case State::BATTLE_FIRST_ON_SKILL_HIT:
            return State::BATTLE_SECOND_ON_SKILL_HIT;
        case State::BATTLE_FIRST_SKILL_EFFECT:
            return State::BATTLE_SECOND_SKILL_EFFECT;
        case State::BATTLE_FIRST_ATTACK_DAMAGE:
            return State::BATTLE_SECOND_ATTACK_DAMAGE;
        case State::BATTLE_FIRST_AFTER_ACTION:
            return State::BATTLE_SECOND_AFTER_ACTION;
        case State::BATTLE_FIRST_ACTION_END:
            return State::BATTLE_SECOND_ACTION_END;
        case State::BATTLE_FIRST_AFTER_ACTION_END:
            return State::BATTLE_SECOND_AFTER_ACTION_END;
        default:
            return state;
    }
}

void bind_participants(Effect& effect, int owner) {
    if (effect.args.owned_int_args.size() < 2) {
        return;
    }
    effect.args.owned_int_args[0] = owner;
    effect.args.owned_int_args[1] = 1 - owner;
    effect.args.refresh_views();
}

} // namespace

// ContinuousEffectFromEffect
ContinuousEffectFromEffect::ContinuousEffectFromEffect(Effect e, State trigger, int owner, int duration, int registeredRound)
    : ContinuousEffect(owner)
    , effect_(std::move(e))
    , triggerState_(trigger)
    , registered_round_(registeredRound)
    , duration_rounds_(duration) {}

bool ContinuousEffectFromEffect::operator()(BattleContext* ctx) {
    if (isExpired(ctx->roundCount) || !effect_.logic) return false;

    effect_.logic(ctx, effect_.args);
    return true;
}

bool ContinuousEffectFromEffect::isExpired(int currentRound) const {
    if (duration_rounds_ < 0) return false;  // 永久效果
    return currentRound - registered_round_ >= duration_rounds_;
}

// SkillExecutionEffect
SkillExecutionEffect::SkillExecutionEffect(int owner, int skillIndex, State trigger)
    : ContinuousEffect(owner)
    , owner_(owner)
    , skillIndex_(skillIndex)
    , triggerState_(trigger)
    , lastSkillResult_(SkillExecResult::SKILL_INVALID)
    , lastResolutionFlags_(resolution_flags_for(SkillExecResult::SKILL_INVALID)) {}

bool SkillExecutionEffect::operator()(BattleContext* ctx) {
    ElfPet& pet = ctx->seerRobot[owner_].elfPets[ctx->on_stage[owner_]];
    Skills& skill = pet.skills[skillIndex_];

    if (!skill.skill_usable()) {
        applySkillResult(SkillExecResult::SKILL_INVALID);
        registerBranch(ctx, SkillExecResult::SKILL_INVALID, skill);
        return false;
    }

    bool hit = calculateHit(ctx, owner_, skillIndex_);

    if (!hit) {
        applySkillResult(SkillExecResult::SKILL_INVALID);
        registerBranch(ctx, SkillExecResult::SKILL_INVALID, skill);
        return true;
    }

    if (isHitEffectInvalid(ctx, owner_, skillIndex_)) {
        applySkillResult(SkillExecResult::EFFECT_INVALID);
        return true;
    }

    applySkillResult(SkillExecResult::HIT);
    registerBranch(ctx, SkillExecResult::HIT, skill);

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

bool SkillExecutionEffect::isHitEffectInvalid(BattleContext* ctx, int attackerId, int skillIndex) const {
    (void)ctx;
    (void)attackerId;
    (void)skillIndex;
    // TODO:
    // 这里后续接“命中效果失效”判定：
    // - 若失效，则技能描述中的 effect 一律不注册
    // - 但攻击伤害链是否继续，由 lastResolutionFlags_.allowAttackDamagePipeline 决定
    return false;
}

void SkillExecutionEffect::applySkillResult(SkillExecResult result) {
    lastSkillResult_ = result;
    lastResolutionFlags_ = resolution_flags_for(result);
}

void SkillExecutionEffect::registerBranch(BattleContext* ctx, SkillExecResult result, const Skills& skill) {
    if (!lastResolutionFlags_.registerSkillEffects) {
        return;
    }

    auto it = skill.effectBranches.find(result);
    if (it == skill.effectBranches.end()) return;

    for (const SkillEffectNode& node : it->second) {
        Effect effect = node.effect;
        bind_participants(effect, owner_);
        if (!effect.logic) continue;

        const State register_state = state_for_owner(node.registerState, owner_, ctx);
        const State pending_observe_state = state_for_owner(node.pendingObserveState, owner_, ctx);

        if (node.usePendingTrigger) {
            ctx->registerPendingEffect(
                pending_observe_state,
                owner_,
                std::make_unique<FutureTrigger>(
                    effect.id,
                    owner_,
                    pending_observe_state,
                    nullptr,
                    [ctx, owner = owner_, registerState = register_state, effect](BattleContext*) {
                        ctx->registerEffect(
                            registerState,
                            owner,
                            std::make_unique<ContinuousEffectFromEffect>(
                                effect,
                                registerState,
                                owner,
                                effect.left_round,
                                ctx->roundCount
                            )
                        );
                    },
                    ctx->roundCount,
                    node.pendingTtlRounds,
                    node.pendingConsumeOnTrigger
                )
            );
            continue;
        }

        ctx->registerEffect(
            register_state,
            owner_,
            std::make_unique<ContinuousEffectFromEffect>(
                effect,
                register_state,
                owner_,
                    effect.left_round,
                ctx->roundCount
            )
        );
    }
}

// RoundContinuousEffect
template<int EffectId, State Trigger, int InitRounds>
RoundContinuousEffect<EffectId, Trigger, InitRounds>::RoundContinuousEffect(int owner, int duration, int registeredRound)
    : ContinuousEffect(owner), registered_round_(registeredRound), duration_rounds_(duration) {}

template<int EffectId, State Trigger, int InitRounds>
bool RoundContinuousEffect<EffectId, Trigger, InitRounds>::operator()(BattleContext* ctx) {
    if (isExpired(ctx->roundCount)) return false;
    auto effect = EffectFactory::getInstance().getEffect(EffectId, {});
    if (effect.logic) {
        effect.logic(ctx, effect.args);
        return true;
    }
    return false;
}

template<int EffectId, State Trigger, int InitRounds>
bool RoundContinuousEffect<EffectId, Trigger, InitRounds>::isExpired(int currentRound) const {
    if (duration_rounds_ < 0) return false;  // 永久效果
    return currentRound - registered_round_ >= duration_rounds_;
}

// CountContinuousEffect
template<int EffectId, State Trigger, int InitCount>
CountContinuousEffect<EffectId, Trigger, InitCount>::CountContinuousEffect(int owner, int count, bool decrementOnTrigger)
    : ContinuousEffect(owner), remainingCount_(count), decrementOnTrigger_(decrementOnTrigger) {}

template<int EffectId, State Trigger, int InitCount>
bool CountContinuousEffect<EffectId, Trigger, InitCount>::operator()(BattleContext* ctx) {
    if (isExpired(ctx->roundCount)) return false;
    auto effect = EffectFactory::getInstance().getEffect(EffectId, {});
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
bool CountContinuousEffect<EffectId, Trigger, InitCount>::isExpired(int currentRound) const {
    (void)currentRound;
    if (remainingCount_ < 0) return false;  // 永久效果
    return remainingCount_ <= 0;
}
