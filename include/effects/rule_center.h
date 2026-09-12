#ifndef RULE_CENTER_H
#define RULE_CENTER_H

// ═══════════════════════════════════════════════════════════════════════════
// RuleCenter —— 统一"被查询消费"的规则容器（header-only，插件可直调）
//
// 合并三个平行容器：ImmunityCenter(免疫) / SkillInvalidCenter(盔威封属+命中失效)
//                      / hit_effect_invalids(③层命中失效)  归并为单一容器。
//
// 核心洞察：免疫、盔威封属、③层命中失效本质都是"等待被查询、按 source 挂载、按
// target 生效、按 次数/回合 消费"的**规则条目(ticket)**，不同的是 kind 与查询/消费方式。
// 旧容器都借 owner 兼任了 source(挂载方)与 target(生效对象)两维，导致：
//   - 封属真施放方被塞进 source_slot/effect_id，断 target 回合会清掉"别人施放"的封属；
//   - 全队异常免(魂印 TEAM 保一队)无法表达"source=魂印宿主、target=每个成员"。
//
// 统一后每条 RuleTicket 显式带 source(挂载)→target(生效)两维：
//   - 免疫   ：target==source_owner(单对象，被护方自身)；问 is_immune(target,...)
//   - 封属   ：source=施放方、target=被封方；问 notify(user)，扫 target==user 响应并消费次数
//   - ③层失效：target=被失效方(防御方)；问 consume_hit_invalid(defender)
//
// 生命周期统一锚 **source**：施放方换宠清其名下 ON_STAGE 条目、断施放方回合清其名下
// 回合类封属 —— "封属属于施放方，解封要断施放方的回合"，而非旧"断被封方回合即解"。
// 免疫单对象(source==target)，被护方换宠清 ON_STAGE(切回原位语义不变)、TEAM 保留。
//
// 覆盖语义(**用户定**：同来源同效果覆盖、不追加)：覆盖键 = (source_owner, source_effect_id,
// category, subtype)。subtype 区分细类(见下)，故"免异常+免弱"(903)同源同 effect 因
// subtype 不同各占一条，不被误合并。
//
// ⚠️ header-only 是为了插件能调：插件 dylib 不链接 sim_core（CLAUDE.md 3.9），
//    原 ImmunityCenter 即全头内联。RuleCenter 遵循之——所有方法定义在本头，不建 .cpp。
// ═══════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <effects/continuousEffect.h>  // EffectScope(ON_STAGE/TEAM)

class BattleContext;

// 免疫类型（原 immunity_center.h 迁入）。规则大类 IMMUNE 的 subtype。
enum class ImmunityType {
    BREAK,       // 免断（本时点不可被断回合）
    DAMAGE,      // 免伤
    STAT_DROP,   // 免弱（能力下降免疫）
    ANOMALY,     // 异常免疫，用 anomaly_mask 细分（0 = 全异常免疫；否则按位）
    HEAL_BLOCK,  // 封回血（封锁体力回复，位覆盖时点仿魂免）
};

// 规则大类。细分在 subtype(见 RuleTicket)：
enum class RuleCategory {
    IMMUNE,      // 免疫(纯查询不消费)：subtype=ImmunityType + coverage/mask/soul。**天然不可被断**（source_valid_id 恒 0，只随上下场清）
    SEAL,        // 盔/威/封属/命中失效(响应即消费)：subtype=SealKind。随来源效果被断作废
    HIT_INVALID, // ③层命中效果失效(防御方按次生效)：subtype=HitInvalidMode
    REFLECT,     // 回弹：target 免疫异常时反弹给施放方(apply_anomaly 反射)。支持 counts/rounds/source 锚
};

