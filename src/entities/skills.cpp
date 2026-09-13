#include <entities/skills.h>

#include <cstdlib>

#include <entities/elf-pet.h>

#include <algorithm>

#include <effects/effect_meta.h>
#include <effects/effect_unit_parser.h>
#include <effects/effect_unit_loader.h>
#include <fsm/battleContext.h>

namespace {

SkillType map_skill_type(int category) {
    switch (category) {
        case 1: return SkillType::Physical;
        case 2: return SkillType::Special;
        default: return SkillType::Attribute;
    }
}

EffectArgs build_effect_args_for_skill(const official_data::SkillEffectRecord& record) {
    std::vector<int> args;
    args.reserve(static_cast<std::size_t>(record.arg_count) + 2);
    args.push_back(0);
    args.push_back(1);
    args.insert(args.end(), record.args.begin(), record.args.end());
    return EffectArgs(std::move(args));
}

bool monster_has_skill(const official_data::MonsterRecord& monster, int skill_id) {
    return std::any_of(
        monster.learnable_moves.begin(),
        monster.learnable_moves.end(),
        [skill_id](const official_data::LearnableMoveRecord& move) {
            return move.move_id == skill_id;
        }
    );
}

// 效果注册时点（后续数据化：从 effect_info / side_effect 表查 register_state 列）
State effect_register_state(int effect_id) {
    switch (effect_id) {
        case 6:
        case 8:
            return State::BATTLE_FIRST_ATTACK_DAMAGE;
        case 1256:  // 王·酷烈风息 "造成的伤害低于X"：需伤害结算后读 resolvedDamage.final
        case 1221:  // 王·酷烈风息 "反转自身能力下降"：攻击技能**先结算伤害再反转**——
                    // 反转不参与本次伤害（本次用反转前等级，提升留给下次），故伤害结算后操作 levels
            return State::BATTLE_FIRST_AFTER_ACTION;
        case 1960:  // 希拓·神煌炎舞斩 "击败对手则令自身N回合内强化无法被消除或吸取"
                    // → 击败对手后时点（本轮线性序最后，本技能效果仍在桶里）
            return State::BATTLE_AFTER_DEFEATING_OPPONENT;
        default:
            return State::BATTLE_FIRST_SKILL_EFFECT;
    }
}

// 基值先制效果模板：MOVE_RIGHT 时点给 owner 的先手权累加 priority。
// Args: [owner(0), target(1), priority(2)]（bind_participants 会填 owner/target）
EffectResult effect_apply_base_priority(BattleContext* ctx, const EffectArgs& args) {
    if (!ctx || !args.int_args || args.int_count < 3) {
        return EffectResult::kOk;
    }
    const int owner = args.int_args[0];
    const int priority = args.int_args[2];
    if (owner < 0 || owner > 1) {
        return EffectResult::kOk;
    }
    ctx->ws.preemptive_level[owner] += priority;
    return EffectResult::kOk;
}

/**
 * 效果所属分支（HIT / SKILL_INVALID）。
 *
 * 官方 effect 描述决定效果何时触发：
 *   - 普通效果：技能命中才触发 → HIT 分支
 *   - "技能无效时XXX" 的被动效果：技能 miss/禁用时仍要执行 → SKILL_INVALID 分支
 *
 * 目前已知的"无效时触发"效果（数据可后续移入 DB 表）：
 *   - 2006：技能无效时，免疫下1次对手的攻击，免疫成功则令对手全属性+1（索杰德尔·无念归空净）
 *   - 2501：技能无效时，重新进行伤害结算且每260特攻威力翻倍1次（薇尔诗·乐园之初诞）
 */
SkillExecResult default_branch_for_effect(int effect_id) {
    switch (effect_id) {
        case 2006:
        case 2501:
        case 2126:  // 烬灭神咒剑：技能无效时消除对手回合类/能力提升 + 焚烬
            return SkillExecResult::SKILL_INVALID;
        default:
            return SkillExecResult::HIT;
    }
}

// 穿透类效果的凭证位（697"无视伤害限制"/699"无视攻击免疫"）。
// 与 effect_meta 的 Penetration 分类双保险：meta 负责"这是不是穿透类"，
// 此表负责"穿什么"；未列出的穿透效果返回全零（降级为无凭证）。
Skills::PenetrationFlags penetration_flags_for_effect(int effect_id) {
    Skills::PenetrationFlags flags;
    switch (effect_id) {
        case 697:  // 无视伤害限制效果
            flags.ignore_damage_limit = true;
            flags.level = 1;
            break;
        case 699:  // 无视攻击免疫效果
            flags.ignore_attack_immunity = true;
            flags.level = 1;
            break;
        default:
            break;
    }
    return flags;
}

// 把"技能自带穿透（697/699）+ 活跃次数授予"合并进 ws.attack_credential[owner]。
// 攻击时现算（每次攻击重新合并）而非每回合物化——额外行动/多攻击不会复用已消费凭证，
// 回合中段新获得的授予也能即时读到。凭证在 ws 里每回合 reset 自动清，无需手动销毁。
void materialize_attack_credential(BattleContext* ctx, int owner, const Skills& skill) {
    if (!ctx || owner < 0 || owner > 1) {
        return;
    }
    BattleWorkspace::AttackCredential& cred = ctx->ws.attack_credential[owner];
    cred = BattleWorkspace::AttackCredential{};  // 用默认成员清零
    cred.ignore_attack_immunity |= skill.penetration_flags.ignore_attack_immunity;
    cred.ignore_damage_limit |= skill.penetration_flags.ignore_damage_limit;
    cred.force_execute |= skill.penetration_flags.force_execute;
    cred.level = std::max(cred.level, skill.penetration_flags.level);
    for (const auto& grant : ctx->penetration_grants[owner]) {
        if (grant.remaining <= 0) {
            continue;
        }
        cred.ignore_attack_immunity |= grant.ignore_attack_immunity;
        cred.ignore_damage_limit |= grant.ignore_damage_limit;
        cred.level = std::max(cred.level, grant.level);
    }
    // 魂印条件凭证（SET 端）：使用 PP=0 技能 + force_execute_on_pp0 → 强制执行（无为觉者 2260）。
    if (ctx->force_execute_on_pp0[owner] && skill.pp == 0) {
        cred.force_execute = true;
    }
    cred.valid = cred.ignore_attack_immunity || cred.ignore_damage_limit || cred.force_execute;
}

// 通用执行器：运行解析出的条件效果单元（Effect.args.extra 指向单元，见 loadSkills）。
// 组合语法：未注册函数的效果模板经 parse_effect_unit 解析成单元，由此函数驱动。
EffectResult effect_run_parsed_unit(BattleContext* ctx, const EffectArgs& args) {
    const auto* unit = static_cast<const EffectUnit*>(args.extra);
    if (!ctx || !unit) {
        return EffectResult::kOk;
    }
    execute_effect_unit(ctx, args, *unit);
    return EffectResult::kOk;
}

} // namespace

