#ifndef CONTINUOUS_EFFECT_H
#define CONTINUOUS_EFFECT_H

#include <memory>
#include <functional>
#include <effects/effect.h>

class BattleContext;
enum class State;

/**
 * ContinuousEffect - 持续效果基类
 *
 * Skill/魂印中的 Effect 是模板（函数指针+参数），不可变。
 * ContinuousEffect 是 Effect 的实例化，保存运行状态和上下文。
 */
class ContinuousEffect {
public:
    virtual ~ContinuousEffect() = default;

    virtual bool operator()(BattleContext* ctx) = 0;
    virtual bool check(BattleContext* ctx) const { (void)ctx; return false; }
    virtual bool consume(BattleContext* ctx) { (void)ctx; return false; }
    virtual void onRoundEnd() {}
    virtual State getTriggerState() const = 0;
    int owner() const { return owner_; }
    virtual bool isExpired() const = 0;
    virtual int getEffectId() const = 0;
    virtual bool isRoundEffect() const { return false; }
    virtual int getEffectCategory() const { return -1; }
    virtual int getCreatedInId() const { return -1; }

protected:
    int owner_;
};

/**
 * SkillExecResult - 技能执行结果
 */
enum class SkillExecResult {
    HIT,
    MISS,
    EFFECT_INVALID,
    SKILL_INVALID,
};

/**
 * ContinuousEffectFromEffect - 将 Effect 包装为 ContinuousEffect
 */
class ContinuousEffectFromEffect : public ContinuousEffect {
protected:
    Effect* effect_;
    State triggerState_;
    int createdInId_;
    int remainingRounds_;

public:
    ContinuousEffectFromEffect() : owner_(-1), effect_(nullptr), remainingRounds_(-1), createdInId_(-1), triggerState_(State::GAME_START) {}
    ContinuousEffectFromEffect(Effect* e, State trigger, int owner, int rounds = -1, int validId = -1);

    bool operator()(BattleContext* ctx) override;
    State getTriggerState() const override { return triggerState_; }
    bool isExpired() const override;
    void onRoundEnd() override;
    int getEffectId() const override { return effect_ ? effect_->id : -1; }
    bool isRoundEffect() const override { return remainingRounds_ > 0; }
    int getEffectCategory() const override { return effect_ ? effect_->id : -1; }
    Effect* getEffect() const { return effect_; }
    int getCreatedInId() const override { return createdInId_; }
};

/**
 * RoundContinuousEffect - 回合类持续效果
 */
template<int EffectId, State Trigger, int InitRounds = -1>
class RoundContinuousEffect : public ContinuousEffect {
    int createdInId_;
    int remainingRounds_;
public:
    RoundContinuousEffect(int owner, int rounds = InitRounds, int validId = -1);

    bool operator()(BattleContext* ctx) override;
    State getTriggerState() const override { return Trigger; }
    bool isExpired() const override;
    void onRoundEnd() override;
    int getEffectId() const override { return EffectId; }
    bool isRoundEffect() const override { return true; }
    int getEffectCategory() const override { return EffectId; }
    int getCreatedInId() const override { return createdInId_; }
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
    bool isExpired() const override;
    void onRoundEnd() override;
    int getEffectId() const override { return EffectId; }
    int getCreatedInId() const override { return -1; }
};

/**
 * SkillExecutionEffect - 技能执行器
 */
class SkillExecutionEffect : public ContinuousEffect {
public:
    SkillExecutionEffect(int owner, int skillIndex, State trigger);

    bool operator()(BattleContext* ctx) override;
    State getTriggerState() const override { return triggerState_; }
    bool isExpired() const override { return false; }
    void onRoundEnd() override {}
    int getEffectId() const override { return -1; }
    int getCreatedInId() const override { return -1; }
    int getLastResult() const { return lastSkillResult_; }

private:
    bool calculateHit(BattleContext* ctx, int attackerId, int skillIndex);
    void registerBranch(BattleContext* ctx, SkillExecResult result, const Skills& skill);

    int skillIndex_;
    State triggerState_;
    int lastSkillResult_;
};

#endif // CONTINUOUS_EFFECT_H