// 封属类别(subtype for SEAL)。含"命中失效"变体(SEAL_ATTRIBUTE_HIT：属性技能照常命中、
// 效果失效、不触发 SKILL_INVALID 无效补偿，区别于普通封属)——用户定的非补偿语义。
enum class SealKind {
    SEAL_ATTACK,        // 狮盔：只封攻击技能
    SEAL_ALL,           // 龙威：封攻击 + 属性技能
    SEAL_ATTRIBUTE,     // 封属：只封属性技能(无效)
    SEAL_ATTRIBUTE_HIT, // 封属·命中失效：只封属性技能，但只"命中效果失效"不触发无效补偿
};

// 一条规则 ticket（纯数据）。
struct RuleTicket {
    // ── 挂载维度（"属于"谁 → 生命周期 + 覆盖键）────────────
    int source_owner = -1;         // 挂载方 (0/1)
    int source_slot = -1;          // 挂载精灵槽（-1=未知/锚当前在场精灵）
    int source_effect_id = -1;     // 来源效果 id（覆盖键的一部分 + 审计）
    EffectScope scope = EffectScope::ON_STAGE;  // ON_STAGE=上场精灵(换宠清)；TEAM=全队保留
    int source_id = 0;             // 授予句柄（revoke / 快照 re-grant 复用用）
    int source_valid_id = 0;       // 来源效果 epoch（注册时 ctx->round_effect_valid_id[source_owner]）。
                                   // 0 = 不参与断回合作废（免疫天然豁免）。随来源效果被断回作废（Q2）。

    // ── 生效维度（"作用于"谁）────────────────────────────
    int target = -1;               // 免疫=被护方自身(source==target)；封属/③层=被封锁方

    // ── 规则身份（覆盖键 = source_owner, source_effect_id, category, subtype）────
    RuleCategory category = RuleCategory::SEAL;
    int subtype = 0;               // IMMUNE→(int)ImmunityType；SEAL→(int)SealKind；HIT_INVALID→(int)HitInvalidMode

    // ── 免疫(IMMUNE)参数 ────────────────────────────────
    uint64_t coverage = 0;         // 时点 bitset（bit=(int)State+1；全置位=闭环）
    uint64_t anomaly_mask = 0;     // ANOMALY 专用：0=全免；否则按 status_id 位
    bool soul = false;             // true=魂免(抗性判定后才查)；false=次免/回合类免疫效果

    // ── 封属(SEAL)参数 ──────────────────────────────────
    bool penetrable = true;        // 可否被"无视攻击免疫"穿透（条件盔/龙威=false）
    std::function<bool(BattleContext*, int attacker, int defender)> condition;  // 条件（nullptr=无条件）

    // ── 消费 / 生命周期 ─────────────────────────────────
    int remaining_counts = 0;      // 次数（>0；响应即减，减到 0 注销）
    int remaining_rounds = 0;      // 回合（>0；tick 每回合减，响应不消耗；断回合清）
    int register_round = 0;        // 窗口/回合起算（与 roundCount 比较过期）

    bool is_round_type() const { return remaining_rounds > 0; }
    bool responds_to(bool is_attribute_skill) const {
        switch (static_cast<SealKind>(subtype)) {
            case SealKind::SEAL_ATTACK:        return !is_attribute_skill;
            case SealKind::SEAL_ATTRIBUTE:
            case SealKind::SEAL_ATTRIBUTE_HIT: return is_attribute_skill;
            case SealKind::SEAL_ALL:           return true;
        }
        return false;
    }
    bool is_hit_invalid_seal() const {
        return category == RuleCategory::SEAL
            && static_cast<SealKind>(subtype) == SealKind::SEAL_ATTRIBUTE_HIT;
    }
};

// SkillInvalidCenter.notify 的三态返回（技能判定门）。
enum class SkillInvalidNotifyResult {
    NONE,        // 无条目响应 → 技能正常可用
    INVALID,     // 有无效类条目响应（盔/威/封属）→ SKILL_INVALID + 补偿
    HIT_INVALID, // 仅命中失效类条目响应 → 照常命中、效果失效、无补偿
};

class RuleCenter {
public:
    RuleCenter() = default;
    RuleCenter(const RuleCenter&) = delete;
    RuleCenter& operator=(const RuleCenter&) = delete;

