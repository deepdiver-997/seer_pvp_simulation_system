#include <fsm/iControlBlock.h>
#include <fsm/battleContext.h>
#include <fsm/battleFsm.h>
#include <entities/pet_factory.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace {

std::string ts_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::ostringstream oss;
    oss << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void log_cb(const std::string& level, const std::string& msg) {
    std::cout << "[" << ts_now() << "] [" << level << "] [control] " << msg << std::endl;
}

void append_bytes(std::vector<char>& buffer, const char* data, std::size_t bytes_transferred) {
    buffer.insert(buffer.end(), data, data + bytes_transferred);
}

bool decode_action_payload(const std::vector<char>& payload, int& robot_id, int& action_type, int& index) {
    if (payload.size() < sizeof(int) * 3) {
        return false;
    }
    int values[3] = {0, 0, 0};
    std::memcpy(values, payload.data(), sizeof(values));
    robot_id = values[0];
    action_type = values[1];
    index = values[2];
    return true;
}

std::string action_type_name(int action_type) {
    switch (action_type) {
        case 0: return "choose_pet";
        case 1: return "skill";
        case 2: return "medicine";
        default: return "unknown";
    }
}

std::string describe_action(const BattleContext* ctx, int robot_id, int action_type, int index) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return "invalid action";
    }

    const int slot = ctx->on_stage[robot_id];
    if (slot < 0 || slot >= 6) {
        return "invalid on_stage";
    }

    std::ostringstream oss;
    const ElfPet& on_stage_pet = ctx->seerRobot[robot_id].elfPets[slot];
    const std::string action_name = action_type_name(action_type);

    oss << "robot=" << robot_id
        << " pet=" << on_stage_pet.name
        << " action=" << action_name
        << " index=" << index;

    if (action_type == 1 && index >= 0 && index < 5) {
        const Skills& skill = on_stage_pet.skills[index];
        oss << " skill=" << skill.name << "(" << skill.id << ")";
    } else if (action_type == 0 && index >= 0 && index < 6) {
        const ElfPet& target_pet = ctx->seerRobot[robot_id].elfPets[index];
        oss << " target_pet=" << target_pet.name;
    }

    return oss.str();
}

bool initialize_battle_from_message(IControlBlock* control_block, const ParsedMessage& msg) {
    if (!control_block || !control_block->get_fsm()) {
        log_cb("ERROR", "battle fsm is not ready");
        return false;
    }

    try {
        const BattleCreateRequest request = decode_battle_create_request(msg.params);
        SeerRobot robots[2] = {
            SeerRobotFactory::create_robot(request.side1),
            SeerRobotFactory::create_robot(request.side2),
        };

        auto ctx = std::make_unique<BattleContext>(control_block, robots);
        if (msg.uuid != 0) {
            ctx->uuid = msg.uuid;
        }

        control_block->register_context(std::move(ctx));
        BattleContext* registered_ctx = control_block->get_context();
        if (!registered_ctx) {
            log_cb("ERROR", "failed to register battle context");
            return false;
        }

        log_cb("INFO", "battle initialized, uuid=" + std::to_string(registered_ctx->uuid));

        control_block->get_fsm()->post([registered_ctx, fsm = control_block->get_fsm().get()]() {
            fsm->run(registered_ctx);
        });
        return true;
    } catch (const std::exception& ex) {
        log_cb("ERROR", std::string("failed to initialize battle: ") + ex.what());
        return false;
    }
}

} // namespace

// TrainingControlBlock
TrainingControlBlock::TrainingControlBlock(int match_id, std::unique_ptr<boost::asio::ip::tcp::socket> socket)
    : match_id_(match_id), socket_(std::move(socket)) {
    buffer_.reserve(4096);
}

TrainingControlBlock::~TrainingControlBlock() = default;