Skills::Skills(int id, const official_data::MonsterRecord& monster)
    : id(id) {
    if (id <= 0) {
        throw std::runtime_error("invalid skill id: " + std::to_string(id));
    }
    if (!monster_has_skill(monster, id)) {
        throw std::runtime_error(
            "skill " + std::to_string(id) + " does not belong to pet " +
            std::to_string(monster.id) + " (" + monster.name + ")"
        );
    }
    if (!loadSkills()) {
        throw std::runtime_error("Failed to load skill with id: " + std::to_string(id));
    }
}

bool Skills::loadSkills() {
    auto& store = official_data::OfficialDataStore::instance();
    if (!store.ready()) {
        if (!store.initialize()) {
            return false;
        }
    }

    const std::optional<official_data::SkillRecord> record = store.repository().load_skill(id);
    if (!record) {
        return false;
    }

    name = record->name;
    type = map_skill_type(record->category);
    power = record->power;
    accuracy = record->accuracy;
    must_hit = record->must_hit != 0;
    priority = record->priority;
    maxPP = record->max_pp;
    pp = record->max_pp;
    critical_strike_rate = 0.0f;  // 默认不暴击；效果/测试可改（官方 crit_rate 列未接入，后续可加载）
    element[0] = record->type_id;
    element[1] = 0;
    rawEffectRecords = record->effects;
    effectBranches.clear();
    selection_effects_.clear();
    parsed_units_.clear();
    // 解析器的分支指针指向 parsed_units_ 内元素，reserve 足量防 realloc 悬垂。
    parsed_units_.reserve(rawEffectRecords.size() * 3 + 4);

    for (const auto& effect_record : rawEffectRecords) {
        // 穿透类效果（697"无视伤害限制"/699"无视攻击免疫"）→ 并入本技能穿透凭证，
        // 不注册普通分支。理由：穿透在 query_usage 门判定（效果注册之前）就消费，
        // 697/699 是 args_num=0 的纯标记模板，注册成 HIT 分支既无函数可执行也时机太晚。
        const EffectMeta* meta = EffectMetaCatalog::instance().find(effect_record.effect_id);
        if (meta && meta->category == EffectCategory::Penetration) {
            const PenetrationFlags pf = penetration_flags_for_effect(effect_record.effect_id);
            penetration_flags.ignore_attack_immunity |= pf.ignore_attack_immunity;
            penetration_flags.ignore_damage_limit |= pf.ignore_damage_limit;
            penetration_flags.level = std::max(penetration_flags.level, pf.level);
            continue;
        }
        // 条件先制效果 2000（大雪纷飞/烬灭神咒剑的固有效果：若对手处于能力提升状态则
        // 先制+1且必定命中）→ selection_effects_（on_selected 统一注册到 MOVE_RIGHT 桶，
        // 先手权比较前生效）。与基值先制同族；一般化的"选择期效果路由"留数据驱动后续。
        if (effect_record.effect_id == 2000) {
            Effect sel = clone_effect(effect_record.effect_id, build_effect_args_for_skill(effect_record));
            if (sel.logic) {
                selection_effects_.push_back(
                    SkillEffectNode(std::move(sel), State::BATTLE_FIRST_MOVE_RIGHT));
            }
            continue;
        }
        // ── 认证数据层（custom_* 表）──────────────────────────────
        // ① override(官差纠偏, 待 seeds 落地后接 dispatch：ignore/dead_column/map_to/...)；
        // ② program(离线编码效果程序)：命中则用 JSON 加载器构造单元, 替代下方"注册函数→
        //    运行时文本 parser"路径（离线编码、不碰脆弱官方文本解析）。
        // 两张表在未落地前(表空/缺表)安全返回 nullopt → 走既有路径, 行为不变。
        auto& repository = store.repository();
        const auto custom_ovr = repository.load_custom_override(effect_record.effect_id);
        const auto custom_prog = repository.load_custom_program(effect_record.effect_id, this->id);
        (void)custom_ovr;  // override dispatch 待 seeds 落地后接（现在只是查询接缝）。
        if (custom_prog) {
            // 离线编码程序：JSON → EffectUnit（skill_args 解析 {"arg":n} 占位）。
            const int unit_idx =
                load_effect_unit_from_json(custom_prog->unit_json, effect_record.args, parsed_units_);
            if (unit_idx >= 0) {
                Effect unit_effect;
                unit_effect.id = effect_record.effect_id;
                unit_effect.logic = &effect_run_parsed_unit;
                unit_effect.args = EffectArgs(
                    build_effect_args_for_skill(effect_record).owned_int_args,
                    &parsed_units_[static_cast<std::size_t>(unit_idx)]
                );
                add_effect_node(
                    default_branch_for_effect(effect_record.effect_id),
                    SkillEffectNode(std::move(unit_effect), effect_register_state(effect_record.effect_id))
                );
            }
            // 程序加载失败(unit_idx<0)：声明在 custom 层却解析失败 → 数据 bug, 跳过并暴露。
            continue;
        }
        // 未命中离线程序：走既有 注册函数 → parser 兜底路径。
        Effect effect = clone_effect(effect_record.effect_id, build_effect_args_for_skill(effect_record));
        if (!effect.logic) {
            // 未注册函数：尝试解析模板为条件效果单元（组合语法），成功则注册通用执行器
            // （args.extra 指向 Skills::parsed_units_ 内单元）。失败维持跳过（现状）。
            const int unit_idx = parse_effect_unit(effect_record.info, effect_record.args, parsed_units_);
            if (unit_idx >= 0) {
                Effect unit_effect;
                unit_effect.id = effect_record.effect_id;
                unit_effect.logic = &effect_run_parsed_unit;
                unit_effect.args = EffectArgs(
                    build_effect_args_for_skill(effect_record).owned_int_args,
                    &parsed_units_[static_cast<std::size_t>(unit_idx)]
                );
                add_effect_node(
                    default_branch_for_effect(effect_record.effect_id),
                    SkillEffectNode(std::move(unit_effect), effect_register_state(effect_record.effect_id))
                );
            }
            continue;
        }
        add_effect_node(
            default_branch_for_effect(effect_record.effect_id),
            SkillEffectNode(std::move(effect), effect_register_state(effect_record.effect_id))
        );
    }

    // 基值先制也作为一条选择期效果数据放进 selection_effects_：
    // on_selected 统一遍历注册到 MOVE_RIGHT 时点（即使本回合被控导致出招失败也生效）。
    // 条件先制效果（如"对手有护盾则先制+1"）由数据/插件在此之后追加。
    Effect base_priority_effect;
    base_priority_effect.id = 0;
    base_priority_effect.logic = &effect_apply_base_priority;
    base_priority_effect.args = EffectArgs(std::vector<int>{0, 1, priority});
    selection_effects_.push_back(
        SkillEffectNode(std::move(base_priority_effect), State::BATTLE_FIRST_MOVE_RIGHT)
    );
    return true;
}

