#ifndef IMMUNITY_CENTER_H
#define IMMUNITY_CENTER_H

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

/**
 * ImmunityType - 免疫类型
 *
 * 免疫内核按"威胁类型"回答查询。枚举值增长 = 数据扩展。
 * - BREAK    : 免断（本时点不可被断回合）
 * - DAMAGE   : 免伤
 * - STAT_DROP: 免弱（能力下降免疫）
 * - ANOMALY  : 异常免疫，用 anomaly_mask 细分（0 = 全 36 异常免疫；否则按位）
 */
enum class ImmunityType {
    BREAK,
    DAMAGE,
    STAT_DROP,
    ANOMALY,
};

/**
 * ImmunityProvider - 一条免疫记录（纯数据）
 *
 * 高低级免断/魂免 = coverage 覆盖集：全置位 = 闭环（任何时点不可被断/被控）；
 * 只含部分时点 = 低级，未覆盖时点 is_immune 返回 false，原语照常执行。
 *
 * 生命周期：
 * - duration_rounds == 0 永久；>0 窗口（register_round 起算）。
 * - 快照语义：由"回合开始"程序调用 grant 写入，窗口自然过期，无手动 cancel。
 * - 按异常免疫（天生免疫xxx）：type=ANOMALY + anomaly_mask 具体位。
 */
struct ImmunityProvider {
    int owner;
    ImmunityType type;
    int register_round;
    int duration_rounds;    // 0 = 永久; >0 = 窗口
    uint64_t coverage;      // 时点 bitset（bit = (int)State + 1）; 全置位 = 闭环
    uint64_t anomaly_mask;  // ANOMALY 专用：免疫哪些异常（36 位）；0 = 全免
    bool active;
    int condition_id;       // 预留：条件目录枚举，-1 = 无条件（live 条件后续迭代）
    int source_id;          // 授予句柄，revoke / 复用更新用
};

/**
 * ImmunityCenter - 免疫内核
 *
 * 统一"在时点 T 对威胁 X 免疫吗"的查询。断回合、异常施加、伤害、能力下降等
 * 原语在动作前查询 is_immune()；免疫源（魂印/技能）经 grant/revoke 管理。
 *
 * 设计要点：
 * - 独立于 skills_effects 桶：天然不可被断、不污染 has_round_effects()。
 * - is_immune 是纯查询（O(#providers)），不做任何副作用。
 * - 不带 BattleContext 依赖：coverage 用归一化 bit 传入，条件由授予方评估。
 */
class ImmunityCenter {
public:
    ImmunityCenter() = default;
    ImmunityCenter(const ImmunityCenter&) = delete;
    ImmunityCenter& operator=(const ImmunityCenter&) = delete;

    /**
     * 授予免疫。
     * @param owner          目标方 (0/1)
     * @param type           免疫类型
     * @param coverage       时点 bitset（bit = (int)State + 1）；全置位 = 闭环
     * @param anomaly_mask   ANOMALY 专用：0 = 全免；否则按 status_id 位
     * @param duration_rounds 0 = 永久; >0 = 窗口
     * @param register_round 窗口起算回合（通常传 ctx->roundCount）
     * @param source_id      0 = 新建并返回新句柄; >0 = 复用更新已有句柄（快照程序每回合 re-grant）
     * @return source_id    用于 revoke / 下次复用
     */
    int grant(int owner, ImmunityType type, uint64_t coverage, uint64_t anomaly_mask,
              int duration_rounds, int register_round, int source_id = 0) {
        const int sid = (source_id != 0) ? source_id : next_source_id_++;
        auto& list = providers_[owner];
        for (auto& p : list) {
            if (p.source_id == sid) {
                // 复用更新：刷新窗口与覆盖（快照程序每回合 re-grant 同句柄）
                p.type = type;
                p.register_round = register_round;
                p.duration_rounds = duration_rounds;
                p.coverage = coverage;
                p.anomaly_mask = anomaly_mask;
                p.active = true;
                p.condition_id = -1;
                return sid;
            }
        }
        list.push_back(ImmunityProvider{owner, type, register_round, duration_rounds,
                                        coverage, anomaly_mask, true, -1, sid});
        return sid;
    }

    void revoke(int owner, int source_id) {
        auto it = providers_.find(owner);
        if (it == providers_.end()) {
            return;
        }
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [source_id](const ImmunityProvider& p) {
                                      return p.source_id == source_id;
                                  }),
                   list.end());
        if (list.empty()) {
            providers_.erase(it);
        }
    }

    /**
     * is_immune - 查询目标在给定时点是否免疫。
     * @param timing_bit  当前时点的归一化 bit（由 BattleContext 提供）
     * @param current_round 当前回合（窗口过期内联检查，避免 cleanup 前误答）
     * @param status_id   ANOMALY 类型专用：被查询的异常状态 id；其余类型忽略
     */
    bool is_immune(int owner, ImmunityType type, uint64_t timing_bit,
                   int current_round, int status_id = 0) const {
        auto it = providers_.find(owner);
        if (it == providers_.end()) {
            return false;
        }
        for (const auto& p : it->second) {
            if (!p.active || p.type != type) continue;
            if (p.duration_rounds > 0
                && current_round - p.register_round >= p.duration_rounds) {
                continue;  // 窗口已过，等 cleanup 回收
            }
            if (!(p.coverage & timing_bit)) continue;  // 本时点不在覆盖内
            if (type == ImmunityType::ANOMALY && p.anomaly_mask != 0
                && status_id >= 0 && !((p.anomaly_mask >> status_id) & 1u)) {
                continue;  // 该具体异常不在免疫列表
            }
            return true;
        }
        return false;
    }

    /** 移除窗口已过的 Provider。由 BattleContext::cleanup_expired_effects 统一调用。 */
    void cleanup(int current_round) {
        for (auto it = providers_.begin(); it != providers_.end();) {
            auto& list = it->second;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [current_round](const ImmunityProvider& p) {
                                          return p.duration_rounds > 0
                                              && current_round - p.register_round >= p.duration_rounds;
                                      }),
                       list.end());
            if (list.empty()) {
                it = providers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clear_all() {
        providers_.clear();
    }

    int provider_count(int owner) const {
        auto it = providers_.find(owner);
        return it == providers_.end() ? 0 : static_cast<int>(it->second.size());
    }

private:
    std::unordered_map<int, std::vector<ImmunityProvider>> providers_;
    int next_source_id_ = 1;
};

#endif // IMMUNITY_CENTER_H