    int grant_immune(int owner, int type, uint64_t coverage, uint64_t anomaly_mask,
                     int duration_rounds, int register_round, int source_id,
                     bool soul, EffectScope scope, int source_slot,
                     int counts = 0) {
        if (owner < 0 || owner > 1) {
            return -1;
        }
        // ① 复用句柄（快照 re-grant 同 source_id 更新，保持旧 grant_immunity 语义）。
        if (source_id > 0) {
            for (RuleTicket& t : all_) {
                if (t.source_id == source_id && t.category == RuleCategory::IMMUNE) {
                    t.register_round = register_round;
                    t.remaining_rounds = duration_rounds;
                    t.remaining_counts = counts;   // 次数型免疫（>0：免下N次，is_immune 命中后 consume_immune 扣）
                    t.coverage = coverage;
                    t.anomaly_mask = anomaly_mask;
                    t.soul = soul;
                    t.scope = scope;
                    t.source_slot = source_slot;
                    return t.source_id;
                }
            }
        }
        // ② 覆盖键（同 source_owner + subtype=type）刷新：同 type 覆盖、不同 type 各占一条（903）。
        for (RuleTicket& t : all_) {
            if (t.category == RuleCategory::IMMUNE
                && t.source_owner == owner && t.source_effect_id == -1
                && t.subtype == type) {
                t.register_round = register_round;
                t.remaining_rounds = duration_rounds;
                t.remaining_counts = counts;
                t.coverage = coverage;
                t.anomaly_mask = anomaly_mask;
                t.soul = soul;
                t.scope = scope;
                t.source_slot = source_slot;
                return t.source_id;
            }
        }
        // ③ 新增。
        const int sid = (source_id != 0) ? source_id : (++next_source_id_);
        RuleTicket t;
        t.source_owner = owner;
        t.source_slot = source_slot;
        t.source_effect_id = -1;
        t.scope = scope;
        t.source_id = sid;
        t.target = owner;              // 免疫单对象：被护方自身(source==target)
        t.category = RuleCategory::IMMUNE;
        t.subtype = type;
        t.coverage = coverage;
        t.anomaly_mask = anomaly_mask;
        t.remaining_rounds = duration_rounds;  // IMMUNE 视为完整窗口（is_immune/cleanup 过期判断，不 tick）
        t.remaining_counts = counts;           // 次数型免疫（>0）
        t.register_round = register_round;
        t.soul = soul;
        all_.push_back(std::move(t));
        return sid;
    }

    // 查询并消费**次数型**免疫（免疫"下N次"某威胁，如免下1次伤害/异常）。
    // 与 is_immune（纯查询不消费）不同：命中 counts>0 的免疫 → counts-1（0 注销）并返回 true；
    // 命中窗口/永久免疫（counts==0）→ 返回 false 不扣（那些不随施加消耗）。
    // 调用方（deal_damage 免伤 / apply_anomaly 免异常）在 is_immune 命中后调它，若 true 说明是次数型被本次消耗。
    bool consume_immune(int target, int type, uint64_t timing_bit, int current_round,
                        int status_id = 0, int soul_filter = -1) {
        if (target < 0 || target > 1) {
            return false;
        }
        for (auto it = all_.begin(); it != all_.end(); ++it) {
            RuleTicket& t = *it;
            if (t.category != RuleCategory::IMMUNE || t.target != target) continue;
            if (t.subtype != type || t.remaining_counts <= 0) continue;  // 仅次数型
            if (soul_filter >= 0 && (t.soul ? 1 : 0) != soul_filter) continue;
            if (t.remaining_rounds > 0 && current_round - t.register_round >= t.remaining_rounds) {
                continue;
            }
            if (!(t.coverage & timing_bit)) continue;
            if (type == static_cast<int>(ImmunityType::ANOMALY) && t.anomaly_mask != 0
                && status_id >= 0 && !((t.anomaly_mask >> status_id) & 1u)) {
                continue;
            }
            --t.remaining_counts;
            if (t.remaining_counts <= 0) {
                all_.erase(it);
            }
            return true;  // 本次免疫被消耗
        }
        return false;
    }

