/**
 * abnormal-applicator.cpp — 施加异常状态的统一入口
 *
 * ================================================================
 * 设计原则
 * ================================================================
 *
 * apply_anomaly() 是系统中"向精灵施加异常状态"的唯一路径。
 * 所有调用方（技能效果、魂印效果、场地效果、道具等）都通过此函数施加异常。
 *
 * 为什么集中在一个函数？
 *   1. 异常免疫机制多种多样（魂免、装备免、场地免……），分散在各处极易遗漏
 *   2. 未来新增阻止条件时，只需在此函数加检查分支，所有调用方自动生效
 *   3. 日志/调试在此集中输出，排查"为什么挂不上异常"时只看这里
 *
 * 为什么不放在动态库？
 *   1. 需要访问 BattleContext 的完整内部状态，plugin interface 暴露不了
 *   2. 这是副作用函数（修改多处 Context 状态），不适合 dylib 模式
 *   3. 被多处调用，放 dylib 意味着更新耦合
 *
 * ================================================================
 * 施加流程（按顺序执行的检查链）
 * ================================================================
 *
 * [Step 1] 参数校验
 *   - target 必须为 0 或 1
 *   - anomaly_id 必须在合法范围 (0~35)
 *
 * [Step 2] 目标存活检查
 *   - 已死亡的目标无法被施加异常
 *
 * [Step 3] 免疫检查 — Mark ID 0（异常免疫标记）
 *   - 遍历 pet.marks，查找 id == 0 的标记
 *   - 标记来源：
 *     a. 全场魂免精灵：初始化时挂 Mark(0)，永不移除
 *     b. 条件魂免：魂印按条件增删 Mark(0)
 *        "回合开始时体力 > 1/2 则本回合免控"
 *        → BATTLE_ROUND_START 加标记，BATTLE_ROUND_END 移除
 *     c. 技能效果："使用后自身下两回合免控"
 *        → add_mark(count=2)，回合结束 tick_mark_count
 *
 * [Step 4] 同种异常 — 回合覆盖
 *   - 如果目标已存在相同 anomaly_id，比较剩余回合数
 *   - 新的回合数 > 旧的剩余回合 → 覆盖
 *   - 新的回合数 <= 旧的剩余回合 → 不做任何操作
 *
 * [Step 5] 控场替换
 *   - 控场类异常（Efftype=0）施加时，清除旧的控场类异常
 *   - 但施加和旧的如果是同种，走 Step 4 的覆盖逻辑
 *   - 例：目标已麻痹，被施加害怕 → 清除麻痹，施加害怕
 *   - 例：目标已麻痹，被施加剧毒 → 保留麻痹，施加剧毒（毒是弱化类）
 *
 * [Step 6] 特殊阻止检查（预留扩展点）
 *   - 装备/称号：概率免疫
 *   - 场地效果：特定异常免疫
 *   - 保护机制：击败后保护回合
 *   - 异常抗性系统：按百分比抵抗
 *
 * [Step 7] 执行施加
 *   - 写入 abnormal_status_end_round[target][anomaly_id]
 *   - abnormal_status_end_round 是异常状态的唯一权威数据源
 *
 * ================================================================
 * 扩展指南
 * ================================================================
 *
 * 当需要新增一种"阻止异常施加"的机制时：
 *   1. 在对应的 Step 位置加入检查
 *   2. 如需新增返回枚举值，在 abnormal-applicator.h 中添加
 *   3. 在此处记录新增机制的来源和触发条件
 * ================================================================
 */

#include <abnormal-system/abnormal-applicator.h>
#include <abnormal-system/abnormal-types.h>
#include <entities/elf-pet.h>
#include <entities/mark.h>
#include <fsm/battleContext.h>

#include <iostream>

