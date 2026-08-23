#ifndef __battleContext_H
#define __battleContext_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <array>
#include <map>
#include <algorithm>
#include <mutex>
#include <initializer_list>

#include <abnormal-system/abnormal-types.h>
#include <entities/seer-robot.h>
#include <fsm/battleWorkspace.h>
#include <effects/continuousEffect.h>
#include <effects/pendingEffect.h>
#include <effects/event_center.h>
#include <effects/immunity_center.h>
#include <effects/damage_pipeline.h>
#include <entities/soul_mark.h>
#include <fsm/state.h>

// Forward declarations
class BattleFsm;
class IControlBlock;

enum class EffectContainer {
    Skill,
    SoulMark,
};


class BattleContext {
public:
    //--- 持久化核心数据 ---
    SeerRobot seerRobot[2];
    int on_stage[2];
    int roundCount;         // 当前回合数（用于效果过期判定）
    unsigned int uuid;
    State currentState;
    BattleFsm* m_fsm;

    //--- 临时工作层（每回合重置）---
    BattleWorkspace ws;

    //--- 当前在场精灵的异常状态结束回合 ---
    // 下标一层是对战方(0/1)，二层是异常状态 id。
    // 存的是”失效回合(exclusive end round)”：
    // currentRound < endRound 视为仍然生效。
    // 这里不放 workspace，因为 workspace 每回合都会 reset。
    std::array<std::array<int, kOfficialAbnormalStatusSlotCount>, 2> abnormal_status_end_round{};

    //--- 回合类效果计数（O(1) 查询”是否有回合类效果”）---
    // 每次 registerEffect 时如果 isRoundEffect()==true 则 +1
    // cleanup_expired_effects / remove_all_round_effects 时扣减
    int active_round_effects[2]{};

    //--- 回合效果版本号（epoch，用于 O(1) 断回合）---
    // 注册回合效果时 effect.valid_id_ = round_effect_valid_id[owner]
    // 断回合时 ++round_effect_valid_id[robotId] 即可使所有旧效果失效
    int round_effect_valid_id[2]{1, 1};  // 从 1 开始，避免和默认初始化的 0 混淆

    //--- 监听器版本号（epoch，用于 O(1) 切换作废监听器）---
    // 注册 watcher 时 watcher.valid_id_ = watcher_valid_id[owner]
    // 切换精灵时 ++watcher_valid_id[owner] 使该方所有 ON_STAGE 监听器失效（补偿不继承给新精灵）
    // 断回合不递增此号 → 被断补偿监听器在断回合后仍然存活（等响应本次断）
    int watcher_valid_id[2]{1, 1};

    //--- 事件通道内核 ---
    // 全 context 唯一的事件中心。原语成功路径末尾 emit，FSM 在 State 桶后 drain 投递。
    // 断回合补偿 = 监听 EVENT_BREAK 的 watcher（经 register_break_callback 注册）。
    EventCenter event_center_;

    //--- 免疫内核 ---
    // 免断/魂免/免伤/免弱/异常免疫统一查询。断回合、异常施加原语在动作前查 is_immune。
    // 免疫源经 grant_immunity / revoke_immunity 管理，独立于效果桶（天然不可被断）。
    ImmunityCenter immunity_center_;

    //--- 伤害修正管线 ---
    // 伤害值在 resolvedDamage 中流经 DamagePhase 节点，各阶段效果读写它。
    // 类别抑制（damage_suppress_mask）按效果类别跳过被抑制方的伤害效果。
    DamagePipeline damage_pipeline_;

    //--- 技能拦截桶（封属性/封攻击）---
    // 拦截效果挂到**被拦截方**的桶里（施放方切换不影响它；被拦截方切换时
    // invalidate_on_stage_effects 清自己桶 → 换宠可洗掉封属性）。
    // 按被拦截方 owner 索引；每条显式带 target（不靠"在哪个桶"推断）。
    // 次数型（remaining>0）命中消费；回合型（remaining_rounds>0）每回合递减、可被断回合。
    struct SkillSeal {
        int target = -1;          // 封锁对象（被拦截方），显式
        int effect_id = -1;       // 来源效果 id：覆盖去重 key（同效果覆盖刷新）
        int remaining = 0;        // 次数型剩余次数（>0 命中消费）
        int remaining_rounds = 0; // 回合型剩余回合（>0 每回合递减；0=次数型）
        bool seal_attribute = false;  // 封锁属性技能（category=4）
        bool seal_attack = false;     // 封锁攻击技能（category=1/2）
        bool penetrable = true;   // 可否被"无视攻击免疫"穿透：false=条件盔/龙威，恒被挡
        int  armor_level = 0;     // 盔等级：0=可穿盔, 1=条件盔, 2=龙威（本轮只存不比较）
    };
    std::vector<SkillSeal> skill_seals[2];  // [被拦截方]

