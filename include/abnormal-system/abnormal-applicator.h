#ifndef ABNORMAL_APPLICATOR_H
#define ABNORMAL_APPLICATOR_H

#include <abnormal-system/abnormal-types.h>

class BattleContext;

/**
 * ApplyAnomalyResult - 施加异常状态的返回结果
 */
enum class ApplyAnomalyResult {
    SUCCESS,               // 成功施加
    TARGET_IMMUNE,         // 目标免疫异常（Mark ID 0 生效中）
    BLOCKED_BY_EFFECT,     // 被其他效果阻止（装备/场地/特定保护效果）
    TARGET_DEFEATED,       // 目标已死亡
    INVALID_PARAM,         // 无效参数（anomaly_id 非法、target 非法）
    REPLACED_EXISTING,     // 替换了已有的控场类异常
    DURATION_EXTENDED,     // 同种异常已存在，延长了回合数
};

/**
 * random_anomaly_duration - 生成异常状态的随机持续回合数
 *
 * 绝大多数异常状态持续 2~3 回合（具体区间待确认）。
 * 当前占位实现直接返回 2。
 */
inline int random_anomaly_duration() {
    // TODO: 改为随机 2~3 回合
    return 2;
}

/**
 * apply_anomaly - 向目标施加异常状态的统一入口
 *
 * 这是整个战斗系统中"施加异常"的唯一路径。
 * 所有技能效果、魂印效果、场地效果等，只要需要给精灵挂异常，都必须通过此函数。
 *
 * @param ctx             战斗上下文
 * @param target          目标方 (0 或 1)
 * @param anomaly_id      异常状态 ID (0~35, 对应 AbnormalStatusId)
 * @param duration_rounds 持续回合数 (-1 表示使用 random_anomaly_duration())
 * @return                施加结果
 *
 * 检查流程（按顺序）：
 *   [1] 参数校验
 *   [2] 目标存活检查
 *   [3] 免疫检查 — Mark ID 0（异常免疫标记）
 *   [4] 同种异常 — 比较剩余回合，用长的覆盖短的
 *   [5] 控场替换 — 已有控场则清除旧控场，施加新控场
 *   [6] 特殊阻止检查 — 被动效果/装备/场地等的阻止（预留）
 *   [7] 执行施加 — 写入 abnormal_status_end_round
 */
ApplyAnomalyResult apply_anomaly(BattleContext* ctx,
                                  int target,
                                  int anomaly_id,
                                  int duration_rounds = -1);

/**
 * 便捷函数：尝试施加异常，成功返回 true
 */
inline bool try_apply_anomaly(BattleContext* ctx,
                              int target,
                              int anomaly_id,
                              int duration_rounds = -1) {
    ApplyAnomalyResult r = apply_anomaly(ctx, target, anomaly_id, duration_rounds);
    return r == ApplyAnomalyResult::SUCCESS
        || r == ApplyAnomalyResult::REPLACED_EXISTING
        || r == ApplyAnomalyResult::DURATION_EXTENDED;
}

#endif // ABNORMAL_APPLICATOR_H
