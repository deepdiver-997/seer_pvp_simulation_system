#ifndef EFFECT_META_H
#define EFFECT_META_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <effects/continuousEffect.h>  // EffectWindowKind
#include <effects/effect.h>

// 效果语义元数据。
//
// 动机：现代魂印大量是"元效果"——检测另一个效果的类别与参数
// （帝皇铠甲/梅赫维特："对方执行概率不高于50%的异常状态附加效果时…"）。
// Effect 本体只有函数指针+参数，无语义自描述；本层为每个 effect id
// 提供可查询的类别/概率/class/可否决策略。
//
// 数据来源：effect_info 模板文本的启发式分类 + 少量人工覆盖表
// （kMetaOverrides）。分类是启发式的，错分靠覆盖表修，不追求一次全对。
//
// 注意：EffectClass / NullifyPolicy 当前不做自动推导（官方 effect_icon.kind
// 的数值语义尚未破解），默认 Undefined / 全 true，待逐机制数据标注。

enum class EffectCategory {
    Unknown,       // 未分类
    StatusInflict, // 异常状态附加（官方"异常状态附加效果"最小子类，effect_des 337）
    StatChange,    // 能力等级变动
    DamageModify,  // 伤害提升/减少
    Heal,          // 恢复
    Priority,      // 先制修改
    Shield,        // 护盾/护罩
    Drain,         // 吸取
    Penetration,   // 穿透凭证（697"无视伤害限制"/699"无视攻击免疫"，请求携带而非可执行效果）
    Combo,         // 连击（"1回合做 x~y 次攻击"一族；官方把"n次连击"列为变威力效果）
    Other,         // 已分析但不属于上述类别
};

// 概率语义：模板中 "{n}%" 占位符 → 参数化概率；无占位符的字面概率 → value。
struct EffectChance {
    int value = -1;      // 字面概率（-1 = 无）
    int arg_index = -1;  // EffectArgs.int_args 下标（-1 = 无）

    bool present() const { return value >= 0 || arg_index >= 0; }
    // 解析实际概率：优先取 args[arg_index]，越界/无参数回退 value；均无效返回 -1
    int resolve(const EffectArgs& args) const {
        if (arg_index >= 0 && args.int_args && arg_index < args.int_count) {
            return args.int_args[arg_index];
        }
        return value;
    }
};

// 效果 class：固有（选择期，图标与命中效果不同）vs 命中（需命中才注册）。
enum class EffectClass {
    Undefined,  // 未标注
    Inherent,   // 固有效果
    Hit,        // 命中效果
};

// 可否决策略（逐效果属性，不能从类别推导——星皇之怒固有但被命中效果失效阻断，
// 条件先制固有且不受影响，见《技能判定流程与无效效果体系》第六节）。
struct NullifyPolicy {
    bool hit_effect_invalidatable = true;  // 命中效果失效时是否跳过本效果
    bool invalid_compensatable = true;     // 技能无效补偿是否可被命中效果失效阻断
};

struct EffectMeta {
    EffectCategory category = EffectCategory::Unknown;
    EffectChance chance;
    EffectClass cls = EffectClass::Undefined;
    NullifyPolicy nullify;
    // 回合数窗口家族（"N回合内" vs "下N回合"）——来自认证数据层
    // custom_effect_overrides(override_type='window')，未声明默认 InRounds。
    EffectWindowKind window = EffectWindowKind::InRounds;
    std::string source_note;  // 分类依据（规则名/override），调试用
};

// 全局效果元数据目录（effect id → EffectMeta）。
// id 空间与 Effect.id / 插件注册 id / effect_info.id 对齐。
class EffectMetaCatalog {
public:
    static EffectMetaCatalog& instance();

    // 从官方库构建（OfficialDataStore 就绪后调用；幂等，重复调用重建）。
    bool build();
    bool ready() const { return built_; }

    // 查询元数据；未收录返回 nullptr（调用方需判空）
    const EffectMeta* find(int effect_id) const;

    // 概率阈值谓词：effect 属异常附加类，且实际概率（args 解析）≤ threshold_pct。
    // 帝皇铠甲"木之伸曲"/梅赫维特"曝"的检测原语。
    // 概率未知（未分类/无参数）返回 false（宁缺勿滥，不误伤）。
    bool is_status_inflict_at_most(int effect_id, const EffectArgs& args, int threshold_pct) const;

private:
    EffectMetaCatalog() = default;

    bool built_ = false;
    std::unordered_map<int, EffectMeta> metas_;
};

#endif // EFFECT_META_H