void TrainingControlBlock::wait_for_input(BattleContext* ctx) {
    bool should_dispatch = false;
    std::vector<char> input;

    {
        std::lock_guard<std::mutex> lock(pending_inputs_mutex_);
        // 设置当前等待的 context
        waiting_uuid_ = ctx->uuid;

        auto it = pending_inputs_.find(ctx->uuid);
        if (it != pending_inputs_.end() && !it->second.empty() && ctx->is_empty) {
            input = std::move(it->second.front());
            it->second.pop_front();
            if (it->second.empty()) {
                pending_inputs_.erase(it);
            }
            waiting_uuid_ = 0;
            should_dispatch = true;
        }
    }

    if (!should_dispatch) {
        return;
    }

    ctx->m_buffer = std::move(input);
    ctx->is_empty = false;
    log_cb("INFO", "dispatched queued input, uuid=" + std::to_string(ctx->uuid));

    fsm_->post([ctx, fsm = fsm_.get()]() {
        fsm->run(ctx);
    });
}

void TrainingControlBlock::start_read_loop() {
    // 启动异步读取循环，永不停止
    // server 管理所有控制块指针所有权，确保生命周期覆盖整个训练过程，因此这里直接用raw指针
    socket_->async_read_some(boost::asio::buffer(read_chunk_),
        [this](boost::system::error_code ec, std::size_t bytes_transferred) {
            if (ec) {
                log_cb("ERROR", std::string("training read error: ") + ec.message());
                return;
            }

            if (bytes_transferred > 0) {
                append_bytes(buffer_, read_chunk_.data(), bytes_transferred);
            }

            // 保护：超过上限就清空重置，防止恶意客户端撑爆内存
            if (buffer_.size() > 1024 * 1024) {  // 1MB 上限
                buffer_.clear();
                log_cb("WARN", "training buffer overflow, cleared");
                this->start_read_loop();
                return;
            }

            // 循环处理 buffer 中可能存在的多个完整消息
            while (this->has_complete_message()) {
                auto msg = this->parse_message();
                this->handle_message(msg);
                // 继续循环检查是否还有完整消息
            }

            // 继续读取
            this->start_read_loop();
        });
}

bool TrainingControlBlock::has_complete_message() const {
    if (buffer_.size() < 10) return false;
    auto header = parse_header(buffer_);
    // 检查总长度是否已接收
    return buffer_.size() >= header.total_length;
}

ParsedMessage TrainingControlBlock::parse_message() {
    ParsedMessage msg = {0, Command::INVALID, {}};

    auto header = parse_header(buffer_);
    if (header.total_length < 10 || buffer_.size() < header.total_length) {
        return msg;
    }

    msg.uuid = header.uuid;
    msg.command = static_cast<Command>(header.command);

    size_t param_len = header.total_length - 10;
    if (param_len > 0) {
        msg.params.resize(param_len);
        std::memcpy(msg.params.data(), buffer_.data() + 10, param_len);
    }

    // 移除已处理的消息
    buffer_.erase(buffer_.begin(), buffer_.begin() + header.total_length);

    return msg;
}