bool Skills::skill_usable(BattleContext* ctx, int owner) {
    if (is_locked) {
        return false;
    }

    if (pp == -1) {
        return true;
    }

    if (pp > 0) {
        return true;
    }

    if (pp < 0) {
        return false;
    }

    // PP=0：魂印 ignore_pp 信号（无为觉者 2260 等）→ 可选
    if (ctx && owner >= 0 && owner <= 1 && ctx->ignore_pp[owner]) {
        return true;
    }

    bool hasIgnorePPEffect = false;
    bool hasForceRespectPPEffect = false;
    for (const auto& entry : usabilityEffects) {
        if (!entry.active) {
            continue;
        }
        if (entry.type == SkillUsabilityEffectType::IgnorePP) {
            hasIgnorePPEffect = true;
        } else if (entry.type == SkillUsabilityEffectType::ForceRespectPP) {
            hasForceRespectPPEffect = true;
        }
    }

    return hasIgnorePPEffect && !hasForceRespectPPEffect;
}

SkillSelectionResult Skills::query_selectable(BattleContext* ctx, int owner) {
    if (is_locked) {
        return SkillSelectionResult::LOCKED;
    }
    if (pp == 0) {
        // PP=0：魂印 ignore_pp 信号优先；否则需 active 的 IgnorePP 效果（且无 ForceRespectPP 覆盖）
        if (ctx && owner >= 0 && owner <= 1 && ctx->ignore_pp[owner]) {
            return SkillSelectionResult::SELECTABLE;
        }
        bool hasIgnorePPEffect = false;
        bool hasForceRespectPPEffect = false;
        for (const auto& entry : usabilityEffects) {
            if (!entry.active) {
                continue;
            }
            if (entry.type == SkillUsabilityEffectType::IgnorePP) {
                hasIgnorePPEffect = true;
            } else if (entry.type == SkillUsabilityEffectType::ForceRespectPP) {
                hasForceRespectPPEffect = true;
            }
        }
        return (hasIgnorePPEffect && !hasForceRespectPPEffect)
            ? SkillSelectionResult::SELECTABLE
            : SkillSelectionResult::PP_EMPTY;
    }
    if (pp < 0) {
        return SkillSelectionResult::LOCKED;
    }
    return SkillSelectionResult::SELECTABLE;
}

