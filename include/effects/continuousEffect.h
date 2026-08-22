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

// 效果类别（区分行为差异，替代多重继承）
enum class EffectKind {
    GENERIC,    // 通用效果（到时点执行函数指针）
    ROUND,      // 回合效果（isRoundEffect()=true）
};

// 效果作用域：ON_STAGE = 当前场上精灵，切换作废；TEAM = 全队绑定，切换保留
enum class EffectScope {
    ON_STAGE,
    TEAM,
};

class ContinuousEffect {
public:
    explicit ContinuousEffect(int owner = -1) : owner_(owner) {}

    // 构造：持有 Effect（函数指针+参数）+ 运行时状态。
    // duration_rounds_ > 0 = 回合效果；<= 0 = 永久/一次性。
    ContinuousEffect(Effect e, State trigger, int owner, int duration, int registeredRound)
        : owner_(owner)
        , effect_(std::move(e))
        , trigger_state_(trigger)
        , registered_round_(registeredRound)
        , duration_rounds_(duration) {}

    virtual ~ContinuousEffect() = default;

    // 内联：执行内部 Effect 模板函数。isExpired 由调用方（execute_bucket_actions）先查，
    // 这里不再重复检查（避免头文件访问 ctx->roundCount 需要完整 BattleContext 类型）。
    virtual bool operator()(BattleContext* ctx) {
        if (!effect_.logic) {
            return false;
        }
        effect_.logic(ctx, effect_.args);
        return true;
    }
    virtual bool check(BattleContext* ctx) const { (void)ctx; return false; }
    virtual bool consume(BattleContext* ctx) { (void)ctx; return false; }
    State getTriggerState() const { return trigger_state_; }
    int owner() const { return owner_; }
    virtual bool isExpired(int currentRound) const {
        if (duration_rounds_ < 0) {
            return false;  // 永久效果
        }
        return currentRound - registered_round_ >= duration_rounds_;
    }
    int getEffectId() const { return effect_.logic ? effect_.id : -1; }
    bool isRoundEffect() const { return duration_rounds_ > 0; }
    int getEffectCategory() const { return effect_.logic ? effect_.id : -1; }
    Effect* getEffect() { return &effect_; }
    int getRegisteredRound() const { return registered_round_; }

    // 回合效果版本号 — 用于 O(1) 断回合/切换作废
    // 注册时从 BattleContext::round_effect_valid_id[owner] 复制。
    // 执行前比较 this->valid_id_ == ctx->round_effect_valid_id[owner]，
    // 不等说明该效果已被断回合/切换失效（仅 ON_STAGE 效果检查）。
    int valid_id_ = 0;

    // 来源标识：技能/魂印 ID。>0 时同源去重（同 key 覆盖旧效果）；0 = 不参与去重。
    int source_id_ = 0;
    // 作用域：ON_STAGE（切换作废）/ TEAM（切换保留，不可被清回合类作废）
    EffectScope scope_ = EffectScope::ON_STAGE;
    // 效果类别（GENERIC / ROUND / SKILL_EXEC）
    EffectKind kind_ = EffectKind::GENERIC;

protected:
    int owner_;
    Effect effect_;                    // 效果模板（函数指针+参数）
    State trigger_state_{};            // 由构造函数指定
    int registered_round_ = -1;        // 注册时的回合数
    int duration_rounds_ = -1;         // 持续回合数；>0 = 回合效果
};

#endif // CONTINUOUS_EFFECT_H