    void grant_seal(int source_owner, int source_slot, int source_effect_id, int target,
                    SealKind kind, int counts, int rounds, bool penetrable,
                    EffectScope scope = EffectScope::ON_STAGE,
                    std::function<bool(BattleContext*, int, int)> condition = nullptr,
                    int source_valid_id = 0) {
        // 允许次数型(counts>0)或回合型(rounds>0)，至少其一（回合型正常 counts=0）。
        if (source_owner < 0 || source_owner > 1 || target < 0 || target > 1
            || (counts <= 0 && rounds <= 0)) {
            return;
        }
        // 覆盖键 (source_owner, source_effect_id, SEAL, kind) 刷新，不追加。
        for (RuleTicket& t : all_) {
            if (t.category == RuleCategory::SEAL
                && t.source_owner == source_owner && t.source_effect_id == source_effect_id
                && t.subtype == static_cast<int>(kind)) {
                t.target = target;
                t.source_slot = source_slot;
                t.scope = scope;
                t.remaining_counts = counts;
                t.remaining_rounds = rounds;
                t.penetrable = penetrable;
                t.condition = std::move(condition);
                t.source_valid_id = source_valid_id;  // 来源效果被断→随断回作废
                recount();
                return;
            }
        }
        RuleTicket t;
        t.source_owner = source_owner;
        t.source_slot = source_slot;
        t.source_effect_id = source_effect_id;
        t.scope = scope;
        t.target = target;
        t.category = RuleCategory::SEAL;
        t.subtype = static_cast<int>(kind);
        t.penetrable = penetrable;
        t.remaining_counts = counts;
        t.remaining_rounds = rounds;
        t.condition = std::move(condition);
        t.source_valid_id = source_valid_id;
        all_.push_back(std::move(t));
        recount();
    }

    void grant_hit_invalid(int target, int source_slot, int source_effect_id, int mode,
                           int count, EffectScope scope = EffectScope::ON_STAGE,
                           int source_valid_id = 0) {
        if (target < 0 || target > 1 || count <= 0) {
            return;
        }
        for (RuleTicket& t : all_) {
            if (t.category == RuleCategory::HIT_INVALID
                && t.source_owner == target && t.source_effect_id == source_effect_id
                && t.subtype == mode) {
                t.source_slot = source_slot;
                t.scope = scope;
                t.remaining_counts = count;
                t.source_valid_id = source_valid_id;
                return;
            }
        }
        RuleTicket t;
        t.source_owner = target;      // ③层挂"被失效方"(防御方)一侧，作 source 锚
        t.source_slot = source_slot;
        t.source_effect_id = source_effect_id;
        t.scope = scope;
        t.target = target;
        t.category = RuleCategory::HIT_INVALID;
        t.subtype = mode;
        t.remaining_counts = count;
        t.source_valid_id = source_valid_id;
        all_.push_back(std::move(t));
    }

    void revoke(int source_id) {
        std::erase_if(all_, [source_id](const RuleTicket& t) { return t.source_id == source_id; });
    }

    bool is_immune(int target, int type, uint64_t timing_bit, int current_round,
                   int status_id = 0, int soul_filter = -1) const {
        if (target < 0 || target > 1) {
            return false;
        }
        for (const RuleTicket& t : all_) {
            if (t.category != RuleCategory::IMMUNE || t.target != target) continue;
            if (t.subtype != type) continue;
            if (soul_filter >= 0 && (t.soul ? 1 : 0) != soul_filter) continue;
            if (t.remaining_rounds > 0 && current_round - t.register_round >= t.remaining_rounds) {
                continue;  // 窗口已过
            }
            if (!(t.coverage & timing_bit)) continue;
            if (type == static_cast<int>(ImmunityType::ANOMALY) && t.anomaly_mask != 0
                && status_id >= 0 && !((t.anomaly_mask >> status_id) & 1u)) {
                continue;
            }
            return true;
        }
        return false;
    }
    bool is_immune_effect(int target, int type, uint64_t timing_bit,
                          int current_round, int status_id = 0) const {
        return is_immune(target, type, timing_bit, current_round, status_id, /*soul_filter=*/0);
    }
    bool is_immune_soul(int target, int type, uint64_t timing_bit,
                        int current_round, int status_id = 0) const {
        return is_immune(target, type, timing_bit, current_round, status_id, /*soul_filter=*/1);
    }

