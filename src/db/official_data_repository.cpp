#include <db/official_data_repository.h>

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <utility>
#include <iostream>

namespace official_data {

namespace {

class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (db) {
            sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr);
        }
    }

    ~Statement() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const { return stmt_; }
    explicit operator bool() const { return stmt_ != nullptr; }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::vector<int> parse_int_list(const std::string& raw) {
    std::vector<int> values;
    std::istringstream iss(raw);
    int value = 0;
    while (iss >> value) {
        values.push_back(value);
    }
    return values;
}

std::string column_text(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : std::string{};
}

bool bind_int(sqlite3_stmt* stmt, int index, int value) {
    return sqlite3_bind_int(stmt, index, value) == SQLITE_OK;
}

bool bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

std::optional<int> query_side_effect_arg_count(sqlite3* db, int effect_id) {
    Statement stmt(db, "SELECT arg_count FROM side_effect WHERE id = ?1");
    if (!stmt || !bind_int(stmt.get(), 1, effect_id)) {
        return std::nullopt;
    }
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return sqlite3_column_int(stmt.get(), 0);
}

void fill_effect_metadata(sqlite3* db, SkillEffectRecord& effect) {
    Statement stmt(
        db,
        "SELECT args_num, info, param, COALESCE(sp, '') "
        "FROM effect_info WHERE id = ?1"
    );
    if (!stmt || !bind_int(stmt.get(), 1, effect.effect_id)) {
        return;
    }
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return;
    }
    effect.effect_args_num = sqlite3_column_int(stmt.get(), 0);
    effect.info = column_text(stmt.get(), 1);
    effect.param = column_text(stmt.get(), 2);
    effect.sp = column_text(stmt.get(), 3);
}

std::vector<SkillEffectRecord> build_skill_effects(
    sqlite3* db,
    const std::string& side_effect_raw,
    const std::string& side_effect_arg_raw
) {
    const std::vector<int> effect_ids = parse_int_list(side_effect_raw);
    const std::vector<int> flat_args = parse_int_list(side_effect_arg_raw);
    std::vector<SkillEffectRecord> effects;
    effects.reserve(effect_ids.size());

    std::size_t arg_cursor = 0;
    for (int effect_id : effect_ids) {
        SkillEffectRecord effect;
        effect.effect_id = effect_id;
        effect.arg_count = query_side_effect_arg_count(db, effect_id).value_or(0);

        if (effect.arg_count > 0) {
            const std::size_t end = std::min(flat_args.size(), arg_cursor + static_cast<std::size_t>(effect.arg_count));
            effect.args.assign(flat_args.begin() + static_cast<std::ptrdiff_t>(arg_cursor),
                               flat_args.begin() + static_cast<std::ptrdiff_t>(end));
            arg_cursor = end;
        }

        fill_effect_metadata(db, effect);
        effects.push_back(std::move(effect));
    }

    return effects;
}

} // namespace

OfficialDataRepository::~OfficialDataRepository() {
    close();
}

bool OfficialDataRepository::open_read_only(const std::string& db_path) {
    std::cout << "db status: " << (db_ == nullptr) << std::endl;
    close();
    last_error_.clear();

    if (!std::filesystem::exists(db_path)) {
        last_error_ = "database file does not exist: " + db_path;
        return false;
    }

    sqlite3* connection = nullptr;
    const int rc = sqlite3_open_v2(db_path.c_str(), &connection, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || !connection) {
        last_error_ = connection ? sqlite3_errmsg(connection) : "sqlite3_open_v2 failed";
        if (connection) {
            sqlite3_close(connection);
        }
        return false;
    }

    sqlite3_busy_timeout(connection, 1000);
    sqlite3_exec(connection, "PRAGMA query_only = ON", nullptr, nullptr, nullptr);

    db_ = connection;
    db_path_ = db_path;
    return true;
}

void OfficialDataRepository::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    db_path_.clear();
}

std::optional<SkillRecord> OfficialDataRepository::load_skill(int move_id) const {
    if (!db_) {
        last_error_ = "database is not open";
        return std::nullopt;
    }

    Statement stmt(
        db_,
        "SELECT id, name, type_id, category, power, accuracy, COALESCE(priority, 0), "
        "COALESCE(max_pp, 0), COALESCE(cd, 0), COALESCE(side_effect, ''), COALESCE(side_effect_arg, '') "
        "FROM moves WHERE id = ?1"
    );
    if (!stmt || !bind_int(stmt.get(), 1, move_id)) {
        last_error_ = sqlite3_errmsg(db_);
        return std::nullopt;
    }

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    SkillRecord skill;
    skill.id = sqlite3_column_int(stmt.get(), 0);
    skill.name = column_text(stmt.get(), 1);
    skill.type_id = sqlite3_column_int(stmt.get(), 2);
    skill.category = sqlite3_column_int(stmt.get(), 3);
    skill.power = sqlite3_column_int(stmt.get(), 4);
    skill.accuracy = sqlite3_column_int(stmt.get(), 5);
    skill.priority = sqlite3_column_int(stmt.get(), 6);
    skill.max_pp = sqlite3_column_int(stmt.get(), 7);
    skill.cd = sqlite3_column_int(stmt.get(), 8);
    skill.effects = build_skill_effects(db_, column_text(stmt.get(), 9), column_text(stmt.get(), 10));
    return skill;
}

std::vector<SkillEffectRecord> OfficialDataRepository::load_skill_effects(int move_id) const {
    const std::optional<SkillRecord> skill = load_skill(move_id);
    return skill ? skill->effects : std::vector<SkillEffectRecord>{};
}