    //--- 命中效果失效桶（③层：命中但效果不注册；白板=伤害归0/保留伤害=伤害照常）---
    // 挂在防御方上：其技能命中时命中效果被失效。mode 见 effect.h HitInvalidMode。
    struct HitEffectInvalid {
        int source_id = -1;
        HitInvalidMode mode = HitInvalidMode::kEffectsOnly;
        int remaining = 0;  // 剩余失效次数
    };
    std::vector<HitEffectInvalid> hit_effect_invalids[2];  // [被失效方]

    //--- 次数型穿透授予（挂在自己身上，"下1次攻击无视伤害限制"类）---
    // 跨回合持久；成功使用攻击技能后统一消费（每槽 remaining-1，0 移除）。
    // 切换精灵/清场时随 invalidate_on_stage_effects / clearAllEffects 一并清理。
    struct PenetrationGrant {
        int owner = -1;
        int remaining = 0;         // 剩余次数
        int level = 1;             // 穿透等级（最小门只用 1）
        bool ignore_attack_immunity = false;  // 699 语义
        bool ignore_damage_limit = false;     // 697 语义
        int source_id = -1;        // 来源（技能/魂印 id）
    };
    std::vector<PenetrationGrant> penetration_grants[2];  // [授予方]

    //--- 魂印条件凭证信号（SET 端：魂印激活时设置，切换/清场清零）---
    bool force_execute_on_pp0[2]{};  // 魂印激活：使用 PP=0 技能时必定命中+强制执行（无为觉者 2260）
    bool ignore_pp[2]{};             // 魂印激活：PP=0 技能仍可选（不受PP限制）
    bool pp_reverse[2]{};            // 魂印激活：使用技能后 PP 反转（当前PP与已损失互换，无为觉者 2260）

    //--- 精灵系别半持久化视图（当前在场精灵有效系别）---
    // 改系别效果（属性反转/龙琰类）写这里，跨回合保留（ws 每回合 reset 会清，故放 context）。
    // bound_slot 记录已绑定的精灵槽：sync_workspace_from_on_stage 在槽变化时从
    // pet.elementalAttributes 重基（开战首回合 bound=-1 自动基线；换宠自动重基），同槽则保留。
    int elf_element_view[2][2]{};              // 当前在场精灵有效系别
    int elf_element_view_bound_slot[2]{-1, -1};  // 已绑定槽（-1=未基线）

    //--- 粉伤抗性（on-stage 作用域，切换/清场清）---
    // 伤害抗性按来源分三种（官方：暴击/固定/百分比），逐型削减对应伤害。
    bool pink_immune[2]{};     // 免疫粉伤（固定/百分比伤害）
    int  fixed_resist_pct[2]{};   // 固定伤害抗性%
    int  percent_resist_pct[2]{}; // 百分比伤害抗性%
    int  crit_resist_pct[2]{};    // 暴击伤害抗性%（削减暴击加成部分）
    int  pink_reduce_pct[2]{}; // 减粉%（百分比免减，固定+百分比通用）
    bool pink_to_true[2]{};    // 粉转真：被免疫/抗性/减粉挡下时改以真实伤害结算

    //--- 反弹/转化异常（on-stage 作用域）---
    bool reflect_anomaly[2]{};                        // 弹控：免疫异常时反弹给施放方（最多反弹 1 次防打乒乓球）
    std::array<std::map<int, int>, 2> anomaly_conversion;  // [目标] 入异常 id → 出异常 id（单跳转换）

    //--- 技能效果执行表 ---
    // 内层用 std::map<uint64_t, ...>：key = (source_id << 32) | effect_id，
    // 同源同 effect 新注册自动覆盖旧（同源去重）；source_id==0 用唯一自增 key 不参与去重。
    std::unordered_map<State, std::array<std::map<uint64_t, std::unique_ptr<ContinuousEffect>>, 2>> skills_effects;

    //--- 魂印效果执行表 ---
    std::unordered_map<State, std::array<std::map<uint64_t, std::unique_ptr<ContinuousEffect>>, 2>> soul_mark_effects;

    //--- 被动效果表 ---
    std::array<std::map<int, ContinuousEffect*>, 2> passiveEffects;

    //--- 待注册效果 / 未来触发表 ---
    std::unordered_map<State, std::array<std::vector<std::unique_ptr<PendingEffect>>, 2>> pending_effects;