void TrainingControlBlock::handle_message(const ParsedMessage& msg) {
    // 命令执行在路由之前
    switch (msg.command) {
        case Command::INIT_BATTLE:
            if (!contexts_.empty()) {
                log_cb("WARN", "battle already initialized");
                return;
            }
            initialize_battle_from_message(this, msg);
            return;
        case Command::DUPLICATE_CONTEXT: {
            log_cb("WARN", "duplicate_context is not supported");
            return;  // 复制命令不触发 run
        }
        case Command::HEARTBEAT:
            // 心跳不触发 run，直接响应
            return;
        case Command::DEBUG_STEP: {
            auto it = contexts_.find(msg.uuid);
            if (it == contexts_.end()) {
                log_cb("WARN", "debug_step context not found, uuid=" + std::to_string(msg.uuid));
                return;
            }
            BattleContext* ctx = it->second.get();
            ctx->debug_step_mode = true;
            log_cb("INFO", "debug_step mode enabled, uuid=" + std::to_string(msg.uuid));
            // 触发一次 run 执行当前状态
            fsm_->post([ctx, fsm = fsm_.get()]() {
                fsm->run(ctx);
            });
            return;
        }
        case Command::DEBUG_CONTINUE: {
            auto it = contexts_.find(msg.uuid);
            if (it == contexts_.end()) {
                log_cb("WARN", "debug_continue context not found, uuid=" + std::to_string(msg.uuid));
                return;
            }
            BattleContext* ctx = it->second.get();
            ctx->debug_step_mode = false;
            log_cb("INFO", "debug_continue: step mode disabled, uuid=" + std::to_string(msg.uuid));
            // 恢复连续执行
            fsm_->post([ctx, fsm = fsm_.get()]() {
                fsm->run(ctx);
            });
            return;
        }
        case Command::DEBUG_BREAKPOINT: {
            auto it = contexts_.find(msg.uuid);
            if (it == contexts_.end()) {
                log_cb("WARN", "debug_breakpoint context not found, uuid=" + std::to_string(msg.uuid));
                return;
            }
            BattleContext* ctx = it->second.get();
            if (msg.params.size() >= sizeof(int)) {
                int state_val = 0;
                std::memcpy(&state_val, msg.params.data(), sizeof(int));
                // toggle: if exists remove, else add
                if (ctx->breakpoints.count(state_val)) {
                    ctx->breakpoints.erase(state_val);
                    log_cb("INFO", "breakpoint removed: state=" + std::to_string(state_val)
                        + " uuid=" + std::to_string(msg.uuid));
                } else {
                    ctx->breakpoints.insert(state_val);
                    log_cb("INFO", "breakpoint added: state=" + std::to_string(state_val)
                        + " uuid=" + std::to_string(msg.uuid));
                }
            } else {
                // 无参数则列出所有断点
                std::ostringstream oss;
                oss << "breakpoints for uuid=" << msg.uuid << ": [";
                bool first = true;
                for (int bp : ctx->breakpoints) {
                    if (!first) oss << ",";
                    first = false;
                    oss << bp;
                }
                oss << "]";
                log_cb("INFO", oss.str());
            }
            return;
        }
        case Command::DEBUG_FULLSTATE: {
            auto it = contexts_.find(msg.uuid);
            if (it == contexts_.end()) {
                log_cb("WARN", "debug_fullstate context not found, uuid=" + std::to_string(msg.uuid));
                const std::string err = "{\"error\":\"context not found\"}";
                auto response = std::make_shared<std::vector<char>>(
                    build_message(Command::DEBUG_FULLSTATE, msg.uuid, err.c_str(), err.size()));
                boost::asio::async_write(*socket_, boost::asio::buffer(*response),
                    [response](boost::system::error_code ec, std::size_t) {
                        if (ec) log_cb("ERROR", std::string("fullstate write error: ") + ec.message());
                    });
                return;
            }
            BattleContext* ctx = it->second.get();
            std::string json = ctx->getFullStateJson();
            auto response = std::make_shared<std::vector<char>>(
                build_message(Command::DEBUG_FULLSTATE, msg.uuid, json.c_str(), json.size()));
            boost::asio::async_write(*socket_, boost::asio::buffer(*response),
                [response](boost::system::error_code ec, std::size_t) {
                    if (ec) log_cb("ERROR", std::string("fullstate write error: ") + ec.message());
                });
            return;
        }
        case Command::SYNC_STATE: {
            // 同步状态请求 - 查找 context 并返回 JSON 状态
            auto it = contexts_.find(msg.uuid);
            if (it == contexts_.end()) {
                log_cb("WARN", "sync_state context not found, uuid=" + std::to_string(msg.uuid));
                  const std::string err = "{\"error\":\"context not found\",\"uuid\":" + std::to_string(msg.uuid) + "}";
                  auto response = std::make_shared<std::vector<char>>(build_message(Command::SYNC_STATE, msg.uuid, err.c_str(), err.size()));
                  boost::asio::async_write(*socket_, boost::asio::buffer(*response),
                      [response](boost::system::error_code ec, std::size_t) {
                          if (ec) {
                              log_cb("ERROR", std::string("sync_state error write failed: ") + ec.message());
                          }
                      });
                return;
            }
            BattleContext* ctx = it->second.get();
            std::string json = ctx->getStateJson();
            // 构建响应消息
            auto response = std::make_shared<std::vector<char>>(build_message(Command::SYNC_STATE, msg.uuid, json.c_str(), json.size()));
            // 发送响应
            boost::asio::async_write(*socket_, boost::asio::buffer(*response),
                [response](boost::system::error_code ec, std::size_t) {
                    if (ec) {
                        log_cb("ERROR", std::string("sync_state write error: ") + ec.message());
                    }
                });
            return;
        }
        default:
            break;
    }

    // 路由到对应 context 并启动 run
    route_and_run(msg);
}

