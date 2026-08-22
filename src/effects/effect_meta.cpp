#include <effects/effect_meta.h>

#include <cctype>
#include <db/official_data_repository.h>

// 效果元数据目录：effect_info 模板的启发式分类 + 人工覆盖表。
//
// 规则设计依据（2026-08-22 用全库 2340 条模板验证过精度）：
// - StatusInflict 主规则 `\{(\d+)\}%令(对手|自身)\{`："{n}%" 概率 + "令对手/自身"
//   后紧跟占位符（异常状态 id）。命中 149 条，抽样全部为异常附加主子句；
//   "未触发则恢复体力" 类兜底子句不影响判定（主子句仍是异常附加）。
// - 其余类别按关键词优先级顺次判定（StatusInflict 最先，避免兜底子句误分流）。

namespace {

// 人工覆盖表：规则错分时在此修正（优先级最高），不改规则引擎。
// key = effect_info.id；未列出的字段沿用规则结果。
struct MetaOverride {
    int effect_id;
    EffectCategory category;
    int chance_value;    // -1 = 不覆盖
    int chance_arg;      // -1 = 不覆盖
    const char* note;
};

constexpr MetaOverride kMetaOverrides[] = {
    // 843 先制（"下{0}回合令自身所有技能先制+{1}"）——规则可命中，显式列出作文档
    {843, EffectCategory::Priority, -1, -1, "override: 基值先制"},
    // 2533 异常附加（"{0}%令对手{1}，未触发则恢复PP"）——规则可命中，作回归锚点
    {2533, EffectCategory::StatusInflict, -1, 0, "override: 概率异常附加"},
    // 697/699 穿透凭证（"无视伤害限制效果"/"无视攻击免疫效果"）——规则可命中，显式列出作文档
    {697, EffectCategory::Penetration, -1, -1, "override: 无视伤害限制（穿透）"},
    {699, EffectCategory::Penetration, -1, -1, "override: 无视攻击免疫（穿透）"},
};

// 可否决性覆盖表：③层命中效果失效时是否跳过该效果。
// 默认 NullifyPolicy.hit_effect_invalidatable=true；此处列出的效果例外（固有效果不可否决，文档第六节）。
struct NullifyOverride {
    int effect_id;
    bool hit_effect_invalidatable;  // false = ③层失效时仍注册（固有效果）
    const char* note;
};

constexpr NullifyOverride kNullifyOverrides[] = {
    // 1002 条件先制（"若对手处于异常状态则先制+1"）：固有效果，③层仍生效
    {1002, false, "override: 条件先制不可否决"},
};

bool contains_substring(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

// 在模板中查找 "{n}%" 概率占位符，返回参数下标；找不到返回 -1。
// 多个占位符取首个（官方模板概率参数均在前部）。
int find_percent_arg_index(const std::string& info) {
    for (std::size_t i = 0; i + 3 < info.size() + 1 && i < info.size(); ++i) {
        if (info[i] != '{') {
            continue;
        }
        const std::size_t close = info.find('}', i);
        if (close == std::string::npos || close == i + 1) {
            continue;
        }
        bool digits = true;
        for (std::size_t j = i + 1; j < close; ++j) {
            if (!std::isdigit(static_cast<unsigned char>(info[j]))) {
                digits = false;
                break;
            }
        }
        if (digits && close + 1 < info.size() && info[close + 1] == '%') {
            return std::stoi(info.substr(i + 1, close - i - 1));
        }
    }
    return -1;
}

// StatusInflict 主规则："{n}%令对手{" / "{n}%令自身{"（概率 + 令 + 紧跟状态占位符）。
// 返回概率参数下标；不匹配返回 -1。
int match_status_inflict_pattern(const std::string& info) {
    static const char* kLeads[] = {"%令对手{", "%令自身{"};
    for (const char* lead : kLeads) {
        std::size_t pos = info.find(lead);
        while (pos != std::string::npos) {
            // 向左找紧邻的 "{n}"（pos 前应是 "}"，再往前是数字段与 "{"）
            if (pos >= 2 && info[pos - 1] == '}') {
                std::size_t open = info.rfind('{', pos - 2);
                if (open != std::string::npos && open + 1 < pos - 1) {
                    bool digits = true;
                    for (std::size_t j = open + 1; j < pos - 1; ++j) {
                        if (!std::isdigit(static_cast<unsigned char>(info[j]))) {
                            digits = false;
                            break;
                        }
                    }
                    if (digits) {
                        return std::stoi(info.substr(open + 1, pos - 2 - open));
                    }
                }
            }
            pos = info.find(lead, pos + 1);
        }
    }
    return -1;
}

// 关键词分类（在 StatusInflict 主规则之后调用）。
EffectCategory classify_by_keywords(const std::string& info) {
    // 穿透凭证（697"无视伤害限制效果"/699"无视攻击免疫效果"）。
    // 放在最前避免被后续 DamageModify/Other 吞掉；泛化兜底"无视…免疫"。
    if (contains_substring(info, "无视伤害限制") || contains_substring(info, "无视攻击免疫")
        || (contains_substring(info, "无视") && contains_substring(info, "免疫"))) {
        return EffectCategory::Penetration;
    }
    if (contains_substring(info, "先制")) {
        return EffectCategory::Priority;
    }
    if ((contains_substring(info, "改变") || contains_substring(info, "提升") || contains_substring(info, "降低"))
        && contains_substring(info, "等级")) {
        return EffectCategory::StatChange;
    }
    if (contains_substring(info, "护盾") || contains_substring(info, "护罩")) {
        return EffectCategory::Shield;
    }
    if (contains_substring(info, "吸取")) {
        return EffectCategory::Drain;
    }
    if (contains_substring(info, "恢复")) {
        return EffectCategory::Heal;
    }
    if (contains_substring(info, "伤害")
        && (contains_substring(info, "提升") || contains_substring(info, "减少")
            || contains_substring(info, "增加"))) {
        return EffectCategory::DamageModify;
    }
    return EffectCategory::Other;
}

}  // namespace

EffectMetaCatalog& EffectMetaCatalog::instance() {
    static EffectMetaCatalog catalog;
    return catalog;
}

bool EffectMetaCatalog::build() {
    metas_.clear();
    built_ = false;

    const official_data::OfficialDataStore& store = official_data::OfficialDataStore::instance();
    if (!store.ready()) {
        return false;  // 数据库未初始化，保持未构建状态
    }

    const std::vector<official_data::EffectTemplateRecord> templates =
        store.repository().load_all_effect_templates();
    for (const auto& template_record : templates) {
        EffectMeta meta;
        const std::string& info = template_record.info;

        const int status_chance_arg = match_status_inflict_pattern(info);
        if (status_chance_arg >= 0) {
            meta.category = EffectCategory::StatusInflict;
            meta.chance.arg_index = status_chance_arg;
            meta.source_note = "rule: {n}%令对手/自身{";
        } else {
            meta.category = classify_by_keywords(info);
            const int percent_arg = find_percent_arg_index(info);
            if (percent_arg >= 0) {
                meta.chance.arg_index = percent_arg;
            }
            meta.source_note = "rule: keywords";
        }

        metas_.emplace(template_record.id, std::move(meta));
    }

    // 覆盖表最后套用（优先级最高）
    for (const MetaOverride& override_entry : kMetaOverrides) {
        EffectMeta& meta = metas_[override_entry.effect_id];
        meta.category = override_entry.category;
        if (override_entry.chance_value >= 0) {
            meta.chance.value = override_entry.chance_value;
        }
        if (override_entry.chance_arg >= 0) {
            meta.chance.arg_index = override_entry.chance_arg;
        }
        meta.source_note = override_entry.note;
    }

    // 可否决性覆盖（③层命中效果失效时逐效果标签）
    for (const NullifyOverride& override_entry : kNullifyOverrides) {
        metas_[override_entry.effect_id].nullify.hit_effect_invalidatable =
            override_entry.hit_effect_invalidatable;
    }

    built_ = !metas_.empty();
    return built_;
}

const EffectMeta* EffectMetaCatalog::find(int effect_id) const {
    const auto it = metas_.find(effect_id);
    return it == metas_.end() ? nullptr : &it->second;
}

bool EffectMetaCatalog::is_status_inflict_at_most(
    int effect_id, const EffectArgs& args, int threshold_pct
) const {
    const EffectMeta* meta = find(effect_id);
    if (!meta || meta->category != EffectCategory::StatusInflict) {
        return false;
    }
    const int chance = meta->chance.resolve(args);
    // 概率未知（-1）不判真：检测类魂印的语义是"概率不高于x%"，
    // 无法确定概率时宁可漏判（返回 false），不误伤对方效果。
    return chance >= 0 && chance <= threshold_pct;
}
