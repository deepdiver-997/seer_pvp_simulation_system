# include/fsm — 调度层

战斗的**状态机骨架**与所有**状态收纳盒**。`effects/` 定义效果，`fsm/` 决定"某时点执行谁"。

| 文件 | 职责（一句话） | 关键入口 / 字段 | 关联文档 |
|------|--------------|----------------|---------|
| `battleFsm.h` | **状态机**：按 `State` 线性序推进 | `BattleFsm::run(BattleContext*)`、`stateHandlerMap`（State→handler）、`perform_switch()`（换宠/死亡换宠共用漏斗）、`submit_operation` | CLAUDE.md §3.2（效果执行唯一入口在 handler 里） |
| `battleContext.h` | **Fsm 状态收纳盒**：所有长期战斗状态 + 时点桶 | `BattleContext`：`skills_effects`/`soul_mark_effects`（TimedBucket）/`skill_seals`/`roundChoice`/`seerRobot`/`ws`、内联 `register_skill_effect`/`register_soulmark_effect`/`invalidate_all_round_effects`/`grant_skill_invalid`/`grant_penetration`/`find_pet_with_soulmark`（插件调的就是这些） | CLAUDE.md §3.2/3.6/3.9 |
| `battleWorkspace.h` | **每回合重置的临时层**（凭证朝生暮灭） | `BattleWorkspace ws`：`skill_power_view`/`skill_element_view`/`eff_*_resist_pct`/`attack_credential`/`skill_effect_source`（kExecOnly 替换）/`dodge_rate`/`heal_mod_pct`… **每回合 `handle_BattleRoundStart` 开头 reset()** | CLAUDE.md §3.3/3.7/3.8 坑7 |
| `state.h` | **State 枚举**（-1..39）+ 中文名 + 免疫覆盖位 | `State`、`state_name_cn`、`state_coverage_*`/`coverage_all` | CLAUDE.md §3.2 坑2（想引用 State 的**必须显式 include 本头**） |
| `iControlBlock.h` | **控制块抽象**（输入等待/输出/日志/上下文管理） | `IControlBlock`、`BattleControlBlock`、`TrainingControlBlock`（training_cli 用） | CLAUDE.md §3、docs/01 |
| （内含）`SkillReplaceSource` | 技能替换**执行期重定向描述符**（kExecOnly/kFull） | 去 `battleContext.h` 的 `pending_skill_replacement`、`battleWorkspace` 的 `skill_effect_source` | CLAUDE.md §3.4 |
| （内含）`DamageSnapshot` | 单次攻击的伤害快照 | 去 `battleContext.h` / `damage_pipeline.h` | CLAUDE.md §3.7 |

**关键区分 — "ws" vs "context"：**

| | `BattleWorkspace ws` | `BattleContext`（字段） |
|---|---|---|
| 生命周期 | **每回合 reset** | 跨回合保留（效果桶/命令类） |
| 放什么 | 本回合凭证：威力/系别/抗性视图、穿透凭证、kExecOnly 替换 | 持久命令：`block_offstage_to_onstage`、`skill_seals`、技能替换 pending 载体、`roundChoice` |

跨回合的东西放 context，朝生暮灭的东西放 ws —— 这是本项目最常用的心智判断。