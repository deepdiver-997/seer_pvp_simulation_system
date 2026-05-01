#ifndef OFFICIAL_DATA_REPOSITORY_H
#define OFFICIAL_DATA_REPOSITORY_H

#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace official_data {

inline constexpr const char* kDefaultOfficialDatabasePath = "scripts/data/processed/seer_official.sqlite";

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

private:
    sqlite3* db_ = nullptr;
    std::string db_path_;
    mutable std::string last_error_;
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