void Skills::on_selected(BattleContext* ctx, int owner) {
    if (!ctx || owner < 0 || owner > 1) {
        return;
    }
    // 选择期效果统一注册到 MOVE_RIGHT 时点（含基值先制 + 条件先制）。
    // 走 registerEffect 统一处理（valid_id 绑定 + 同源去重），source_id = 技能 id。
    for (const SkillEffectNode& node : selection_effects_) {
        Effect effect = node.effect;
        if (!effect.logic) {
            continue;
        }
        // 绑定参与者：args[0]=owner, args[1]=1-owner
        if (effect.args.owned_int_args.size() >= 2) {
            effect.args.owned_int_args[0] = owner;
            effect.args.owned_int_args[1] = 1 - owner;
            effect.args.refresh_views();
        }
        // left_round==0 的一次性效果归一化为本回合有效（同 continuousEffect.cpp 规则）
        const int left_round = effect.left_round;
        const int duration = (left_round < 0) ? -1 : (left_round == 0 ? 1 : left_round);
        auto ce = std::make_unique<ContinuousEffect>(
            effect, State::BATTLE_FIRST_MOVE_RIGHT, owner, duration, ctx->roundCount
        );
        ce->source_id_ = id;  // 技能 id 作为来源，同源去重
        ctx->registerEffect(State::BATTLE_FIRST_MOVE_RIGHT, owner, std::move(ce), EffectContainer::Skill);
    }
}