void TrainingControlBlock::route_and_run(const ParsedMessage& msg) {
    auto it = contexts_.find(msg.uuid);
    if (it == contexts_.end()) {
        log_cb("WARN", "context not found, uuid=" + std::to_string(msg.uuid));
        return;
    }

    auto& ctx = it->second;

    const bool is_action_command =
        msg.command == Command::SELECT_SKILL ||
        msg.command == Command::USE_MEDICINE ||
        msg.command == Command::CHOOSE_PET;

    if (is_action_command) {
        std::size_t queue_size = 0;
        uint32_t waiting_uuid_snapshot = 0;
        bool should_wake = false;

        {
            std::lock_guard<std::mutex> lock(pending_inputs_mutex_);
            auto& q = pending_inputs_[msg.uuid];
            q.push_back(msg.params);
            queue_size = q.size();
            waiting_uuid_snapshot = waiting_uuid_;
            // 若已在等待且这是队列首条，唤醒一次即可。
            should_wake = (waiting_uuid_ == msg.uuid && q.size() == 1);
        }

        int robot_id = -1;
        int action_type = -1;
        int index = -1;
        std::string detail;
        if (decode_action_payload(msg.params, robot_id, action_type, index)) {
            detail = " " + describe_action(ctx.get(), robot_id, action_type, index);
        }

        log_cb("INFO", "input queued, uuid=" + std::to_string(msg.uuid)
            + " queue_size=" + std::to_string(queue_size)
            + " waiting=" + std::to_string(waiting_uuid_snapshot)
            + detail);

        // 当 FSM 正在等待该 uuid 输入时，触发一次 run，让 wait_for_input 派发队列头。
        if (should_wake) {
            fsm_->post([ctx = ctx.get(), fsm = fsm_.get()]() {
                fsm->run(ctx);
            });
        }
        return;
    }

    // 如果这个 context 正在等待输入，处理数据
    uint32_t waiting_uuid_snapshot = 0;
    {
        std::lock_guard<std::mutex> lock(pending_inputs_mutex_);
        waiting_uuid_snapshot = waiting_uuid_;
    }

    if (waiting_uuid_snapshot == msg.uuid && ctx->is_empty) {
        // 复制参数到 context buffer
        ctx->m_buffer = msg.params;
        ctx->is_empty = false;
        {
            std::lock_guard<std::mutex> lock(pending_inputs_mutex_);
            waiting_uuid_ = 0;  // 清除等待状态
        }

        int robot_id = -1;
        int action_type = -1;
        int index = -1;
        if (decode_action_payload(msg.params, robot_id, action_type, index)) {
            log_cb("INFO", "context writeback accepted, uuid=" + std::to_string(msg.uuid)
                + " " + describe_action(ctx.get(), robot_id, action_type, index));
        } else {
            log_cb("INFO", "context writeback accepted, uuid=" + std::to_string(msg.uuid)
                + " payload_bytes=" + std::to_string(msg.params.size()));
        }

        // 启动 run
        fsm_->post([ctx = ctx.get(), fsm = fsm_.get()]() {
            fsm->run(ctx);
        });
    } else {
        log_cb("WARN", "message dropped, uuid=" + std::to_string(msg.uuid)
            + " waiting=" + std::to_string(waiting_uuid_snapshot)
            + " is_empty=" + std::to_string(ctx->is_empty ? 1 : 0));
    }
}