std::optional<int> OfficialDataRepository::find_monster_id_by_exact_name(const std::string& monster_name) const {
    if (!db_) {
        last_error_ = "database is not open";
        return std::nullopt;
    }

    Statement stmt(
        db_,
        "SELECT id FROM monsters "
        "WHERE json_extract(raw_json, '$.DefName') = ?1 "
        "LIMIT 1"
    );
    if (!stmt || !bind_text(stmt.get(), 1, trim(monster_name))) {
        last_error_ = sqlite3_errmsg(db_);
        return std::nullopt;
    }

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return sqlite3_column_int(stmt.get(), 0);
}

std::vector<LearnableMoveRecord> OfficialDataRepository::load_monster_learnable_moves(int monster_id) const {
    std::vector<LearnableMoveRecord> moves;
    if (!db_) {
        last_error_ = "database is not open";
        return moves;
    }

    Statement stmt(
        db_,
        "SELECT "
        "json_extract(json_each.value, '$.ID') AS move_id, "
        "COALESCE(json_extract(json_each.value, '$.LearningLv'), 0) AS learning_lv "
        "FROM monsters, json_each(monsters.raw_json, '$.LearnableMoves.Move') "
        "WHERE monsters.id = ?1 "
        "ORDER BY learning_lv ASC, move_id ASC"
    );
    if (!stmt || !bind_int(stmt.get(), 1, monster_id)) {
        last_error_ = sqlite3_errmsg(db_);
        return moves;
    }

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        LearnableMoveRecord item;
        item.move_id = sqlite3_column_int(stmt.get(), 0);
        item.learning_level = sqlite3_column_int(stmt.get(), 1);
        moves.push_back(std::move(item));
    }

    return moves;
}

std::optional<MonsterRecord> OfficialDataRepository::load_monster(int monster_id) const {
    if (!db_) {
        last_error_ = "database is not open";
        return std::nullopt;
    }

    Statement stmt(
        db_,
        "SELECT "
        "id, "
        "COALESCE(json_extract(raw_json, '$.DefName'), ''), "
        "COALESCE(json_extract(raw_json, '$.Type'), 0), "
        "COALESCE(json_extract(raw_json, '$.Type2'), 0), "
        "COALESCE(json_extract(raw_json, '$.AddSeParam'), 0), "
        "COALESCE(json_extract(raw_json, '$.Gender'), 2), "
        "COALESCE(json_extract(raw_json, '$.HP'), 0), "
        "COALESCE(json_extract(raw_json, '$.Atk'), 0), "
        "COALESCE(json_extract(raw_json, '$.Def'), 0), "
        "COALESCE(json_extract(raw_json, '$.SpAtk'), 0), "
        "COALESCE(json_extract(raw_json, '$.SpDef'), 0), "
        "COALESCE(json_extract(raw_json, '$.Spd'), 0) "
        "FROM monsters WHERE id = ?1"
    );
    if (!stmt || !bind_int(stmt.get(), 1, monster_id)) {
        last_error_ = sqlite3_errmsg(db_);
        return std::nullopt;
    }

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    MonsterRecord monster;
    monster.id = sqlite3_column_int(stmt.get(), 0);
    monster.name = column_text(stmt.get(), 1);
    monster.type = sqlite3_column_int(stmt.get(), 2);
    monster.secondary_type = sqlite3_column_int(stmt.get(), 3);
    monster.soul_mark_id = sqlite3_column_int(stmt.get(), 4);
    monster.gender = sqlite3_column_int(stmt.get(), 5);
    monster.hp = sqlite3_column_int(stmt.get(), 6);
    monster.atk = sqlite3_column_int(stmt.get(), 7);
    monster.def = sqlite3_column_int(stmt.get(), 8);
    monster.sp_atk = sqlite3_column_int(stmt.get(), 9);
    monster.sp_def = sqlite3_column_int(stmt.get(), 10);
    monster.spd = sqlite3_column_int(stmt.get(), 11);
    monster.learnable_moves = load_monster_learnable_moves(monster.id);
    return monster;
}

std::optional<MonsterRecord> OfficialDataRepository::load_monster_by_exact_name(const std::string& monster_name) const {
    const std::optional<int> monster_id = find_monster_id_by_exact_name(monster_name);
    return monster_id ? load_monster(*monster_id) : std::nullopt;
}

std::optional<SoulMarkRecord> OfficialDataRepository::load_soul_mark(int soul_mark_id) const {
    if (!db_) {
        last_error_ = "database is not open";
        return std::nullopt;
    }
    if (soul_mark_id <= 0) {
        return std::nullopt;
    }

    Statement stmt(
        db_,
        "SELECT idx, stat, effect_id, COALESCE(args, ''), COALESCE(can_reset, 0), "
        "COALESCE(desc, ''), COALESCE(intro, ''), COALESCE(star_level, 0) "
        "FROM new_se WHERE idx = ?1"
    );
    if (!stmt || !bind_int(stmt.get(), 1, soul_mark_id)) {
        last_error_ = sqlite3_errmsg(db_);
        return std::nullopt;
    }
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    SoulMarkRecord record;
    record.id = sqlite3_column_int(stmt.get(), 0);
    record.stat = sqlite3_column_int(stmt.get(), 1);
    record.effect_id = sqlite3_column_int(stmt.get(), 2);
    record.args = parse_int_list(column_text(stmt.get(), 3));
    record.can_reset = sqlite3_column_int(stmt.get(), 4);
    record.description = column_text(stmt.get(), 5);
    record.intro = column_text(stmt.get(), 6);
    record.star_level = sqlite3_column_int(stmt.get(), 7);
    return record;
}

OfficialDataStore& OfficialDataStore::instance() {
    static OfficialDataStore store;
    return store;
}

bool OfficialDataStore::initialize(const std::string& db_path) {
    return repository_.open_read_only(db_path);
}

void OfficialDataStore::shutdown() {
    repository_.close();
}

} // namespace official_data
