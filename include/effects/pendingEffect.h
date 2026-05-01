#ifndef PENDING_EFFECT_H
#define PENDING_EFFECT_H

#include <functional>

class BattleContext;
enum class State;

enum class PendingEffectDisposition {
    Keep,
    Consume,
};

using PendingEffectCondition = std::function<bool(BattleContext*)>;
using PendingEffectAction = std::function<void(BattleContext*)>;

/**
 * PendingEffect - 待注册效果 / 未来触发器
 *
 * 用于表达“当前先埋下一个观察者，等未来某个时点再决定是否正式落地”的效果。
 * 它本身不是已经生效的回合类效果，因此不应直接进入普通持续效果表。
 */
class PendingEffect {
public:
    virtual ~PendingEffect() = default;

    virtual State getObserveState() const = 0;
    virtual PendingEffectDisposition onState(BattleContext* ctx) = 0;
    virtual bool isExpired(int currentRound) const = 0;

    int owner() const { return owner_; }
    int getEffectId() const { return effect_id_; }
    int getRegisteredRound() const { return registered_round_; }

protected:
    int owner_ = -1;
    int effect_id_ = -1;
    int registered_round_ = -1;
};

/**
 * FutureTrigger - 最小可用的未来触发器实现
 *
 * 典型用途：
 * - “未击败对手则下回合封攻击”
 * - “若受高伤则注册补偿控”
 * - “若某节点条件满足则在未来再挂正式回合效果”
 */
class FutureTrigger : public PendingEffect {
public:
    FutureTrigger(int effectId,
                  int owner,
                  State observeState,
                  PendingEffectCondition condition,
                  PendingEffectAction action,
                  int registeredRound = -1,
                  int ttlRounds = -1,
                  bool consumeOnTrigger = true);

    State getObserveState() const override { return observe_state_; }
    PendingEffectDisposition onState(BattleContext* ctx) override;
    bool isExpired(int currentRound) const override;

private:
    State observe_state_;
    PendingEffectCondition condition_;
    PendingEffectAction action_;
    int ttl_rounds_;
    bool consume_on_trigger_;
};

#endif // PENDING_EFFECT_H