    //--- 网络缓冲 ---
    std::vector<char> m_buffer;
    bool is_empty = true;

    //--- 运行串行化 ---
    // 同一个 battle context 只能串行执行 run，避免多线程 post 导致并发状态破坏。
    std::mutex run_mutex;

    //--- 调试模式 ---
    bool debug_step_mode = false;                       // 单步执行模式：每执行一个状态就停
    std::unordered_set<int> breakpoints;                // 断点状态集合（存 int 便于序列化）

    //--- 控制块 ---
    IControlBlock* control_block_;
    int current_player_id_;  // 当前等待输入的玩家

    //--- 操作日志 ---
    std::string operation_log_;
    std::string moves_log_;

    //--- 每回合输入收集 ---
    // 训练模式下同一连接会顺序提交双方操作，先收齐再进入战斗链。
    std::array<bool, 2> operation_collected{};

    //--- 错误处理 ---
    static constexpr int MAX_ATTEMPTS = 3;
    int failed_attempts;

    //--- 便利引用 ---
    int (&roundChoice)[2][2];
    int (&lastActionType)[2];
    int (&lastActionIndex)[2];
    PreemptiveRight& preemptive_right;
    int (&damage_reduce_add)[2][4];
    int (&damage_reduce_mul)[2][4];
    DamageSnapshot& pendingDamage;
    DamageSnapshot& resolvedDamage;

    //--- 构造函数 ---
    BattleContext(IControlBlock* control_block, const SeerRobot robots[]);
    BattleContext() = delete;
    BattleContext(const BattleContext&) = delete;
    ~BattleContext();

    //--- 基础方法 ---
    void init_battle();

    //--- 状态控制 ---
    bool need_input() const;
    void generateState();
    void back_to_last_state();

    //--- Workspace ---
    void resetWorkspace() { ws.reset(); }

    //--- 当前在场异常状态 ---
    void clear_on_stage_abnormal_statuses(int robotId);
    void clear_all_on_stage_abnormal_statuses();
    void set_abnormal_status_end_round(int robotId, int statusId, int endRound);
    void apply_abnormal_status_for_rounds(int robotId, int statusId, int durationRounds);
    int get_abnormal_status_end_round(int robotId, int statusId) const;
    bool has_active_abnormal_status(int robotId, int statusId) const;

    //--- 效果注册 ---
    void registerEffect(State trigger, int owner, std::unique_ptr<ContinuousEffect> effect,
                        EffectContainer container = EffectContainer::Skill);
    void registerPendingEffect(State observeState, int owner, std::unique_ptr<PendingEffect> effect);
    void registerPassiveEffect(int owner, int effectId, ContinuousEffect* effect);

    //--- 效果执行 ---
    void execute_pending_effects(int robotId, State state);
    void execute_registered_actions(int robotId, State state);

    //--- 效果查询 ---
    template<int EffectId>
    bool hasEffect(State trigger, int owner) const;

    template<int EffectId>
    bool consumeEffect(State trigger, int opponent);

    //--- 回合结束 ---
    void advanceRound() { ++roundCount; }

    //--- 次数型穿透授予 API ---
    // 授予"下N次攻击无视免疫/伤害限制"（如魂印/技能给的一次性穿透）。跨回合持久，
    // 成功使用攻击技能后由 consume_penetration_grants_after_attack 统一消费。
    // 内联实现：插件动态库不链接 sim_core，需头文件可见（仿 843 先例）。
    int grant_penetration(int owner, int level, bool ignore_attack_immunity,
                          bool ignore_damage_limit, int count, int source_id = -1) {
        if (owner < 0 || owner > 1 || count <= 0) {
            return -1;
        }
        penetration_grants[owner].push_back(
            PenetrationGrant{owner, count, level, ignore_attack_immunity,
                             ignore_damage_limit, source_id});
        return static_cast<int>(penetration_grants[owner].size()) - 1;
    }

    // 成功使用攻击技能后统一消费：每槽 remaining-1，0 移除。
    // 即使对手无阻挡也消费（"下一次攻击"语义），miss/sealed 不消费（调用方保证）。
    void consume_penetration_grants_after_attack(int owner) {
        if (owner < 0 || owner > 1) {
            return;
        }
        auto& grants = penetration_grants[owner];
        for (auto it = grants.begin(); it != grants.end();) {
            --it->remaining;
            if (it->remaining <= 0) {
                it = grants.erase(it);
            } else {
                ++it;
            }
        }
    }