SkillUsageResult Skills::query_usage(BattleContext* ctx, int owner) {
    if (!ctx || owner < 0 || owner > 1) {
        return SkillUsageResult::MISS;
    }

    // 0) 物化本次请求凭证（穿透 697/699 + 次数授予 + 强制执行），供 ①miss 与 ②门判定读。
    materialize_attack_credential(ctx, owner, *this);

    // ① 命中判定（优先级最高）：强制执行隐含必定命中 → 跳过 miss 计算。
    if (!must_hit && !ctx->ws.attack_credential[owner].force_execute) {
        const int accuracy = this->accuracy;
        const float dodge_chance = ctx->ws.dodge_rate[1 - owner];
        const int hit_chance = accuracy - static_cast<int>(dodge_chance * 100);
        if ((std::rand() % 100) >= hit_chance) {
            // miss 也照常 notify 中心：文档 §2.3「一旦本次技能命中失败（miss 类），
            // 会消耗所有可响应的次数类效果」——狮盔会被响应并消耗，尽管技能是 miss 的。
            ctx->rule_center_.notify(
                ctx, owner, type == SkillType::Attribute, this->power,
                ctx->ws.attack_credential[owner].ignore_attack_immunity);
            return SkillUsageResult::MISS;
        }
    }

    // ② 门判定（技能无效中心：盔 / 威 / 封属 / 封属·命中失效）。
    // 文档 §2.3/§5.2 **全部消费**：一次技能使用会消耗**所有**响应它的次数类条目
    // （无效类与命中失效类在同一次遍历中统一消费），不因某个盔挡了另一个就保留次数。
    // 回合类条目响应但不消耗（靠减扣点/断回合结束）。
    // 穿透只绕"可穿盔"：条件盔/龙威（penetrable=false）即使有凭证也照旧被挡。
    // 注：miss 分支已在 ① 提前 return —— 但那里**也要** notify（miss 同样消费可响应的次数类），
    //     见下方 ① 的改动。miss 时该处返回被丢弃（走 MISS，不看命中失效）。
    const bool is_attribute = (type == SkillType::Attribute);
    const SkillInvalidNotifyResult nr = ctx->rule_center_.notify(
        ctx, owner, is_attribute, this->power,
        ctx->ws.attack_credential[owner].ignore_attack_immunity);
    switch (nr) {
        case SkillInvalidNotifyResult::INVALID:
            return SkillUsageResult::SEALED;      // 被无效（盔/威/封属）→ SKILL_INVALID + 补偿
        case SkillInvalidNotifyResult::HIT_INVALID:
            return SkillUsageResult::HIT_INVALID; // 命中失效（只封属性技能）→ 效果失效、无补偿
        case SkillInvalidNotifyResult::NONE:
        default:
            return SkillUsageResult::OK;
    }
}