void TrainingControlBlock::async_write(int player_id, const std::string& data,
                                       BattleContext* ctx, BattleFsm* fsm) {
    auto payload = std::make_shared<std::string>(data);
    boost::asio::async_write(*socket_, boost::asio::buffer(*payload),
        [ctx, fsm, payload](boost::system::error_code ec, std::size_t) {
            if (ec) {
                log_cb("ERROR", std::string("training write error: ") + ec.message());
                return;
            }
            fsm->post([ctx, fsm]() {
                fsm->run(ctx);
            });
        });
}

void TrainingControlBlock::broadcast(const std::string& data) {
    auto payload = std::make_shared<std::string>(data);
    boost::asio::async_write(*socket_, boost::asio::buffer(*payload),
        [payload](boost::system::error_code ec, std::size_t) {
            if (ec) {
                log_cb("ERROR", std::string("training broadcast error: ") + ec.message());
            }
        });
}

void TrainingControlBlock::broadcast_to(const std::vector<int>& player_ids, const std::string& data) {
    broadcast(data);  // 单 socket 模式
}

void TrainingControlBlock::log_operation(const std::string& op) {
#ifdef BATTLE_OP_LOGGING
    operation_log_ += op;
    operation_log_ += "|";
#endif
}

std::string TrainingControlBlock::get_operation_log() const {
    return operation_log_;
}

std::unique_ptr<BattleContext> TrainingControlBlock::duplicate_context(uint32_t uuid) {
    (void)uuid;
    return nullptr;
}

void TrainingControlBlock::register_context(std::unique_ptr<BattleContext> ctx) {
    contexts_[ctx->uuid] = std::move(ctx);
}

std::unique_ptr<BattleContext> TrainingControlBlock::unregister_context(uint32_t uuid) {
    auto it = contexts_.find(uuid);
    if (it == contexts_.end()) {
        return nullptr;
    }
    auto ctx = std::move(it->second);
    contexts_.erase(it);
    return ctx;
}

BattleContext* TrainingControlBlock::get_context() {
    // 返回第一个 context（训练模式只有一个）
    if (contexts_.empty()) return nullptr;
    return contexts_.begin()->second.get();
}

// BattleControlBlock
BattleControlBlock::BattleControlBlock(int match_id,
                                       std::unique_ptr<boost::asio::ip::tcp::socket> socket1,
                                       std::unique_ptr<boost::asio::ip::tcp::socket> socket2)
    : match_id_(match_id), sockets_{std::move(socket1), std::move(socket2)} {
    buffers_[0].reserve(4096);
    buffers_[1].reserve(4096);
}

BattleControlBlock::~BattleControlBlock() = default;

void BattleControlBlock::wait_for_input(BattleContext* ctx) {
    // 设置当前等待的 context 和 socket
    waiting_uuid_[ctx->current_player_id_] = ctx->uuid;
}

void BattleControlBlock::start_read_loop(int player_id) {
    auto socket = sockets_[player_id].get();

    socket->async_read_some(boost::asio::buffer(read_chunks_[player_id]),
        [this, player_id](boost::system::error_code ec, std::size_t bytes_transferred) {
            if (ec) {
                log_cb("ERROR", std::string("battle read error: ") + ec.message());
                return;
            }

            if (bytes_transferred > 0) {
                append_bytes(buffers_[player_id], read_chunks_[player_id].data(), bytes_transferred);
            }

            // 保护：超过上限就清空重置
            if (buffers_[player_id].size() > 1024 * 1024) {
                buffers_[player_id].clear();
                log_cb("WARN", "battle buffer overflow, cleared");
                this->start_read_loop(player_id);
                return;
            }

            while (this->has_complete_message()) {
                auto msg = this->parse_message();
                this->handle_message(msg);
            }

            this->start_read_loop(player_id);
        });
}

bool BattleControlBlock::has_complete_message() const {
    if (buffers_[0].size() < 10 && buffers_[1].size() < 10) return false;
    // 需要分别检查两个 buffer
    for (int i = 0; i < 2; ++i) {
        if (buffers_[i].size() >= 10) {
            auto header = parse_header(buffers_[i]);
            if (buffers_[i].size() >= header.total_length) {
                return true;
            }
        }
    }
    return false;
}

