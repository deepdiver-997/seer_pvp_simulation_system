#ifndef TIMED_BUCKET_H
#define TIMED_BUCKET_H

// 时点桶容器：按 State 触发的效果容器。
//
// 封装一整套"注册 → 同源去重 → 到时点执行 → 过期清理 → epoch 作废"的生命周期，
// 对应 docs/02-效果系统/效果系统内核设计.md 的"时点桶"概念。
//
// 设计动机：技能桶与魂印桶原先各自是裸的
//   unordered_map<State, array<map<uint64_t, unique_ptr<ContinuousEffect>>, 2>>
// 且共用同一份注册/执行/清理代码（分别散在 BattleContext::registerEffect /
// execute_bucket_actions / cleanup_expired_effects 里）。抽成一个类后：
//   - 新增一类"按 State 触发的容器"（如回合首时点的更新器桶）只需多声明一个成员；
//   - 回合计数 active_round_count_ 与桶内容绑定，不再有"必须与桶同步的影子索引"；
//   - 清理判定谓词只写一次（原先计数的 pass 与删除的 pass 各写了一遍，有漂移风险）。
//
// ⚠️ 插件边界（CLAUDE.md 3.9）：moves_lib / soul_lib 不链接 sim_core，只能调
//    **inline 方法**。因此 register_effect 是头文件内联的——它是插件注册效果的唯一入口
//    （插件此前是自己手抄一套 registerEffect 逻辑，见 resources/moves_lib/lib_1.cpp 历史）。

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>

#include <effects/continuousEffect.h>
#include <fsm/state.h>

class BattleContext;

// 单个时点、单个 owner 的效果集合。key = 同源去重键（见 timed_effect_key）。
using TimedEffectMap = std::map<uint64_t, std::unique_ptr<ContinuousEffect>>;
// 桶本体：[State][owner] → 效果集合。
using TimedBucketMap = std::unordered_map<State, std::array<TimedEffectMap, 2>>;

// 效果桶 key：同源去重用 (source_id << 32) | effect_id；
// source_id==0（无来源）用进程内自增唯一 key，不参与去重。
inline uint64_t timed_effect_key(const ContinuousEffect& effect) {
    if (effect.source_id_ > 0) {
        const int eid = effect.getEffectId();
        const uint64_t lo = static_cast<uint32_t>(eid > 0 ? eid : 0);
        return (static_cast<uint64_t>(effect.source_id_) << 32) | lo;
    }
    static uint64_t counter = 1;
    return counter++;
}

class TimedBucket {
public:
    // 注册一个效果到 (trigger, owner) 时点。
    // epoch_valid_id = 注册时该 owner 的 BattleContext::round_effect_valid_id
    //   —— 绑进 effect->valid_id_，供断回合/切换作废时 O(1) 比对。
    //
    // 行为（与重构前的 BattleContext::registerEffect 逐行等价）：
    //   ① 回合类效果 → active_round_count_++
    //   ② valid_id_ = epoch（不仅回合类：次数类 ON_STAGE 效果切换时也能惰性作废）
    //   ③ 同源去重：同 key 旧效果被覆盖；旧的若是回合类 → 计数回滚（下限 0）
    inline void register_effect(State trigger, int owner,
                                std::unique_ptr<ContinuousEffect> effect,
                                int epoch_valid_id) {
        if (!effect || owner < 0 || owner > 1) {
            return;
        }
        if (effect->isRoundEffect()) {
            ++active_round_count_[owner];
        }
        effect->valid_id_ = epoch_valid_id;

        TimedEffectMap& effects = buckets_[trigger][owner];
        const uint64_t key = timed_effect_key(*effect);
        auto it = effects.find(key);
        if (it != effects.end() && it->second->isRoundEffect()) {
            --active_round_count_[owner];
            if (active_round_count_[owner] < 0) {
                active_round_count_[owner] = 0;
            }
        }
        effects[key] = std::move(effect);
    }

    // 执行 (state, owner) 时点上所有未过期、未被作废的效果。
    // - ON_STAGE 效果：valid_id_ 与 ctx->round_effect_valid_id[owner] 不符 → 跳过执行
    //   （等待 cleanup 统一移除；TEAM 效果不检查版本号）
    // - once_ 效果：本趟执行后移除（下回合重注册再生效）
    // 在 .cpp：需要完整的 BattleContext 类型。
    void execute_at(State state, int owner, BattleContext* ctx);

    // 清理：(a) 自然过期（isExpired）、(b) 被 epoch 作废的 ON_STAGE 效果。
    // 同时扣减 active_round_count_。判定谓词只在此处写一次。
    void cleanup(int current_round, const int (&epoch_valid_id)[2]);

    // 作废后归零回合计数（epoch 递增由 BattleContext 负责——该 epoch 由两个桶共享）。
    void reset_round_count(int owner) {
        if (owner >= 0 && owner <= 1) {
            active_round_count_[owner] = 0;
        }
    }

    void clear() {
        buckets_.clear();
        active_round_count_[0] = 0;
        active_round_count_[1] = 0;
    }

    int active_round_count(int owner) const {
        return (owner >= 0 && owner <= 1) ? active_round_count_[owner] : 0;
    }

    bool empty_at(State state, int owner) const;

    // 直接访问某个 (时点, owner) 的效果集合（供查询模板/调试/测试用）。
    // 非 const 版按需插入空桶（语义同先前的 operator[]）。
    TimedEffectMap& at(State trigger, int owner) { return buckets_[trigger][owner]; }
    const TimedEffectMap& at(State trigger, int owner) const;

    // 遍历全部桶（供 getFullStateJson 输出）。
    const TimedBucketMap& all() const { return buckets_; }

private:
    TimedBucketMap buckets_;
    // 本桶内回合类效果的计数（O(1) 查"是否还有回合类效果"，供 break_round_effects 用）。
    // 与桶内容在一处维护，故不存在与桶不同步的可能。
    int active_round_count_[2] = {0, 0};
};

#endif  // TIMED_BUCKET_H