ApplyAnomalyResult apply_anomaly(BattleContext* ctx,
                                  int target,
                                  int anomaly_id,
                                  int duration_rounds) {
    // ----------------------------------------------------------------
    // [Step 1] 参数校验
    // ----------------------------------------------------------------
    if (target < 0 || target > 1) {
        return ApplyAnomalyResult::INVALID_PARAM;
    }
    if (!is_valid_abnormal_status_id(anomaly_id)) {
        return ApplyAnomalyResult::INVALID_PARAM;
    }
    if (duration_rounds <= 0) {
        duration_rounds = random_anomaly_duration();
    }

    ElfPet& pet = ctx->getPet(target);

    // ----------------------------------------------------------------
    // [Step 2] 目标存活检查
    // ----------------------------------------------------------------
    if (pet.hp <= 0) {
        return ApplyAnomalyResult::TARGET_DEFEATED;
    }

    // ----------------------------------------------------------------
    // [Step 3] 魂免检查 — Mark ID 0
    // ----------------------------------------------------------------
    if (has_mark(pet.marks, 0)) {
        return ApplyAnomalyResult::TARGET_IMMUNE;
    }

    // ----------------------------------------------------------------
    // [Step 4] 回合类效果检查 — Mark ID 0
    // ----------------------------------------------------------------

    bool already_has_same = ctx->has_active_abnormal_status(target, anomaly_id);

    // ----------------------------------------------------------------
    // [Step 4] 同种异常 — 回合覆盖
    //
    // 再次施加同种异常时，取剩余回合数较长的那个。
    //
    // 例：目标已麻痹剩余 1 回合，被施加麻痹 3 回合
    //     → 用 3 回合覆盖，返回 DURATION_EXTENDED
    // 例：目标已麻痹剩余 2 回合，被施加麻痹 1 回合
    //     → 不做操作，返回 SUCCESS（不改变已有状态）
    // ----------------------------------------------------------------
    if (already_has_same) {
        int current_end = ctx->get_abnormal_status_end_round(target, anomaly_id);
        int current_remaining = current_end - ctx->roundCount;
        if (duration_rounds > current_remaining) {
            ctx->set_abnormal_status_end_round(target, anomaly_id,
                                               ctx->roundCount + duration_rounds);
            return ApplyAnomalyResult::DURATION_EXTENDED;
        }
        // 新回合数不更长，不覆盖，但也不算失败——异常已经存在
        return ApplyAnomalyResult::SUCCESS;
    }

    // ----------------------------------------------------------------
    // [Step 6] 特殊阻止检查（预留扩展点）
    //
    // 未来需要在此处添加：
    //
    // a. 异常抗性系统：
    //    精灵可以通过抗性训练获得对特定异常的抗性百分比。
    //    例如"麻痹抗性 35%"→ 35% 概率抵抗麻痹。
    //    → 检查 ResistanceSystem，roll 概率
    //    → 返回 BLOCKED_BY_EFFECT
    //
    // b. 装备/称号免疫：
    //    某称号"30% 概率免疫异常"
    //
    // c. 场地效果阻止：
    //    某场地"双方无法进入麻痹状态"
    //
    // d. 保护机制（BATTLE_NEW_DEFEAT_MECHANISM）：
    //    精灵在被击败后的保护回合内不受异常
    //
    // 建议：每个阻止条件用独立的辅助函数，在此处依次调用
    // ----------------------------------------------------------------

    // ----------------------------------------------------------------
    // [Step 7] 执行施加
    //
    // abnormal_status_end_round[target][anomaly_id] 是异常状态的唯一权威数据源。
    // 其他需要查询异常状态的地方（AI 决策、效果判定等）都从这里读取。
    // ----------------------------------------------------------------
    ctx->set_abnormal_status_end_round(target, anomaly_id,
                                       ctx->roundCount + duration_rounds);

#ifdef BATTLE_FSM_VERBOSE_DEFAULT
    std::cout << "[apply_anomaly] target=" << target
              << " anomaly=" << abnormal_status_name_cn(anomaly_id)
              << "(" << anomaly_id << ")"
              << " duration=" << duration_rounds
              << " end_round=" << (ctx->roundCount + duration_rounds)
              << std::endl;
#endif

    return ApplyAnomalyResult::SUCCESS;
}
