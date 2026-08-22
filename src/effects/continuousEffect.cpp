#include <effects/continuousEffect.h>
#include <effects/effect_meta.h>
#include <fsm/battleContext.h>
#include <entities/elf-pet.h>
#include <entities/skills.h>
#include <cstdlib>

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

    // 执行期可用性判定（统一走 query_usage：miss + 封属性/封攻击）
    const SkillUsageResult usage = query_usage(ctx, owner);
    if (usage == SkillUsageResult::MISS || usage == SkillUsageResult::SEALED) {
        const SkillResolutionFlags flags = resolution_flags_for(SkillExecResult::SKILL_INVALID);
        ctx->event_center_.emit(BattleEvent{EventType::EVENT_SKILL_INVALID, owner, ctx->opponent(owner)});
        register_branch(ctx, owner, SkillExecResult::SKILL_INVALID, flags);
        return {SkillExecResult::SKILL_INVALID, flags};
    }

    // 成功使用攻击技能 → 统一消费次数型穿透授予（"下一次攻击"语义：即使对手无阻挡也消费）。
    // miss/sealed 已在上方提前 return 不消费；属性技能无攻击语义不消费。
    // EFFECT_INVALID（命中效果失效）也算成功使用 → 也消费。
    if (type != SkillType::Attribute) {
        ctx->consume_penetration_grants_after_attack(owner);
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
    // ③层触发源：防御方（1-owner）挂了命中效果失效桶 → 消费一次并返回模式。
    if (ctx && owner >= 0 && owner <= 1) {
        auto& entries = ctx->hit_effect_invalids[1 - owner];
        for (auto it = entries.begin(); it != entries.end();) {
            if (it->remaining <= 0) {
                it = entries.erase(it);  // 清理过期条目（entries 保序）
            } else {
                break;  // 第一条有效即用
            }
        }
        if (!entries.empty()) {
            const HitInvalidMode mode = entries.front().mode;
            --entries.front().remaining;
            if (entries.front().remaining <= 0) {
                entries.erase(entries.begin());
            }
            return mode;
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