void Skills::register_usability_effect(int effectId, SkillUsabilityEffectType type, bool active) {
    for (auto& entry : usabilityEffects) {
        if (entry.effectId == effectId) {
            entry.type = type;
            entry.active = active;
            return;
        }
    }
    usabilityEffects.push_back(SkillUsabilityEffectEntry{effectId, type, active});
}

void Skills::set_usability_effect_active(int effectId, bool active) {
    for (auto& entry : usabilityEffects) {
        if (entry.effectId == effectId) {
            entry.active = active;
            return;
        }
    }
}

void Skills::remove_usability_effect(int effectId) {
    for (auto it = usabilityEffects.begin(); it != usabilityEffects.end(); ++it) {
        if (it->effectId == effectId) {
            usabilityEffects.erase(it);
            return;
        }
    }
}

void Skills::clear_usability_effects() {
    usabilityEffects.clear();
}

Effect Skills::clone_effect(int effectId, EffectArgs args) const {
    return EffectFactory::getInstance().getEffect(effectId, std::move(args));
}

void Skills::add_effect_node(SkillExecResult result, SkillEffectNode node) {
    effectBranches[result].push_back(std::move(node));
}

// ================================================================
// 以下内容原在 src/effects/continuousEffect.cpp —— 该文件 229 行里
// ContinuousEffect:: 的实现是 0 个（全在头文件内联），实际装的全是 Skills:: 的方法。
// 文件名与内容无关，故整体搬入本文件（skills.cpp），使 Skills 的实现集中一处；
// continuousEffect.cpp 随之删除。
// 内容：Skills::execute（执行期主流程）/ is_hit_effect_invalid（③层）/
//       register_branch（分支注册 + 逐节点 nullify 过滤）
// ================================================================

namespace {

SkillResolutionFlags resolution_flags_for(SkillExecResult result) {
    switch (result) {
        case SkillExecResult::HIT:
            return SkillResolutionFlags{true, true};
        case SkillExecResult::SKILL_INVALID:
            return SkillResolutionFlags{true, false};
        case SkillExecResult::EFFECT_INVALID:
            return SkillResolutionFlags{false, true};
        default:
            return SkillResolutionFlags{};
    }
}

int first_mover_id(const BattleContext* ctx) {
    if (!ctx) {
        return 0;
    }
    if (ctx->preemptive_right == PreemptiveRight::SEER_ROBOT_2) {
        return 1;
    }
    return 0;
}

State state_for_owner(State state, int owner, const BattleContext* ctx) {
    const bool owner_is_first = owner == first_mover_id(ctx);
    if (owner_is_first) {
        return state;
    }

    switch (state) {
        case State::BATTLE_FIRST_ON_SKILL_HIT:
            return State::BATTLE_SECOND_ON_SKILL_HIT;
        case State::BATTLE_FIRST_SKILL_EFFECT:
            return State::BATTLE_SECOND_SKILL_EFFECT;
        case State::BATTLE_FIRST_ATTACK_DAMAGE:
            return State::BATTLE_SECOND_ATTACK_DAMAGE;
        case State::BATTLE_FIRST_AFTER_ACTION:
            return State::BATTLE_SECOND_AFTER_ACTION;
        case State::BATTLE_FIRST_ACTION_END:
            return State::BATTLE_SECOND_ACTION_END;
        case State::BATTLE_FIRST_AFTER_ACTION_END:
            return State::BATTLE_SECOND_AFTER_ACTION_END;
        default:
            return state;
    }
}

void bind_participants(Effect& effect, int owner) {
    if (effect.args.owned_int_args.size() < 2) {
        return;
    }
    effect.args.owned_int_args[0] = owner;
    effect.args.owned_int_args[1] = 1 - owner;
    effect.args.refresh_views();
}

/**
 * 把 Effect.left_round 转成 ContinuousEffect 的持续回合数。
 *
 * - left_round < 0：永久效果（从不按回合过期）。
 * - left_round == 0：一次性效果（技能效果默认值）。若直接以 0 作为
 *   duration_rounds_，isExpired 会算出"当前回合 - 注册回合 >= 0"= 立即过期，
 *   导致效果永不执行。这里归一化为 1：本回合有效，后续回合被 isExpired 拦截
 *   不重放，并在下个回合扣减点被 cleanup 移除。
 * - left_round > 0：按原值（N 回合的回合类效果）。
 */
int duration_for_effect(int left_round) {
    if (left_round < 0) return -1;
    if (left_round == 0) return 1;
    return left_round;
}

} // namespace