    //--- 切换/清场 ---
    // 使某方所有 ON_STAGE 效果惰性失效（切换精灵/清场用）。
    // 通过递增版本号实现：旧 ON_STAGE 效果 valid_id_ 不匹配 → 执行时跳过 + cleanup 移除。
    // TEAM 效果不受影响（scope_ == TEAM 不检查 valid_id_）。
    // 切换同时递增监听器版本号 → 该方 ON_STAGE 监听器（含被断补偿）一并作废，
    // 不继承给下一个登场精灵。
    void invalidate_on_stage_effects(int owner) {
        ++round_effect_valid_id[owner];
        ++watcher_valid_id[owner];
        active_round_effects[owner] = 0;  // ON_STAGE 回合效果已全部失效，清计数器
        penetration_grants[owner].clear();  // 次数型穿透授予不继承给新精灵
        hit_effect_invalids[owner].clear();  // 命中效果失效（③层）不继承给新精灵
        force_execute_on_pp0[owner] = false;  // 魂印条件信号不继承给新精灵（待新魂印重新激活）
        ignore_pp[owner] = false;
        pp_reverse[owner] = false;
        skill_seals[owner].clear();  // 拦截挂在被拦截方桶：换宠洗掉自己身上的封属性
        pink_immune[owner] = false;          // 粉伤抗性不继承给新精灵
        fixed_resist_pct[owner] = 0;
        percent_resist_pct[owner] = 0;
        crit_resist_pct[owner] = 0;
        pink_reduce_pct[owner] = 0;
        pink_to_true[owner] = false;
        reflect_anomaly[owner] = false;      // 弹控不继承
        anomaly_conversion[owner].clear();   // 异常转化规则不继承
        elf_element_view_bound_slot[owner] = -1;  // 新精灵下次 sync 重基系别
    }

    //--- 清空效果 ---
    void clearAllEffects() {
        skills_effects.clear();
        soul_mark_effects.clear();
        pending_effects.clear();
        penetration_grants[0].clear();
        penetration_grants[1].clear();
        hit_effect_invalids[0].clear();
        hit_effect_invalids[1].clear();
        force_execute_on_pp0[0] = false;
        force_execute_on_pp0[1] = false;
        ignore_pp[0] = false;
        ignore_pp[1] = false;
        pp_reverse[0] = false;
        pp_reverse[1] = false;
        pink_immune[0] = pink_immune[1] = false;
        fixed_resist_pct[0] = fixed_resist_pct[1] = 0;
        percent_resist_pct[0] = percent_resist_pct[1] = 0;
        crit_resist_pct[0] = crit_resist_pct[1] = 0;
        pink_reduce_pct[0] = pink_reduce_pct[1] = 0;
        pink_to_true[0] = pink_to_true[1] = false;
        reflect_anomaly[0] = reflect_anomaly[1] = false;
        anomaly_conversion[0].clear();
        anomaly_conversion[1].clear();
        elf_element_view_bound_slot[0] = elf_element_view_bound_slot[1] = -1;
        for (int p = 0; p < 2; ++p) {
            for (ElfPet& pet : seerRobot[p].elfPets) {
                pet.soulmark_storage.clear();  // 魂印持久槽：战斗结束/清场清空
            }
        }
        active_round_effects[0] = 0;
        active_round_effects[1] = 0;
        event_center_.clear_all();
        immunity_center_.clear_all();
        damage_pipeline_.clear();
        install_default_damage_reduction();
    }

    //--- 回合类效果管理 ---

    /**
     * 清理所有已过期的回合类效果
     *
     * 在 BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS 统一调用，
     * 遍历所有效果桶，移除 isExpired() == true 的效果。
     * 同时更新 active_round_effects 计数器。
     */
    void cleanup_expired_effects();

    /**
     * 机械无效化目标全部回合类效果（内核操作，O(1)）。
     *
     * 递增 round_effect_valid_id[robotId] 使所有旧效果失效并归零计数器。
     * 免疫检查、结果判定、EVENT_BREAK 事件由原语 break_round_effects
     * （include/primitives/battle_primitives.h）负责。
     * 注意：Mark ID 0（异常免疫标记）不受断回合影响，它不在效果桶中。
     */
    void invalidate_all_round_effects(int robotId);

    /**
     * 注册被断回合补偿回调 —— 事件通道兼容层
     *
     * 等价于向事件中心注册一个监听 EVENT_BREAK 的 watcher：
     * - 只在"自己（owner）被断"（event.target == owner）时触发；
     * - once 语义：触发一次后自动移除（与被断补偿只触发一次一致）；
     * - 窗口 = duration_rounds，与关联回合效果一致。
     *
     * @param owner           注册方（被断回合时的补偿触发方）
     * @param duration_rounds 持续回合（0 = 永久）
     * @param fn              补偿逻辑
     * @return watcher_id    用于 remove_break_callback 手动注销
     */
    int register_break_callback(int owner, int duration_rounds,
                                std::function<void(BattleContext*)> fn);

