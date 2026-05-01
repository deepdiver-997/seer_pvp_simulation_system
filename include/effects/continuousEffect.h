#ifndef CONTINUOUS_EFFECT_H
#define CONTINUOUS_EFFECT_H

#include <memory>
#include <functional>
#include <effects/effect.h>

class BattleContext;
class Skills;
enum class State;

/**
 * ContinuousEffect - 持续效果基类
 *
 * Skill/魂印中的 Effect 是模板（函数指针+参数），不可变。
 * ContinuousEffect 是 Effect 的实例化，保存运行状态和上下文。
 *
 * 回合过期机制：
 * - registered_round_: 注册时的回合数
 * - duration_rounds_: 持续回合数
 * 判断过期：currentRound - registered_round_ >= duration_rounds_
 */
class ContinuousEffect {
public:
    explicit ContinuousEffect(int owner = -1) : owner_(owner) {}
    virtual ~ContinuousEffect() = default;

    virtual bool operator()(BattleContext* ctx) = 0;
    virtual bool check(BattleContext* ctx) const { (void)ctx; return false; }
    virtual bool consume(BattleContext* ctx) { (void)ctx; return false; }
    virtual State getTriggerState() const = 0;
    int owner() const { return owner_; }
    virtual bool isExpired(int currentRound) const = 0;
    virtual int getEffectId() const = 0;
    virtual bool isRoundEffect() const { return false; }
    virtual int getEffectCategory() const { return -1; }
    virtual int getRegisteredRound() const { return -1; }

    // 回合效果版本号 — 用于 O(1) 断回合
    // 注册时从 BattleContext::round_effect_valid_id[owner] 复制。
    // 执行前比较 this->valid_id_ == ctx->round_effect_valid_id[owner]，
    // 不等说明该效果已被断回合失效。
    int valid_id_ = 0;

protected:
    int owner_;
};

/**
 * ContinuousEffectFromEffect - 将 Effect 包装为 ContinuousEffect
 */
class ContinuousEffectFromEffect : public ContinuousEffect {
protected:
    Effect effect_;
    State triggerState_;
    int registered_round_;   // 注册时的回合数
    int duration_rounds_;    // 持续回合数

public:
    ContinuousEffectFromEffect() : ContinuousEffect(-1), effect_(), registered_round_(-1), duration_rounds_(-1) {}
    ContinuousEffectFromEffect(Effect e, State trigger, int owner, int duration, int registeredRound);

    bool operator()(BattleContext* ctx) override;
    State getTriggerState() const override { return triggerState_; }
    bool isExpired(int currentRound) const override;
    int getEffectId() const override { return effect_.logic ? effect_.id : -1; }
    bool isRoundEffect() const override { return duration_rounds_ > 0; }
    int getEffectCategory() const override { return effect_.logic ? effect_.id : -1; }
    Effect* getEffect() { return &effect_; }
    int getRegisteredRound() const override { return registered_round_; }
};

/**
 * RoundContinuousEffect - 回合类持续效果
 */
template<int EffectId, State Trigger, int InitRounds = -1>
class RoundContinuousEffect : public ContinuousEffect {
    int registered_round_;   // 注册时的回合数
    int duration_rounds_;    // 持续回合数
public:
    RoundContinuousEffect(int owner, int duration = InitRounds, int registeredRound = -1);

    bool operator()(BattleContext* ctx) override;
    State getTriggerState() const override { return Trigger; }
    bool isExpired(int currentRound) const override;
    int getEffectId() const override { return EffectId; }
    bool isRoundEffect() const override { return true; }
    int getEffectCategory() const override { return EffectId; }
    int getRegisteredRound() const override { return registered_round_; }
};

/**
 * CountContinuousEffect - 次数类持续效果
 */
template<int EffectId, State Trigger, int InitCount = -1>
class CountContinuousEffect : public ContinuousEffect {
    int remainingCount_;
    bool decrementOnTrigger_;
public:
    CountContinuousEffect(int owner, int count = InitCount, bool decrementOnTrigger = true);

    bool operator()(BattleContext* ctx) override;
    bool check(BattleContext* ctx) const override;
    bool consume(BattleContext* ctx) override;
    State getTriggerState() const override { return Trigger; }
    bool isExpired(int currentRound) const override;
    int getEffectId() const override { return EffectId; }
    int getRegisteredRound() const override { return -1; }
};

/**
 * SkillExecutionEffect - 技能执行器
 */
class SkillExecutionEffect : public ContinuousEffect {
public:
    SkillExecutionEffect(int owner, int skillIndex, State trigger);

    bool operator()(BattleContext* ctx) override;
    State getTriggerState() const override { return triggerState_; }
    bool isExpired(int currentRound) const override { (void)currentRound; return false; }
    void onRoundEnd() {}
    int getEffectId() const override { return -1; }
    int getRegisteredRound() const override { return -1; }
    SkillExecResult getLastResult() const { return lastSkillResult_; }
    const SkillResolutionFlags& getLastResolutionFlags() const { return lastResolutionFlags_; }

private:
    bool calculateHit(BattleContext* ctx, int attackerId, int skillIndex);
    bool isHitEffectInvalid(BattleContext* ctx, int attackerId, int skillIndex) const;
    void applySkillResult(SkillExecResult result);
    void registerBranch(BattleContext* ctx, SkillExecResult result, const Skills& skill);

    int owner_;
    int skillIndex_;
    State triggerState_;
    SkillExecResult lastSkillResult_;
    SkillResolutionFlags lastResolutionFlags_;
};

#endif // CONTINUOUS_EFFECT_H