// Skills::execute — 技能执行期主流程（替代 SkillExecutionEffect）
//
// 流程：query_usage 判可用性（miss/封属性/封攻击）→
//       命中效果失效判定（EFFECT_INVALID，不注册任何效果）→
//       命中（注册 HIT 分支）→ 各分支注册效果到对应时点桶。
std::pair<SkillExecResult, SkillResolutionFlags> Skills::execute(BattleContext* ctx, int owner, State trigger_state) {
    (void)trigger_state;
    if (!ctx || owner < 0 || owner > 1) {
        return {SkillExecResult::SKILL_INVALID, resolution_flags_for(SkillExecResult::SKILL_INVALID)};
    }

    // 执行期可用性判定（统一走 query_usage：miss + 封属性/封攻击/命中失效）
    const SkillUsageResult usage = query_usage(ctx, owner);
    if (usage == SkillUsageResult::MISS || usage == SkillUsageResult::SEALED) {
        const SkillResolutionFlags flags = resolution_flags_for(SkillExecResult::SKILL_INVALID);
        ctx->event_center_.emit(BattleEvent{EventType::EVENT_SKILL_INVALID, owner, ctx->opponent(owner)});
        register_branch(ctx, owner, SkillExecResult::SKILL_INVALID, flags);
        return {SkillExecResult::SKILL_INVALID, flags};
    }
    // 命中失效（SEAL_ATTRIBUTE_HIT）：技能照常命中，但效果不被注册、不触发 SKILL_INVALID 补偿。
    // 只由 SkillInvalidCenter 的门判定引起（仅封属性技能）；与下方 ③层 走同一 hit-invalid 执行路径
    // （逐节点 nullify 过滤 + EVENT_HIT）。这里 **不 emit EVENT_SKILL_INVALID**、不注册无效补偿分支。
    if (usage == SkillUsageResult::HIT_INVALID) {
        const SkillResolutionFlags flags = resolution_flags_for(SkillExecResult::HIT);
        register_branch(ctx, owner, SkillExecResult::HIT, flags, /*filter_hit_invalid=*/true);
        ctx->event_center_.emit(BattleEvent{EventType::EVENT_HIT, owner, ctx->opponent(owner)});
        return {SkillExecResult::HIT, flags};
    }

    // 成功使用攻击技能 → 统一消费次数型穿透授予（"下一次攻击"语义：即使对手无阻挡也消费）。
    // miss/sealed 已在上方提前 return 不消费；属性技能无攻击语义不消费。
    // EFFECT_INVALID（命中效果失效）也算成功使用 → 也消费。
    if (type != SkillType::Attribute) {
        ctx->consume_penetration_grants_after_attack(owner);
        ctx->consume_attack_boost_grants_after_attack(owner);  // 次数型攻击增伤同步消费（"下1次攻击"语义）
    }

    // 命中效果失效③层：效果选择性注册（逐节点 nullify 过滤），伤害按模式处理。
    // ③层绝不注册 SKILL_INVALID 补偿分支；强制执行由 is_hit_effect_invalid 返回 nullopt 绕过。
    const std::optional<HitInvalidMode> invalid_mode = is_hit_effect_invalid(ctx, owner);
    if (invalid_mode.has_value()) {
        if (*invalid_mode == HitInvalidMode::kFullNull) {
            ctx->ws.hit_invalid_zero_damage[owner] = true;  // 白板：命中效果失效 + 伤害归0
        }
        const SkillResolutionFlags flags = resolution_flags_for(SkillExecResult::HIT);
        register_branch(ctx, owner, SkillExecResult::HIT, flags, /*filter_hit_invalid=*/true);
        ctx->event_center_.emit(BattleEvent{EventType::EVENT_HIT, owner, ctx->opponent(owner)});
        return {SkillExecResult::HIT, flags};
    }

    const SkillResolutionFlags flags = resolution_flags_for(SkillExecResult::HIT);
    register_branch(ctx, owner, SkillExecResult::HIT, flags);

    // 技能命中事件（"技能命中后/受到攻击后"监听；属性技能也算命中，但无伤害量）
    ctx->event_center_.emit(BattleEvent{EventType::EVENT_HIT, owner, ctx->opponent(owner)});

    return {SkillExecResult::HIT, flags};
}