ParsedMessage BattleControlBlock::parse_message() {
    ParsedMessage msg = {0, Command::INVALID, {}};

    // 找到有完整消息的 buffer
    for (int i = 0; i < 2; ++i) {
        if (buffers_[i].size() >= 10) {
            auto header = parse_header(buffers_[i]);
            if (buffers_[i].size() >= header.total_length) {
                msg.uuid = header.uuid;
                msg.command = static_cast<Command>(header.command);

                size_t param_len = header.total_length - 10;
                if (param_len > 0) {
                    msg.params.resize(param_len);
                    std::memcpy(msg.params.data(), buffers_[i].data() + 10, param_len);
                }

                buffers_[i].erase(buffers_[i].begin(), buffers_[i].begin() + header.total_length);
                return msg;
            }
        }
    }
    return msg;
}

void BattleControlBlock::handle_message(const ParsedMessage& msg) {
    switch (msg.command) {
        case Command::INIT_BATTLE:
            if (!contexts_.empty()) {
                log_cb("WARN", "battle already initialized");
                return;
            }
            initialize_battle_from_message(this, msg);
            return;
        case Command::DUPLICATE_CONTEXT: {
            log_cb("WARN", "duplicate_context is not supported");
            return;
        }
        case Command::HEARTBEAT:
            return;
        case Command::DEBUG_STEP: {
            const bool is_waiting_empty = (msg.params.empty() || msg.params.size() < sizeof(int)) ? false : true;
            uint32_t target_uuid = msg.uuid;
            if (is_waiting_empty) {
                int sid = 0;
                std::memcpy(&sid, msg.params.data(), sizeof(int));
                if (sid >= 0 && sid <= 1 && waiting_uuid_[sid] != 0) {
                    target_uuid = waiting_uuid_[sid];
                }
            }
            auto it = contexts_.find(target_uuid);
            if (it != contexts_.end()) {
                it->second->debug_step_mode = true;
                fsm_->post([ctx = it->second.get(), fsm = fsm_.get()]() { fsm->run(ctx); });
            }
            return;
        }
        case Command::DEBUG_CONTINUE: {
            const bool is_waiting_empty = (msg.params.empty() || msg.params.size() < sizeof(int)) ? false : true;
            uint32_t target_uuid = msg.uuid;
            if (is_waiting_empty) {
                int sid = 0;
                std::memcpy(&sid, msg.params.data(), sizeof(int));
                if (sid >= 0 && sid <= 1 && waiting_uuid_[sid] != 0) {
                    target_uuid = waiting_uuid_[sid];
                }
            }
            auto it = contexts_.find(target_uuid);
            if (it != contexts_.end()) {
                it->second->debug_step_mode = false;
                fsm_->post([ctx = it->second.get(), fsm = fsm_.get()]() { fsm->run(ctx); });
            }
            return;
        }
        case Command::DEBUG_BREAKPOINT: {
            auto it = contexts_.find(msg.uuid);
            if (it != contexts_.end() && msg.params.size() >= sizeof(int)) {
                int state_val = 0;
                std::memcpy(&state_val, msg.params.data(), sizeof(int));
                auto& bps = it->second->breakpoints;
                if (bps.count(state_val)) bps.erase(state_val); else bps.insert(state_val);
            }
            return;
        }
        case Command::DEBUG_FULLSTATE: {
            auto it = contexts_.find(msg.uuid);
            if (it != contexts_.end()) {
                std::string json = it->second->getFullStateJson();
                auto response = std::make_shared<std::vector<char>>(
                    build_message(Command::DEBUG_FULLSTATE, msg.uuid, json.c_str(), json.size()));
                async_write(0, std::string(json.begin(), json.end()), it->second.get(), fsm_.get());
            }
            return;
        }
        default:
            break;
    }

    route_and_run(msg);
}