    SkillInvalidNotifyResult notify(BattleContext* ctx, int user, bool is_attribute_skill,
                                    int power, bool ignore_attack_immunity) {
        if (user < 0 || user > 1) {
            return SkillInvalidNotifyResult::NONE;
        }
        bool any_invalid = false;
        bool any_hit_invalid = false;
        for (auto it = all_.begin(); it != all_.end();) {
            RuleTicket& t = *it;
            if (t.category != RuleCategory::SEAL || t.target != user) {
                ++it;
                continue;
            }
            if (!t.responds_to(is_attribute_skill)) {
                ++it;
                continue;
            }
            if (t.condition && !t.condition(ctx, user, t.target)) {
                ++it;
                continue;
            }
            if (t.penetrable && ignore_attack_immunity) {
                ++it;
                continue;
            }
            if (t.is_hit_invalid_seal()) {
                any_hit_invalid = true;
            } else {
                any_invalid = true;
            }
            if (t.remaining_counts > 0) {
                --t.remaining_counts;
                if (t.remaining_counts <= 0) {
                    it = all_.erase(it);
                    continue;
                }
            }
            ++it;  // 回合类响应但不消耗
        }
        if (any_invalid) {
            return SkillInvalidNotifyResult::INVALID;
        }
        if (any_hit_invalid) {
            return SkillInvalidNotifyResult::HIT_INVALID;
        }
        return SkillInvalidNotifyResult::NONE;
    }

    // ── 回弹（免疫命中时反弹给施放方）──────────────────
    void grant_reflect(int source_owner, int source_slot, int source_effect_id, int target,
                       int rounds, EffectScope scope = EffectScope::ON_STAGE,
                       int source_valid_id = 0) {
        if (source_owner < 0 || source_owner > 1 || target < 0 || target > 1) {
            return;
        }
        RuleTicket t;
        t.source_owner = source_owner;
        t.source_slot = source_slot;
        t.source_effect_id = source_effect_id;
        t.scope = scope;
        t.target = target;
        t.category = RuleCategory::REFLECT;
        t.remaining_rounds = rounds;   // 回弹目前仅回合类（0=本回合持续）；counts 可后续扩
        t.register_round = 0;
        t.source_valid_id = source_valid_id;
        all_.push_back(std::move(t));
        recount();
    }
    bool has_reflect(int target) const {
        if (target < 0 || target > 1) {
            return false;
        }
        for (const RuleTicket& t : all_) {
            if (t.category == RuleCategory::REFLECT && t.target == target) {
                return true;
            }
        }
        return false;
    }

    // ── 断回合作废（Q2）：作废"来源 ON_STAGE 效果已被断"的非免疫规则────
    // source_valid_id 注册时记 round_effect_valid_id[source_owner]；断回回合(++ epoch)后，
    // 来源效果被作废，其授予的非免疫规则一并作废。IMMUNE 恒 0 天然豁免（不断回合清）。
    void invalidate_stale(int source_owner, int current_epoch) {
        if (source_owner < 0 || source_owner > 1) {
            return;
        }
        // 只作废旧**回合类**规则（与 clear_round_type"断回合操作回合类"一致，次数型封属不随断回清）。
        // 由来源 ON_STAGE 回合类效果授予时随其 epoch 作废；免疫恒 0 天然豁免。
        std::erase_if(all_, [source_owner, current_epoch](RuleTicket& t) {
            if (t.source_owner != source_owner || t.scope != EffectScope::ON_STAGE) {
                return false;  // TEAM / 非本方：豁免
            }
            if (t.category == RuleCategory::IMMUNE || !t.is_round_type()) {
                return false;  // 免疫天然不可被断；非回合类规则不随断回清
            }
            return t.source_valid_id > 0 && t.source_valid_id < current_epoch;
        });
        recount();
    }