std::optional<HitInvalidMode> Skills::is_hit_effect_invalid(BattleContext* ctx, int owner) const {
    // 强制执行：无视命中效果失效 → 无失效（凭证已在 query_usage 0) 步物化）。
    if (ctx && owner >= 0 && owner <= 1 && ctx->ws.attack_credential[owner].force_execute) {
        return std::nullopt;
    }
    // ③层触发源：防御方（1-owner）挂了命中效果失效 → 消费一次并返回模式（RuleCenter HIT_INVALID）。
    if (ctx && owner >= 0 && owner <= 1) {
        const std::optional<int> mode = ctx->rule_center_.consume_hit_invalid(/*defender=*/1 - owner);
        if (mode.has_value()) {
            return static_cast<HitInvalidMode>(*mode);
        }
    }
    return std::nullopt;
}

void Skills::register_branch(BattleContext* ctx, int owner, SkillExecResult result,
                             const SkillResolutionFlags& flags, bool filter_hit_invalid) {
    if (!flags.registerSkillEffects) {
        return;
    }

    auto it = effectBranches.find(result);
    if (it == effectBranches.end()) return;

    for (const SkillEffectNode& node : it->second) {
        Effect effect = node.effect;
        bind_participants(effect, owner);
        if (!effect.logic) continue;
        // ③层：命中效果失效时跳过可否决节点（逐效果 nullify 标签，非整技能一刀切）
        if (filter_hit_invalid) {
            const EffectMeta* meta = EffectMetaCatalog::instance().find(effect.id);
            if (meta && meta->nullify.hit_effect_invalidatable) {
                continue;
            }
        }

        const State register_state = state_for_owner(node.registerState, owner, ctx);
        const State pending_observe_state = state_for_owner(node.pendingObserveState, owner, ctx);
        // one-shot (left_round==0) 归一化为本回合有效的 1 回合效果，否则立即过期永不执行
        const int duration = duration_for_effect(effect.left_round);

        if (node.usePendingTrigger) {
            ctx->registerPendingEffect(
                pending_observe_state,
                owner,
                std::make_unique<FutureTrigger>(
                    effect.id,
                    owner,
                    pending_observe_state,
                    nullptr,
                    [ctx, owner, registerState = register_state, effect, duration](BattleContext*) {
                        ctx->registerEffect(
                            registerState,
                            owner,
                            std::make_unique<ContinuousEffect>(
                                effect,
                                registerState,
                                owner,
                                duration,
                                ctx->roundCount
                            )
                        );
                    },
                    ctx->roundCount,
                    node.pendingTtlRounds,
                    node.pendingConsumeOnTrigger
                )
            );
            continue;
        }

        ctx->registerEffect(
            register_state,
            owner,
            std::make_unique<ContinuousEffect>(
                effect,
                register_state,
                owner,
                duration,
                ctx->roundCount
            )
        );
    }
}

