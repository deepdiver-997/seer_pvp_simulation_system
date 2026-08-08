#ifndef DAMAGE_PIPELINE_H
#define DAMAGE_PIPELINE_H

#include <array>
#include <functional>
#include <vector>

class BattleContext;

/**
 * DamagePhase - 伤害修正节点（有序）
 *
 * 伤害值在 workspace（resolvedDamage.final）中流经各节点，每个节点的效果读取/修改它。
 * 节点顺序决定机制胜负：
 *   - "靠后增伤击穿锁伤" = 那个增伤挂在 CAP 之后的节点；
 *   - "无法被击穿的锁伤" = 那个锁伤挂在 FINAL_CORRECT（最终修正）。
 *
 * 顺序本身是游戏知识整理出的配置（像 FSM 状态序），此处为初始序，可按需调整。
 */
enum class DamagePhase {
    AMP,           // 增伤（进攻）
    REDUCE,        // 减伤（防御）
    FLOOR,         // 保底（最低伤害）
    CAP,           // 锁伤（最高伤害）
    DETECT,        // 挡伤检测/触发（如 受高伤回血、反伤）
    FINAL_CORRECT, // 最终修正（归零/不可击穿的锁伤）
};

/**
 * DamageEffectCategory - 伤害效果类别（用于按类别抑制）
 *
 * 沧岚"使对手挡伤失效" = 置对手 `damage_suppress_mask |= MITIGATE|DETECT`，
 * 该对手所有 MITIGATE/DETECT 类别的伤害效果在 walk 时被跳过（含检测归零的回血）。
 * 增伤（AMP）不受影响。新效果声明类别即可被同类抑制自动覆盖，无两两硬编码。
 */
enum class DamageEffectCategory {
    AMP,      // 增伤（进攻）
    MITIGATE, // 减伤/挡伤（防御：减伤、保底、锁伤）
    DETECT,   // 检测/触发（防御：受高伤回血、反伤）
};

struct DamageEffect {
    DamageEffectCategory category;
    std::function<void(BattleContext*, int owner)> fn;  // owner = 该效果所属方
};

/**
 * DamagePipeline - 伤害修正管线
 *
 * 每阶段每方一个效果桶；walk 按阶段序执行，读/写 ctx->resolvedDamage（伤害值）。
 * 执行前检查所属方的 damage_suppress_mask，被抑制的类别直接跳过。
 * 萨瑞卡式"中途检测+归零" = 在 DETECT 阶段读 resolvedDamage.final，> 阈值则置 0。
 */
class DamagePipeline {
public:
    static constexpr int kPhaseCount = 6;

    DamagePipeline() = default;
    DamagePipeline(const DamagePipeline&) = delete;
    DamagePipeline& operator=(const DamagePipeline&) = delete;

    void register_effect(DamagePhase phase, int owner, DamageEffectCategory category,
                         std::function<void(BattleContext*, int)> fn) {
        if (owner < 0 || owner > 1) {
            return;
        }
        buckets_[static_cast<int>(phase)][owner].push_back(DamageEffect{category, std::move(fn)});
    }

    /**
     * walk - 按阶段序执行双方伤害效果。
     * 各阶段内先执行攻击方，再执行防御方（顺序可随游戏知识调整）。
     * 效果通过 ctx->resolvedDamage 读取/修改当前伤害值。
     */
    void run(BattleContext* ctx, int attacker, int defender);

    void clear() {
        for (auto& owner_buckets : buckets_) {
            for (auto& bucket : owner_buckets) {
                bucket.clear();
            }
        }
    }

    static constexpr DamagePhase kOrder[kPhaseCount] = {
        DamagePhase::AMP,
        DamagePhase::REDUCE,
        DamagePhase::FLOOR,
        DamagePhase::CAP,
        DamagePhase::DETECT,
        DamagePhase::FINAL_CORRECT,
    };

private:
    // [phase][owner] -> effects
    std::array<std::array<std::vector<DamageEffect>, 2>, kPhaseCount> buckets_{};
};

#endif // DAMAGE_PIPELINE_H
