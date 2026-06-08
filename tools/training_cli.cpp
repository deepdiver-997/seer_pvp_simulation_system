#include <arpa/inet.h>

#include <boost/asio.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* kDefaultHost = "127.0.0.1";
constexpr int kDefaultPort = 4399;

constexpr std::uint16_t CMD_SELECT_SKILL = 1;
constexpr std::uint16_t CMD_USE_MEDICINE = 2;
constexpr std::uint16_t CMD_CHOOSE_PET = 3;
constexpr std::uint16_t CMD_INIT_BATTLE = 5;
constexpr std::uint16_t CMD_SYNC_STATE = 10;
constexpr std::uint16_t CMD_HEARTBEAT = 11;
constexpr std::uint16_t CMD_DEBUG_STEP = 20;
constexpr std::uint16_t CMD_DEBUG_CONTINUE = 21;
constexpr std::uint16_t CMD_DEBUG_BREAKPOINT = 22;
constexpr std::uint16_t CMD_DEBUG_FULLSTATE = 23;

struct Header {
    std::uint32_t total_length = 0;
    std::uint16_t command = 0;
    std::uint32_t uuid = 0;
};

struct SkillView {
    int id = 0;
    std::string name;
    int pp = 0;
    int max_pp = 0;
};

struct PetView {
    int slot = -1;
    int id = 0;
    std::string name;
    int hp = 0;
    int max_hp = 0;
    bool alive = false;
    bool on_stage = false;
    std::vector<SkillView> skills;
};

struct SyncView {
    std::uint32_t uuid = 0;
    int round = 0;
    int state = 0;
    std::string state_name;
    bool need_input = false;
    int input_player = 0;
    bool need_death_switch0 = false;
    bool need_death_switch1 = false;
    std::array<std::vector<PetView>, 2> party;
};

std::string now_string() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::ostringstream oss;
    oss << std::put_time(&tmv, "%H:%M:%S");
    return oss.str();
}

void log_line(const std::string& level, const std::string& msg) {
    std::cout << "[" << now_string() << "] [" << level << "] " << msg << std::endl;
}

std::optional<int> extract_int_field(const std::string& src, const std::string& key) {
    const std::regex re("\\\"" + key + "\\\":(-?[0-9]+)");
    std::smatch m;
    if (std::regex_search(src, m, re)) {
        return std::stoi(m[1].str());
    }
    return std::nullopt;
}

std::optional<std::string> extract_string_field(const std::string& src, const std::string& key) {
    const std::regex re("\\\"" + key + "\\\":\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (std::regex_search(src, m, re)) {
        return m[1].str();
    }
    return std::nullopt;
}

std::optional<bool> extract_bool_field(const std::string& src, const std::string& key) {
    const std::regex re("\\\"" + key + "\\\":(true|false)");
    std::smatch m;
    if (std::regex_search(src, m, re)) {
        return m[1].str() == "true";
    }
    return std::nullopt;
}

bool extract_object_after_key(const std::string& src, const std::string& key, std::string& out_obj) {
    const std::size_t key_pos = src.find(key);
    if (key_pos == std::string::npos) {
        return false;
    }
    std::size_t i = src.find('{', key_pos + key.size());
    if (i == std::string::npos) {
        return false;
    }
    int depth = 0;
    const std::size_t start = i;
    for (; i < src.size(); ++i) {
        if (src[i] == '{') {
            ++depth;
        } else if (src[i] == '}') {
            --depth;
            if (depth == 0) {
                out_obj = src.substr(start, i - start + 1);
                return true;
            }
        }
    }
    return false;
}

bool extract_object_after_marker(const std::string& src, const std::string& marker, std::string& out_obj) {
    const std::size_t key_pos = src.find(marker);
    if (key_pos == std::string::npos) {
        return false;
    }
    std::size_t i = key_pos + marker.size() - 1;
    if (i >= src.size() || src[i] != '{') {
        return false;
    }

    int depth = 0;
    const std::size_t start = i;
    for (; i < src.size(); ++i) {
        if (src[i] == '{') {
            ++depth;
        } else if (src[i] == '}') {
            --depth;
            if (depth == 0) {
                out_obj = src.substr(start, i - start + 1);
                return true;
            }
        }
    }
    return false;
}

