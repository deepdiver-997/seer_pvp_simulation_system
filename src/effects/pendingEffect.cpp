#include <effects/pendingEffect.h>

#include <utility>

#include <fsm/battleContext.h>

FutureTrigger::FutureTrigger(int effectId,
                             int owner,
                             State observeState,
                             PendingEffectCondition condition,
                             PendingEffectAction action,
                             int registeredRound,
                             int ttlRounds,
                             bool consumeOnTrigger)
    : observe_state_(observeState)
    , condition_(std::move(condition))
    , action_(std::move(action))
    , ttl_rounds_(ttlRounds)
    , consume_on_trigger_(consumeOnTrigger) {
    effect_id_ = effectId;
    owner_ = owner;
    registered_round_ = registeredRound;
}

PendingEffectDisposition FutureTrigger::onState(BattleContext* ctx) {
    if (!ctx || isExpired(ctx->roundCount)) {
        return PendingEffectDisposition::Consume;
    }

    const bool matched = condition_ ? condition_(ctx) : true;
    if (!matched) {
        return PendingEffectDisposition::Keep;
    }

    if (action_) {
        action_(ctx);
    }

    return consume_on_trigger_ ? PendingEffectDisposition::Consume
                               : PendingEffectDisposition::Keep;
}

bool FutureTrigger::isExpired(int currentRound) const {
    if (ttl_rounds_ < 0 || registered_round_ < 0) {
        return false;
    }
    return currentRound - registered_round_ >= ttl_rounds_;
}