    /**
     * 手动注销被断回合补偿回调
     */
    void remove_break_callback(int owner, int callback_id);

    //--- 免疫内核便利方法 ---

    /**
     * 授予免疫。coverage 用 state_coverage_bit / coverage_all / coverage_union 构造。
     * @param source_id 0 = 新建; >0 = 复用更新（快照程序每回合 re-grant 同句柄）
     * @return source_id（revoke 用）
     */
    int grant_immunity(int owner, ImmunityType type, uint64_t coverage,
                       uint64_t anomaly_mask = 0, int duration_rounds = 0, int source_id = 0,
                       bool soul_immunity = false) {
        return immunity_center_.grant(owner, type, coverage, anomaly_mask,
                                      duration_rounds, roundCount, source_id, soul_immunity);
    }

    void revoke_immunity(int owner, int source_id) {
        immunity_center_.revoke(owner, source_id);
    }

    /**
     * is_immune - 原语在动作前查询"目标在此时点是否免疫该威胁"。
     * @param status_id ANOMALY 类型专用：被查询的异常状态 id；其余类型忽略
     */
    bool is_immune(int owner, ImmunityType type, State timing, int status_id = 0) const {
        return immunity_center_.is_immune(owner, type, state_coverage_bit(timing), roundCount, status_id);
    }

    // 细分查询：次免/回合类免疫（soul=false）先于抗性判定；魂免（soul=true）在抗性失败后才查。
    bool is_immune_effect(int owner, ImmunityType type, State timing, int status_id = 0) const {
        return immunity_center_.is_immune_effect(owner, type, state_coverage_bit(timing), roundCount, status_id);
    }
    bool is_immune_soul(int owner, ImmunityType type, State timing, int status_id = 0) const {
        return immunity_center_.is_immune_soul(owner, type, state_coverage_bit(timing), roundCount, status_id);
    }

    //--- 伤害管线便利方法 ---

    /**
     * 注册一个伤害修正效果到指定阶段。
     * fn 通过 ctx->resolvedDamage 读取/修改当前伤害值（resolvedDamage.final）。
     * 被抑制的类别（所属方 damage_suppress_mask）在 walk 时自动跳过。
     */
    void register_damage_effect(DamagePhase phase, int owner, DamageEffectCategory category,
                                std::function<void(BattleContext*, int)> fn) {
        damage_pipeline_.register_effect(phase, owner, category, std::move(fn));
    }

    /**
     * 安装默认减伤（REDUCE 阶段，MITIGATE 类别）。
     * 把工作区 damage_reduce_add/mul（4 槽减伤）从同步 inline 结算迁入管线，
     * 从而可被 damage_suppress_mask 按类别抑制（如沧岚"挡伤失效"）并可与其他 REDUCE 效果排序。
     * 每次攻击伤害结算前确保已安装（init_battle / clearAllEffects 后调用）。
     */
    void install_default_damage_reduction();

    /**
     * O(1) 查询目标是否还有回合类效果
     */
    bool has_round_effects(int robotId) const {
        return robotId >= 0 && robotId <= 1 && active_round_effects[robotId] > 0;
    }

    //--- 日志 ---
    void log_operation(const std::string& op) {
#ifdef BATTLE_OP_LOGGING
        operation_log_ += op;
        operation_log_ += "|";
#endif
    }

    std::string get_operation_log() const { return operation_log_; }
    void clear_operation_log() { operation_log_.clear(); }

      void reset_operation_collection() { operation_collected = {false, false}; }
      bool has_collected_both_operations() const {
          return std::all_of(operation_collected.begin(), operation_collected.end(), [](bool v) { return v; });
      }

    //--- 便利方法 ---
    ElfPet& getPet(int robotId) { return seerRobot[robotId].elfPets[on_stage[robotId]]; }
    int opponent(int robotId) const { return 1 - robotId; }

    //--- 设置当前玩家 ---
    void set_current_player(int player_id) { current_player_id_ = player_id; }

    //--- 状态同步 ---
    std::string getStateJson() const;
    std::string getFullStateJson() const;
};

// 显式实例化模板
extern template bool BattleContext::hasEffect<0>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<1>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<2>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<3>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<4>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<10>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<11>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<12>(State trigger, int owner) const;

extern template bool BattleContext::consumeEffect<0>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<1>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<2>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<3>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<4>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<10>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<11>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<12>(State trigger, int opponent);

#endif
