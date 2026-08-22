#ifndef OFFICIAL_DATA_REPOSITORY_H
#define OFFICIAL_DATA_REPOSITORY_H

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct sqlite3;

namespace official_data {

inline constexpr const char* kDefaultOfficialDatabasePath = "scripts/data/processed/seer_unity.sqlite";

struct SkillEffectRecord {
    int effect_id = -1;
    int arg_count = 0;
    std::vector<int> args;
    int effect_args_num = 0;
    std::string info;
    std::string param;
    std::string sp;
};

struct SkillRecord {
    int id = -1;
    std::string name;
    int type_id = 0;
    int category = 0;
    int power = 0;
    int accuracy = 0;
    int priority = 0;
    int max_pp = 0;
    int cd = 0;
    int must_hit = 0;   // 官方 MustHit：1 = 必中（无视命中率）
    std::vector<SkillEffectRecord> effects;
};

struct LearnableMoveRecord {
    int move_id = -1;
    int learning_level = 0;
};

struct MonsterRecord {
    int id = -1;
    std::string name;
    int type = 0;
    int secondary_type = 0;
    int soul_mark_id = 0;
    int gender = 2;
    int hp = 0;
    int atk = 0;
    int def = 0;
    int sp_atk = 0;
    int sp_def = 0;
    int spd = 0;
    std::vector<LearnableMoveRecord> learnable_moves;
};

struct SoulMarkRecord {
    int id = 0;
    int stat = 0;
    int effect_id = -1;
    std::vector<int> args;
    int can_reset = 0;
    std::string description;
    std::string intro;
    int star_level = 0;
};

// 通用特性（new_se 表 stat=1，50 种 × 等级 0-5）。每种特性是任何精灵都可配置的通用能力，
// 非怪物天生绑定，创建宠物时通过 BattlePetMessage.common_trait_id 指定。
struct CommonTraitRecord {
    int id = 0;           // idx 主键
    int stat = 1;
    int effect_id = -1;
    std::vector<int> args;
    std::string description;  // desc 列（特性名，如 瞬杀/精准/强袭）
    std::string intro;        // intro 列（人读描述）
    int star_level = 0;       // 等级 0-5
};

// 现代精灵魂印展示记录（effect_icon 表）。
// monsters.soul_mark_id 在 unity 库中全为 0（死列），魂印权威数据在 effect_icon：
// pet_id 是 JSON 数组字符串（须精确匹配，LIKE 会误中 14911 之类）；
// tips 是完整描述（含富文本标记），kind 是官方分类标签；
// effect_id 是引擎内部效果号，本库快照不可解析，原样保留。
// 一只精灵可能有多行（基础/强化版本），取 icon_id 最大者为当前版本。
struct SoulMarkDisplayRecord {
    int icon_id = 0;             // effect_icon.id 行主键
    int effect_id = 0;           // 引擎内部效果号（插件函数注册键）
    std::vector<int> kind_tags;  // kind 列（JSON 数组解析）
    std::vector<int> args;       // args 列（空格分隔）
    std::string tips;            // 原始富文本描述
    std::string tips_plain;      // 去富文本纯文本
    std::string come;            // 来源描述
    int monster_id = 0;
};

// effect_des 词典词条。kind=1 专属名词/状态词条（412 条）；kind=2 异常状态定义（46 条）；
// kind=3~5 机制/时点术语（"执行""回合开始时""免疫对手的攻击"等官方正式定义）。
struct TermRecord {
    int id = 0;
    int kind = 0;
    std::string name;         // kinddes 列
    std::string description;  // desc 列
};

// effect_info 效果模板（元数据分类器的输入）。
struct EffectTemplateRecord {
    int id = 0;
    int args_num = 0;
    std::string info;   // 模板文本（{0}{1} 占位符）
    std::string param;  // 参数类型规格（类型 id 数组，unity 库无图例表，原样保留）
};

class OfficialDataRepository {
public:
    OfficialDataRepository() = default;
    OfficialDataRepository(const OfficialDataRepository&) = delete;
    OfficialDataRepository& operator=(const OfficialDataRepository&) = delete;
    ~OfficialDataRepository();

    bool open_read_only(const std::string& db_path = kDefaultOfficialDatabasePath);
    void close();

    bool is_open() const { return db_ != nullptr; }
    const std::string& db_path() const { return db_path_; }
    const std::string& last_error() const { return last_error_; }

    std::optional<SkillRecord> load_skill(int move_id) const;
    std::vector<SkillEffectRecord> load_skill_effects(int move_id) const;

    std::optional<int> find_monster_id_by_exact_name(const std::string& monster_name) const;
    std::optional<MonsterRecord> load_monster(int monster_id) const;
    std::optional<MonsterRecord> load_monster_by_exact_name(const std::string& monster_name) const;
    std::vector<LearnableMoveRecord> load_monster_learnable_moves(int monster_id) const;
    std::optional<SoulMarkRecord> load_soul_mark(int soul_mark_id) const;
    std::optional<CommonTraitRecord> load_common_trait(int idx) const;

    // 现代精灵魂印：按精灵 id 从 effect_icon 精确匹配（pet_id JSON 数组），
    // 多行取 icon_id 最大。无记录返回 nullopt。
    std::optional<SoulMarkDisplayRecord> load_soul_mark_display_by_monster(int monster_id) const;

    // effect_des 词典：名字精确匹配（失败退 LIKE），kind 过滤查询（2=异常状态词表等）。
    std::optional<TermRecord> load_term(const std::string& term_name) const;
    std::vector<TermRecord> load_terms_by_kind(int kind) const;

    // 全量 effect_info 模板（约 2340 条），供 EffectMetaCatalog 构建用。
    std::vector<EffectTemplateRecord> load_all_effect_templates() const;

    // 从 types_relation（官方克制表）填充克制矩阵。
    // matrix[attacker_type_id][defender_type_id] ∈ {0, 1, 2}（0=微弱/免疫, 1=普通, 2=克制）。
    bool load_elemental_restraints(std::vector<std::vector<int>>& matrix) const;

private:
    // 新 Unity 结构：双属性精灵 type 用合并 id（如 41=战斗地面），需分解为两个单属性 id。
    void ensure_type_components_cache() const;
    std::pair<int, int> decompose_type(int type_id) const;

    sqlite3* db_ = nullptr;
    std::string db_path_;
    mutable std::string last_error_;
    mutable bool type_components_cache_loaded_ = false;
    mutable std::unordered_map<int, std::pair<int, int>> type_components_cache_;
};

class OfficialDataStore {
public:
    static OfficialDataStore& instance();

    bool initialize(const std::string& db_path = kDefaultOfficialDatabasePath);
    void shutdown();

    bool ready() const { return repository_.is_open(); }
    const OfficialDataRepository& repository() const { return repository_; }
    OfficialDataRepository& repository() { return repository_; }

private:
    OfficialDataStore() = default;

    OfficialDataRepository repository_;
};

} // namespace official_data

#endif // OFFICIAL_DATA_REPOSITORY_H
