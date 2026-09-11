# include/entities — 组装层

把 DB 静态数据 / 手动数值 / 插件效果 **组装成可参战的对象**（精灵、技能、魂印）。
这里的重点是"对象长什么样"，执行逻辑在 `effects/`，调度在 `fsm/`。

| 文件 | 职责（一句话） | 关键入口 / 字段 | 关联文档 |
|------|--------------|----------------|---------|
| `skills.h` | **技能**对象：静态数据 + 按执行结果分叉的效果分支 | `effectBranches`(map<SkillExecResult, vector<SkillEffectNode>>)、`selection_effects_`（选技能注册先制）、`loadSkills()`（DB）、`register_branch()`（注册到桶）、`resolve_executing_skill()`（替换取用点）、`cancel_next_parsed_condition()`、`parsed_units_` | CLAUDE.md §3.4、`docs/02/技能判定流程与无效效果体系.md`§七 |
| `soul_mark.h` | **魂印**对象 = 程序化效果节点序列 | `SoulMark`=`vector<SoulMarkNodeRef{trigger_state, effect_fn, once, early, scope}>`、`SoulMarkNodeRef` | CLAUDE.md §3.5、`docs/02/魂印机制设计与精灵表现档案.md` |
| `soul_mark_manager.h` | **魂印注册管理**：程序/钩子登记 + 上场激活 | `activate_soul_mark()`（战斗开始+切换上场，扫全部6槽）、`registerSoulMarkProgram(id,nodes)`、`registerSoulMarkHooks(id,hooks)`、`registerSoulMark(id,fn)` | CLAUDE.md §3.5 |
| `elf-pet.h` | **精灵本体**：数值 + 抗性 + 护盾 + 持久槽 | `soulmark_storage`（`map<int,std::any>` 持久槽，pl/:场景H）、`damage_resist`（本体抗性）、`shield_bank_`/`hood_bank_`（护盾/护罩）、`marks`、`resistance`（异常抗性） | CLAUDE.md §3.7/3.8/3.10 |
| `pet_factory.h` | 运行时**数据初始化**工厂 | `initialize_runtime_data()`（相对路径 `scripts/data/processed/seer_unity.sqlite` + 加载插件 DDL） | CLAUDE.md §1（必须从项目根跑） |
| `seer-robot.h` | 玩家**阵营宿主**（持有精灵队伍） | `SeerRobot`、`SeerRobotFactory` | CLAUDE.md §3 |
| `common_trait.h` | 精灵体质/特性数据 | `CommonTrait` | — |
| `elemental-attributes.h` | **元素/系别**与克制倍率 | `ElementalAttributes`、克制矩阵（`calculateRestraintMultiples`） | CLAUDE.md §3.7 |
| `numerical-properties.h` | 精灵**六维数值** | `numerical_properties`、`NumericalPropertyIndex` | — |
| `shield_bank.h` | **护盾/护罩银行**（多来源+优先级消耗） | `ShieldBank`、`Shield`；护盾只响应 NORMAL、护罩只响应 FIXED/PERCENT | CLAUDE.md §3.7 |
| `mark.h` | 精灵**印记** | `Mark` | — |

**读入口建议**：想跟一次"技能执行" → 从 `skills.h` 的 `effectBranches` → 追 `register_branch` 进桶；
想跟"魂印" → 从 `soul_mark_manager.h` 的 `activate_soul_mark` 开始。