    std::optional<int> consume_hit_invalid(int defender) {
        if (defender < 0 || defender > 1) {
            return std::nullopt;
        }
        for (auto it = all_.begin(); it != all_.end(); ++it) {
            RuleTicket& t = *it;
            if (t.category != RuleCategory::HIT_INVALID || t.target != defender) {
                continue;
            }
            if (t.remaining_counts > 0) {
                const int mode = t.subtype;
                --t.remaining_counts;
                if (t.remaining_counts <= 0) {
                    all_.erase(it);
                }
                return mode;
            }
        }
        return std::nullopt;
    }

    void clear_on_stage(int source_owner, int source_slot) {
        if (source_owner < 0 || source_owner > 1) {
            return;
        }
        // 免疫(source_slot=-1)整方清；封属/③层按挂载槽清并保留同队其它槽规则；TEAM 保留。
        std::erase_if(all_, [source_owner, source_slot](const RuleTicket& t) {
            if (t.source_owner != source_owner) return false;
            if (t.scope != EffectScope::ON_STAGE) return false;
            return source_slot == -1 || t.source_slot == -1 || t.source_slot == source_slot;
        });
        recount();
    }

    void clear_round_type(int source) {
        if (source < 0 || source > 1) {
            return;
        }
        std::erase_if(all_, [source](const RuleTicket& t) {
            return t.source_owner == source
                && t.category != RuleCategory::IMMUNE && t.is_round_type();
        });
        recount();
    }

    void tick_rounds() {
        std::erase_if(all_, [](RuleTicket& t) {
            if (t.category == RuleCategory::IMMUNE || !t.is_round_type()) {
                return false;
            }
            --t.remaining_rounds;
            return t.remaining_rounds <= 0;
        });
        recount();
    }

    void cleanup(int current_round) {
        std::erase_if(all_, [current_round](const RuleTicket& t) {
            return t.category == RuleCategory::IMMUNE
                && t.remaining_rounds > 0
                && current_round - t.register_round >= t.remaining_rounds;
        });
        recount();
    }

    void clear_all() {
        all_.clear();
        next_source_id_ = 1;
        recount();
    }

    bool empty() const { return all_.empty(); }
    // ③层命中失效在 target 侧剩余次数和（0 = 已消费/无）。抛只读测试/审计用。
    int hit_invalid_remaining(int target) const {
        int sum = 0;
        for (const RuleTicket& t : all_) {
            if (t.category == RuleCategory::HIT_INVALID && t.target == target) {
                sum += t.remaining_counts;
            }
        }
        return sum;
    }
    // O(1)：回合类非免疫规则计数（断回合"有无可断物"门 + 断回作废查询，学 timed_bucket active_round_count_）。
    bool has_round_type(int source) const {
        return source >= 0 && source <= 1 && round_count_[source] > 0;
    }
    std::size_t size() const { return all_.size(); }
    const std::vector<RuleTicket>& entries() const { return all_; }

private:
    // 回合类（非 IMMUNE，is_round_type）规则计数，per-owner。见 has_round_type。
    void recount() {
        round_count_[0] = round_count_[1] = 0;
        for (const RuleTicket& t : all_) {
            if (t.category != RuleCategory::IMMUNE && t.is_round_type()) {
                ++round_count_[t.source_owner];
            }
        }
    }
    int next_source_id_ = 1;
    int round_count_[2]{0, 0};
    std::vector<RuleTicket> all_;
};

#endif // RULE_CENTER_H