void BattleControlBlock::route_and_run(const ParsedMessage& msg) {
    auto it = contexts_.find(msg.uuid);
    if (it == contexts_.end()) {
        log_cb("WARN", "context not found, uuid=" + std::to_string(msg.uuid));
        return;
    }

    auto& ctx = it->second;

    // 检查是否在等待状态
    int player_id = ctx->current_player_id_;
    if (waiting_uuid_[player_id] == msg.uuid && ctx->is_empty) {
        ctx->m_buffer = msg.params;
        ctx->is_empty = false;
        waiting_uuid_[player_id] = 0;

        int action_robot = -1;
        int action_type = -1;
        int index = -1;
        if (decode_action_payload(msg.params, action_robot, action_type, index)) {
            log_cb("INFO", "context writeback accepted, uuid=" + std::to_string(msg.uuid)
                + " player=" + std::to_string(player_id) + " "
                + describe_action(ctx.get(), action_robot, action_type, index));
        } else {
            log_cb("INFO", "context writeback accepted, uuid=" + std::to_string(msg.uuid)
                + " player=" + std::to_string(player_id)
                + " payload_bytes=" + std::to_string(msg.params.size()));
        }

        fsm_->post([ctx = ctx.get(), fsm = fsm_.get()]() {
            fsm->run(ctx);
        });
    } else {
        log_cb("WARN", "message dropped, uuid=" + std::to_string(msg.uuid) + " player=" + std::to_string(player_id));
    }
}

void BattleControlBlock::async_write(int player_id, const std::string& data,
                                     BattleContext* ctx, BattleFsm* fsm) {
    auto socket = sockets_[player_id].get();
    auto payload = std::make_shared<std::string>(data);
    boost::asio::async_write(*socket, boost::asio::buffer(*payload),
        [ctx, fsm, payload](boost::system::error_code ec, std::size_t) {
            if (ec) {
                log_cb("ERROR", std::string("battle write error: ") + ec.message());
                return;
            }
            fsm->post([ctx, fsm]() {
                fsm->run(ctx);
            });
        });
}

void BattleControlBlock::broadcast(const std::string& data) {
    for (auto& socket : sockets_) {
        if (socket) {
            auto payload = std::make_shared<std::string>(data);
            boost::asio::async_write(*socket, boost::asio::buffer(*payload),
                [payload](boost::system::error_code ec, std::size_t) {
                    if (ec) {
                        log_cb("ERROR", std::string("battle broadcast error: ") + ec.message());
                    }
                });
        }
    }
}

void BattleControlBlock::broadcast_to(const std::vector<int>& player_ids, const std::string& data) {
    for (int id : player_ids) {
        if (id >= 0 && id < 2 && sockets_[id]) {
            auto payload = std::make_shared<std::string>(data);
            boost::asio::async_write(*sockets_[id], boost::asio::buffer(*payload),
                [payload](boost::system::error_code ec, std::size_t) {
                    if (ec) {
                        log_cb("ERROR", std::string("battle targeted broadcast error: ") + ec.message());
                    }
                });
        }
    }
}

void BattleControlBlock::log_operation(const std::string& op) {
#ifdef BATTLE_OP_LOGGING
    operation_log_ += op;
    operation_log_ += "|";
#endif
}

std::string BattleControlBlock::get_operation_log() const {
    return operation_log_;
}

std::unique_ptr<BattleContext> BattleControlBlock::duplicate_context(uint32_t uuid) {
    (void)uuid;
    return nullptr;
}

void BattleControlBlock::register_context(std::unique_ptr<BattleContext> ctx) {
    contexts_[ctx->uuid] = std::move(ctx);
}

std::unique_ptr<BattleContext> BattleControlBlock::unregister_context(uint32_t uuid) {
    auto it = contexts_.find(uuid);
    if (it == contexts_.end()) {
        return nullptr;
    }
    auto ctx = std::move(it->second);
    contexts_.erase(it);
    return ctx;
}

BattleContext* BattleControlBlock::get_context() {
    if (contexts_.empty()) return nullptr;
    return contexts_.begin()->second.get();
}