bool extract_array_after_key(const std::string& src, const std::string& key, std::string& out_arr) {
    const std::size_t key_pos = src.find(key);
    if (key_pos == std::string::npos) {
        return false;
    }
    std::size_t i = src.find('[', key_pos + key.size());
    if (i == std::string::npos) {
        return false;
    }
    int depth = 0;
    const std::size_t start = i;
    for (; i < src.size(); ++i) {
        if (src[i] == '[') {
            ++depth;
        } else if (src[i] == ']') {
            --depth;
            if (depth == 0) {
                out_arr = src.substr(start, i - start + 1);
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> split_top_level_objects(const std::string& json_array) {
    std::vector<std::string> objs;
    if (json_array.size() < 2 || json_array.front() != '[' || json_array.back() != ']') {
        return objs;
    }

    int depth = 0;
    std::size_t start = std::string::npos;
    for (std::size_t i = 1; i + 1 < json_array.size(); ++i) {
        const char c = json_array[i];
        if (c == '{') {
            if (depth == 0) {
                start = i;
            }
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objs.push_back(json_array.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objs;
}

std::vector<SkillView> parse_skills_array(const std::string& pet_obj) {
    std::vector<SkillView> out;
    std::string arr;
    if (!extract_array_after_key(pet_obj, "\"skills\":", arr)) {
        return out;
    }

    const auto skill_objs = split_top_level_objects(arr);
    for (const auto& obj : skill_objs) {
        SkillView s;
        s.id = extract_int_field(obj, "id").value_or(0);
        s.name = extract_string_field(obj, "name").value_or("");
        s.pp = extract_int_field(obj, "pp").value_or(0);
        s.max_pp = extract_int_field(obj, "maxPp").value_or(0);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<PetView> parse_party(const std::string& player_obj) {
    std::vector<PetView> out;
    std::string arr;
    if (!extract_array_after_key(player_obj, "\"party\":", arr)) {
        return out;
    }

    const auto pet_objs = split_top_level_objects(arr);
    for (const auto& obj : pet_objs) {
        PetView p;
        p.slot = extract_int_field(obj, "slot").value_or(-1);
        p.id = extract_int_field(obj, "id").value_or(0);
        p.name = extract_string_field(obj, "name").value_or("");
        p.hp = extract_int_field(obj, "hp").value_or(0);
        p.max_hp = extract_int_field(obj, "maxHp").value_or(0);
        p.alive = extract_bool_field(obj, "alive").value_or(false);
        p.on_stage = extract_bool_field(obj, "onStage").value_or(false);
        p.skills = parse_skills_array(obj);
        out.push_back(std::move(p));
    }
    return out;
}

bool parse_sync_view(const std::string& json, SyncView& out) {
    out.uuid = static_cast<std::uint32_t>(extract_int_field(json, "uuid").value_or(0));
    out.round = extract_int_field(json, "round").value_or(0);
    out.state = extract_int_field(json, "state").value_or(0);
    out.state_name = extract_string_field(json, "stateName").value_or("未知状态");
    out.need_input = extract_bool_field(json, "needInput").value_or(false);
    out.input_player = extract_int_field(json, "inputPlayer").value_or(0);

    std::string death_obj;
    if (extract_object_after_marker(json, "\"needDeathSwitch\":{", death_obj)) {
        out.need_death_switch0 = extract_bool_field(death_obj, "player0").value_or(false);
        out.need_death_switch1 = extract_bool_field(death_obj, "player1").value_or(false);
    }

    std::string player0_obj;
    std::string player1_obj;
    if (!extract_object_after_marker(json, "\"player0\":{", player0_obj)) {
        return false;
    }
    if (!extract_object_after_marker(json, "\"player1\":{", player1_obj)) {
        return false;
    }

    out.party[0] = parse_party(player0_obj);
    out.party[1] = parse_party(player1_obj);
    return true;
}

bool print_sync_error_if_any(const std::string& payload) {
    const auto err = extract_string_field(payload, "error");
    if (!err) {
        return false;
    }
    const int err_uuid = extract_int_field(payload, "uuid").value_or(0);
    log_line("WARN", "sync error: " + *err + " (uuid=" + std::to_string(err_uuid) + ")");
    return true;
}

std::vector<char> build_packet(std::uint16_t cmd, std::uint32_t uuid, const std::vector<char>& body) {
    const std::uint32_t total_length = static_cast<std::uint32_t>(10 + body.size());
    const std::uint32_t total_length_be = htonl(total_length);
    const std::uint16_t cmd_be = htons(cmd);
    const std::uint32_t uuid_be = htonl(uuid);

    std::vector<char> packet(10 + body.size());
    std::memcpy(packet.data(), &total_length_be, sizeof(total_length_be));
    std::memcpy(packet.data() + 4, &cmd_be, sizeof(cmd_be));
    std::memcpy(packet.data() + 6, &uuid_be, sizeof(uuid_be));
    if (!body.empty()) {
        std::memcpy(packet.data() + 10, body.data(), body.size());
    }
    return packet;
}

std::vector<char> build_action_body(int robot_id, int action_type, int index) {
    std::vector<char> body(sizeof(int) * 4, 0);
    int payload[4] = {robot_id, action_type, index, 0};
    std::memcpy(body.data(), payload, sizeof(payload));
    return body;
}

std::vector<char> build_int_body(const std::vector<int>& values) {
    std::vector<char> body(values.size() * sizeof(std::uint32_t), 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        std::uint32_t v = htonl(static_cast<std::uint32_t>(values[i]));
        std::memcpy(body.data() + i * sizeof(v), &v, sizeof(v));
    }
    return body;
}

std::vector<char> build_init_012_body() {
    constexpr std::size_t kPartySize = 6;
    constexpr std::size_t kIntsPerPet = 12;
    std::vector<char> body(kPartySize * 2 * kIntsPerPet * sizeof(std::uint32_t), 0);

    const std::array<int, 5> attacker_skills = {10038, 10057, 10001, 10008, 10009};
    const std::array<int, 5> defender_skills = {10001, 10008, 10009, 10033, 20017};

    std::size_t cursor = 0;
    auto write_pet = [&](const std::array<int, 5>& skills) {
        std::uint32_t pet_id = htonl(12);
        std::memcpy(body.data() + cursor, &pet_id, sizeof(pet_id));
        cursor += sizeof(pet_id);

        for (int skill : skills) {
            std::uint32_t v = htonl(static_cast<std::uint32_t>(skill));
            std::memcpy(body.data() + cursor, &v, sizeof(v));
            cursor += sizeof(v);
        }

        for (int i = 0; i < 6; ++i) {
            std::uint32_t base = htonl(0);
            std::memcpy(body.data() + cursor, &base, sizeof(base));
            cursor += sizeof(base);
        }
    };

    for (std::size_t i = 0; i < kPartySize; ++i) {
        write_pet(attacker_skills);
    }
    for (std::size_t i = 0; i < kPartySize; ++i) {
        write_pet(defender_skills);
    }

    return body;
}

bool parse_header(const std::vector<char>& raw, Header& out) {
    if (raw.size() < 10) {
        return false;
    }

    std::uint32_t total_length_be = 0;
    std::uint16_t cmd_be = 0;
    std::uint32_t uuid_be = 0;
    std::memcpy(&total_length_be, raw.data(), sizeof(total_length_be));
    std::memcpy(&cmd_be, raw.data() + 4, sizeof(cmd_be));
    std::memcpy(&uuid_be, raw.data() + 6, sizeof(uuid_be));

    out.total_length = ntohl(total_length_be);
    out.command = ntohs(cmd_be);
    out.uuid = ntohl(uuid_be);
    return true;
}

bool try_extract_frame(std::vector<char>& stream, std::vector<char>& frame) {
    if (stream.size() < 10) {
        return false;
    }
    Header h;
    if (!parse_header(stream, h)) {
        return false;
    }
    if (h.total_length < 10 || stream.size() < h.total_length) {
        return false;
    }
    frame.assign(stream.begin(), stream.begin() + h.total_length);
    stream.erase(stream.begin(), stream.begin() + h.total_length);
    return true;
}

bool wait_one_frame(boost::asio::ip::tcp::socket& socket,
                    std::vector<char>& stream,
                    std::vector<char>& out_frame,
                    int timeout_ms) {
    if (try_extract_frame(stream, out_frame)) {
        return true;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        boost::system::error_code ec;
        const std::size_t available = socket.available(ec);
        if (ec) {
            log_line("ERROR", "socket available failed: " + ec.message());
            return false;
        }
        if (available > 0) {
            std::vector<char> buf(available);
            const std::size_t read = socket.read_some(boost::asio::buffer(buf), ec);
            if (ec) {
                log_line("ERROR", "socket read failed: " + ec.message());
                return false;
            }
            buf.resize(read);
            stream.insert(stream.end(), buf.begin(), buf.end());
            if (try_extract_frame(stream, out_frame)) {
                return true;
            }
        }
    }

    return false;
}

std::optional<std::string> request_sync_json(boost::asio::ip::tcp::socket& socket,
                                             std::vector<char>& stream,
                                             std::uint32_t uuid,
                                             int timeout_ms) {
    const auto packet = build_packet(CMD_SYNC_STATE, uuid, {});
    boost::asio::write(socket, boost::asio::buffer(packet));
    log_line("SEND", "cmd=sync uuid=" + std::to_string(uuid));

    std::vector<char> frame;
    if (!wait_one_frame(socket, stream, frame, timeout_ms)) {
        log_line("WARN", "sync response timeout");
        return std::nullopt;
    }

    Header h;
    if (!parse_header(frame, h) || frame.size() < 10) {
        log_line("WARN", "invalid frame for sync response");
        return std::nullopt;
    }
    if (h.command != CMD_SYNC_STATE) {
        log_line("WARN", "unexpected response command=" + std::to_string(h.command));
    }

    std::string payload(frame.begin() + 10, frame.end());
    return payload;
}

std::optional<std::string> request_fullstate(boost::asio::ip::tcp::socket& socket,
                                              std::vector<char>& stream,
                                              std::uint32_t uuid,
                                              int timeout_ms) {
    const auto packet = build_packet(CMD_DEBUG_FULLSTATE, uuid, {});
    boost::asio::write(socket, boost::asio::buffer(packet));
    log_line("SEND", "cmd=fullstate uuid=" + std::to_string(uuid));

    std::vector<char> frame;
    if (!wait_one_frame(socket, stream, frame, timeout_ms)) {
        log_line("WARN", "fullstate response timeout");
        return std::nullopt;
    }

    Header h;
    if (!parse_header(frame, h) || frame.size() < 10) {
        log_line("WARN", "invalid frame for fullstate response");
        return std::nullopt;
    }
    if (h.command != CMD_DEBUG_FULLSTATE) {
        log_line("WARN", "unexpected response command=" + std::to_string(h.command));
    }

    std::string payload(frame.begin() + 10, frame.end());
    return payload;
}

const PetView* find_on_stage_pet(const SyncView& view, int player) {
    if (player < 0 || player > 1) {
        return nullptr;
    }
    for (const auto& pet : view.party[player]) {
        if (pet.on_stage) {
            return &pet;
        }
    }
    return nullptr;
}

void print_pet_detail(const std::string& prefix, const PetView& pet) {
    std::ostringstream line;
    line << prefix << " [slot=" << pet.slot << "] "
         << pet.name << "(" << pet.id << ")"
         << " hp=" << pet.hp << "/" << pet.max_hp;
    log_line("INFO", line.str());
    for (std::size_t i = 0; i < pet.skills.size(); ++i) {
        const auto& s = pet.skills[i];
        std::ostringstream sk;
        sk << "  skill" << i << " " << s.name << "(" << s.id << ") pp=" << s.pp << "/" << s.max_pp;
        log_line("INFO", sk.str());
    }
}

void print_status_summary(const SyncView& view) {
    const int display_round = view.round + 1;
    std::ostringstream line;
    line << "status uuid=" << view.uuid
         << " round=" << display_round << "(internal=" << view.round << ")"
         << " state=" << view.state_name << "(" << view.state << ")"
         << " needInput=" << (view.need_input ? "true" : "false")
         << " inputPlayer=" << view.input_player;
    log_line("INFO", line.str());

    if (view.need_death_switch0 || view.need_death_switch1) {
        log_line("INFO", "death switch required: player0="
            + std::string(view.need_death_switch0 ? "true" : "false")
            + " player1=" + std::string(view.need_death_switch1 ? "true" : "false"));
    }

    const PetView* p0 = find_on_stage_pet(view, 0);
    const PetView* p1 = find_on_stage_pet(view, 1);
    if (p0) {
        log_line("INFO", "onStage player0=" + p0->name + " hp=" + std::to_string(p0->hp) + "/" + std::to_string(p0->max_hp));
    }
    if (p1) {
        log_line("INFO", "onStage player1=" + p1->name + " hp=" + std::to_string(p1->hp) + "/" + std::to_string(p1->max_hp));
    }
}

void print_payload_as_text(const std::vector<char>& payload) {
    std::string text(payload.begin(), payload.end());
    log_line("RECV", "payload=" + text);
}

void print_received(const std::vector<char>& raw) {
    Header h;
    if (!parse_header(raw, h) || h.total_length != raw.size()) {
        std::string text(raw.begin(), raw.end());
        log_line("RECV", "plain=" + text);
        return;
    }

    std::vector<char> payload;
    if (raw.size() > 10) {
        payload.assign(raw.begin() + 10, raw.end());
    }
    std::ostringstream oss;
    oss << "frame cmd=" << h.command << " uuid=" << h.uuid << " payload_len=" << payload.size();
    log_line("RECV", oss.str());
    if (!payload.empty()) {
        print_payload_as_text(payload);
    }
}

void poll_once(boost::asio::ip::tcp::socket& socket, std::vector<char>& stream, int timeout_ms) {
    std::vector<char> frame;
    if (!wait_one_frame(socket, stream, frame, timeout_ms)) {
        log_line("WARN", "poll timeout");
        return;
    }
    print_received(frame);
}

void print_help() {
    std::cout
        << "commands:\n"
        << "  help\n"
        << "  quit|exit\n"
        << "  use <uuid>                                   set default uuid\n"
        << "  init012 [uuid]                                init battle with preset 012 parties\n"
        << "  skill <robot> <skill_idx> [uuid]\n"
        << "  medicine <robot> <med_idx> [uuid]\n"
        << "  pet <robot> <pet_idx> [uuid]\n"
        << "  turn <r0_action> <r0_idx> <r1_action> <r1_idx> [uuid]\n"
        << "    submit both actions for this round. action: skill|medicine|pet\n"
        << "  mode step [uuid]                              enable single-step debug mode\n"
        << "  mode run [uuid]                               enable continuous run mode\n"
        << "  sync [uuid]                                   request SYNC_STATE\n"
        << "  status [uuid]                                 summary from SYNC_STATE\n"
        << "  check                                         check on-stage both players\n"
        << "  check <player>                                check one player's party\n"
        << "  check <player,slot>                           check one party pet detail\n"
        << "  heartbeat [uuid]\n"
        << "  debug step [uuid]                             single-step one state forward\n"
        << "  debug break <state_id> [uuid]                 toggle breakpoint at state\n"
        << "  debug breaks [uuid]                           list breakpoints (via fullstate)\n"
        << "  debug full [uuid]                             get full state dump\n"
        << "  raw <cmd> <uuid> [int1 int2 ...]             send custom command with int payload\n"
        << "  poll [timeout_ms]                             read one response packet\n";
}

int action_to_cmd(const std::string& action) {
    if (action == "skill") return CMD_SELECT_SKILL;
    if (action == "medicine") return CMD_USE_MEDICINE;
    if (action == "pet") return CMD_CHOOSE_PET;
    return -1;
}

int action_to_type(const std::string& action) {
    if (action == "skill") return 1;
    if (action == "medicine") return 2;
    if (action == "pet") return 0;
    return -1;
}

} // namespace

int main() {
    try {
        boost::asio::io_context io;
        boost::asio::ip::tcp::socket socket(io);
        socket.connect({boost::asio::ip::make_address(kDefaultHost), static_cast<unsigned short>(kDefaultPort)});

        std::uint32_t default_uuid = 1;
          std::vector<char> stream;
        log_line("INFO", std::string("connected to ") + kDefaultHost + ":" + std::to_string(kDefaultPort));
        log_line("INFO", "default uuid=1");
        print_help();

        std::string line;
        while (std::cout << "cli> " && std::getline(std::cin, line)) {
            if (line.empty()) {
                continue;
            }

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            if (cmd == "quit" || cmd == "exit") {
                break;
            }
            if (cmd == "help") {
                print_help();
                continue;
            }
            if (cmd == "use") {
                std::uint32_t u = 0;
                if (!(iss >> u)) {
                    log_line("WARN", "usage: use <uuid>");
                    continue;
                }
                default_uuid = u;
                log_line("INFO", "default uuid set to " + std::to_string(default_uuid));
                continue;
            }

            std::uint32_t uuid = default_uuid;
            std::vector<char> packet;

            if (cmd == "init012") {
                if (iss.peek() != EOF) {
                    iss >> uuid;
                }
                packet = build_packet(CMD_INIT_BATTLE, uuid, build_init_012_body());
            } else if (cmd == "skill" || cmd == "medicine" || cmd == "pet") {
                int robot = 0;
                int idx = 0;
                if (!(iss >> robot >> idx)) {
                    log_line("WARN", "usage: " + cmd + " <robot> <idx> [uuid]");
                    continue;
                }
                if (iss.peek() != EOF) {
                    iss >> uuid;
                }
                const int wire_cmd = action_to_cmd(cmd);
                const int action_type = action_to_type(cmd);
                packet = build_packet(static_cast<std::uint16_t>(wire_cmd), uuid, build_action_body(robot, action_type, idx));
            } else if (cmd == "turn") {
                std::string a0;
                int i0 = 0;
                std::string a1;
                int i1 = 0;
                if (!(iss >> a0 >> i0 >> a1 >> i1)) {
                    log_line("WARN", "usage: turn <r0_action> <r0_idx> <r1_action> <r1_idx> [uuid]");
                    continue;
                }
                if (iss.peek() != EOF) {
                    iss >> uuid;
                }

                const int c0 = action_to_cmd(a0);
                const int c1 = action_to_cmd(a1);
                const int t0 = action_to_type(a0);
                const int t1 = action_to_type(a1);
                if (c0 < 0 || c1 < 0) {
                    log_line("WARN", "turn action must be skill|medicine|pet");
                    continue;
                }

                const auto p0 = build_packet(static_cast<std::uint16_t>(c0), uuid, build_action_body(0, t0, i0));
                const auto p1 = build_packet(static_cast<std::uint16_t>(c1), uuid, build_action_body(1, t1, i1));
                boost::asio::write(socket, boost::asio::buffer(p0));
                log_line("SEND", "turn r0: " + a0 + " " + std::to_string(i0));
                boost::asio::write(socket, boost::asio::buffer(p1));
                log_line("SEND", "turn r1: " + a1 + " " + std::to_string(i1));

                const auto payload = request_sync_json(socket, stream, uuid, 500);
                if (!payload) continue;
                if (print_sync_error_if_any(*payload)) continue;
                SyncView view;
                if (!parse_sync_view(*payload, view)) {
                    log_line("WARN", "failed to parse sync payload after turn");
                    continue;
                }
                print_status_summary(view);
                continue;
            } else if (cmd == "mode") {
                std::string mode_name;
                if (!(iss >> mode_name)) {
                    log_line("WARN", "usage: mode <step|run> [uuid]");
                    continue;
                }
                if (iss.peek() != EOF) iss >> uuid;
                if (mode_name == "step") {
                    packet = build_packet(CMD_DEBUG_STEP, uuid, {});
                    log_line("INFO", "mode set to step (single-step), uuid=" + std::to_string(uuid));
                } else if (mode_name == "run") {
                    packet = build_packet(CMD_DEBUG_CONTINUE, uuid, {});
                    log_line("INFO", "mode set to run (continuous), uuid=" + std::to_string(uuid));
                } else {
                    log_line("WARN", "unknown mode: " + mode_name + " (valid: step, run)");
                    continue;
                }
                boost::asio::write(socket, boost::asio::buffer(packet));
                poll_once(socket, stream, 200);
                continue;
            } else if (cmd == "sync") {
                if (iss.peek() != EOF) {
                    iss >> uuid;
                }
                  const auto payload = request_sync_json(socket, stream, uuid, 500);
                  if (payload) {
                      if (print_sync_error_if_any(*payload)) {
                          continue;
                      }
                      log_line("RECV", "sync payload=" + *payload);
                  }
                  continue;
              } else if (cmd == "status") {
                  if (iss.peek() != EOF) {
                      iss >> uuid;
                  }
                  const auto payload = request_sync_json(socket, stream, uuid, 500);
                  if (!payload) {
                      continue;
                  }
                  if (print_sync_error_if_any(*payload)) {
                      continue;
                  }
                  SyncView view;
                  if (!parse_sync_view(*payload, view)) {
                      log_line("WARN", "failed to parse sync payload");
                      continue;
                  }
                  print_status_summary(view);
                  continue;
              } else if (cmd == "check") {
                  const auto payload = request_sync_json(socket, stream, uuid, 500);
                  if (!payload) {
                      continue;
                  }
                  if (print_sync_error_if_any(*payload)) {
                      continue;
                  }
                  SyncView view;
                  if (!parse_sync_view(*payload, view)) {
                      log_line("WARN", "failed to parse sync payload");
                      continue;
                  }

                  std::string arg;
                  if (!(iss >> arg)) {
                      const PetView* p0 = find_on_stage_pet(view, 0);
                      const PetView* p1 = find_on_stage_pet(view, 1);
                      if (p0) {
                          print_pet_detail("player0 on-stage", *p0);
                      }
                      if (p1) {
                          print_pet_detail("player1 on-stage", *p1);
                      }
                      continue;
                  }

                  const std::size_t comma = arg.find(',');
                  if (comma == std::string::npos) {
                      int player = -1;
                      try {
                          player = std::stoi(arg);
                      } catch (...) {
                          log_line("WARN", "usage: check | check <player> | check <player,slot>");
                          continue;
                      }
                      if (player < 0 || player > 1) {
                          log_line("WARN", "player must be 0 or 1");
                          continue;
                      }
                      for (const auto& pet : view.party[player]) {
                          std::ostringstream line;
                          line << "player" << player << " slot=" << pet.slot
                               << " " << pet.name
                               << " hp=" << pet.hp << "/" << pet.max_hp;
                          if (pet.on_stage) {
                              line << " [on-stage]";
                          }
                          log_line("INFO", line.str());
                      }
                      continue;
                  }

                  int player = -1;
                  int slot = -1;
                  try {
                      player = std::stoi(arg.substr(0, comma));
                      slot = std::stoi(arg.substr(comma + 1));
                  } catch (...) {
                      log_line("WARN", "usage: check <player,slot>");
                      continue;
                  }
                  if (player < 0 || player > 1 || slot < 0 || slot > 5) {
                      log_line("WARN", "player must be 0/1 and slot must be 0..5");
                      continue;
                  }

                  const auto& party = view.party[player];
                  const auto it = std::find_if(party.begin(), party.end(), [slot](const PetView& p) {
                      return p.slot == slot;
                  });
                  if (it == party.end()) {
                      log_line("WARN", "pet slot not found in sync payload");
                      continue;
                  }
                  print_pet_detail("player" + std::to_string(player), *it);
                  continue;
            } else if (cmd == "heartbeat") {
                if (iss.peek() != EOF) {
                    iss >> uuid;
                }
                packet = build_packet(CMD_HEARTBEAT, uuid, {});
            } else if (cmd == "debug") {
                std::string sub;
                if (!(iss >> sub)) {
                    log_line("WARN", "usage: debug <step|continue|break|breaks|full> [args] [uuid]");
                    continue;
                }
                if (sub == "step") {
                    if (iss.peek() != EOF) iss >> uuid;
                    packet = build_packet(CMD_DEBUG_STEP, uuid, {});
                    boost::asio::write(socket, boost::asio::buffer(packet));
                    log_line("SEND", "debug step uuid=" + std::to_string(uuid));
                    // Step后自动sync看结果
                    const auto payload = request_sync_json(socket, stream, uuid, 500);
                    if (!payload) continue;
                    if (print_sync_error_if_any(*payload)) continue;
                    SyncView view;
                    if (parse_sync_view(*payload, view)) print_status_summary(view);
                    else log_line("RECV", "sync payload=" + *payload);
                    continue;
                } else if (sub == "continue" || sub == "cont") {
                    if (iss.peek() != EOF) iss >> uuid;
                    packet = build_packet(CMD_DEBUG_CONTINUE, uuid, {});
                    boost::asio::write(socket, boost::asio::buffer(packet));
                    log_line("SEND", "debug continue uuid=" + std::to_string(uuid));
                    poll_once(socket, stream, 200);
                    continue;
                } else if (sub == "break") {
                    int state_id = -1;
                    if (!(iss >> state_id)) {
                        log_line("WARN", "usage: debug break <state_id> [uuid]");
                        continue;
                    }
                    if (iss.peek() != EOF) iss >> uuid;
                    packet = build_packet(CMD_DEBUG_BREAKPOINT, uuid, build_int_body({state_id}));
                    boost::asio::write(socket, boost::asio::buffer(packet));
                    log_line("SEND", "debug break state=" + std::to_string(state_id) + " uuid=" + std::to_string(uuid));
                    poll_once(socket, stream, 200);
                    continue;
                } else if (sub == "breaks") {
                    if (iss.peek() != EOF) iss >> uuid;
                    const auto payload = request_fullstate(socket, stream, uuid, 1000);
                    if (payload) {
                        const auto bps = extract_string_field(*payload, "\"breakpoints\":[");
                        // simple parse: find breakpoints array
                        std::string bp_arr;
                        if (extract_array_after_key(*payload, "\"breakpoints\":", bp_arr)) {
                            log_line("INFO", "breakpoints: " + bp_arr);
                        }
                    }
                    continue;
                } else if (sub == "full") {
                    if (iss.peek() != EOF) iss >> uuid;
                    const auto payload = request_fullstate(socket, stream, uuid, 2000);
                    if (payload) {
                        log_line("RECV", "fullstate=" + *payload);
                    }
                    continue;
                } else {
                    log_line("WARN", "unknown debug sub-command: " + sub);
                    continue;
                }
            } else if (cmd == "raw") {
                int c = 0;
                if (!(iss >> c >> uuid)) {
                    log_line("WARN", "usage: raw <cmd> <uuid> [int1 int2 ...]");
                    continue;
                }
                std::vector<int> values;
                int v = 0;
                while (iss >> v) {
                    values.push_back(v);
                }
                packet = build_packet(static_cast<std::uint16_t>(c), uuid, build_int_body(values));
            } else if (cmd == "poll") {
                int timeout = 200;
                if (iss.peek() != EOF) {
                    iss >> timeout;
                }
                  poll_once(socket, stream, timeout);
                continue;
            } else {
                log_line("WARN", "unknown command, type help");
                continue;
            }

            boost::asio::write(socket, boost::asio::buffer(packet));
            log_line("SEND", "cmd=" + cmd + " uuid=" + std::to_string(uuid));
              poll_once(socket, stream, 200);
        }
    } catch (const std::exception& ex) {
        log_line("ERROR", std::string("training_cli error: ") + ex.what());
        return 1;
    }

    return 0;
}
