#include <fsm/iControlBlock.h>
#include <fsm/battleContext.h>
#include <fsm/battleFsm.h>
#include <iostream>
#include <cstring>

// TrainingControlBlock
TrainingControlBlock::TrainingControlBlock(int match_id, std::unique_ptr<boost::asio::ip::tcp::socket> socket)
    : match_id_(match_id), socket_(std::move(socket)) {
    buffer_.reserve(4096);
}

void TrainingControlBlock::wait_for_input(BattleContext* ctx) {
    // 设置当前等待的 context
    waiting_uuid_ = ctx->uuid;
}

void TrainingControlBlock::start_read_loop() {
    // 启动异步读取循环，永不停止
    // server 管理所有控制块指针所有权，确保生命周期覆盖整个训练过程，因此这里直接用raw指针
    socket_->async_read_some(boost::asio::buffer(buffer_),
        [this](boost::system::error_code ec, std::size_t bytes_transferred) {
            if (ec) {
                std::cerr << "Read error: " << ec.message() << std::endl;
                return;
            }

            // 保护：超过上限就清空重置，防止恶意客户端撑爆内存
            if (buffer_.size() > 1024 * 1024) {  // 1MB 上限
                buffer_.clear();
                std::cerr << "Buffer overflow, clearing" << std::endl;
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
        case Command::DUPLICATE_CONTEXT: {
            // 复制战局，创建新 context
            auto it = contexts_.find(msg.uuid);
            if (it != contexts_.end()) {
                auto new_ctx = std::make_unique<BattleContext>(*(it->second));
                new_ctx->uuid = std::chrono::system_clock::now().time_since_epoch().count();
                // 新 context 默认等待输入
                new_ctx->is_empty = true;
                register_context(std::move(new_ctx));
            }
            return;  // 复制命令不触发 run
        }
        case Command::HEARTBEAT:
            // 心跳不触发 run，直接响应
            return;
        default:
            break;
    }

    // 路由到对应 context 并启动 run
    route_and_run(msg);
}

void TrainingControlBlock::route_and_run(const ParsedMessage& msg) {
    auto it = contexts_.find(msg.uuid);
    if (it == contexts_.end()) {
        std::cerr << "Context not found: " << msg.uuid << std::endl;
        return;
    }

    auto& ctx = it->second;

    // 如果这个 context 正在等待输入，处理数据
    if (waiting_uuid_ == msg.uuid && ctx->is_empty) {
        // 复制参数到 context buffer
        ctx->m_buffer = msg.params;
        ctx->is_empty = false;
        waiting_uuid_ = 0;  // 清除等待状态

        // 启动 run
        fsm_->post([ctx = ctx.get(), fsm = fsm_.get()]() {
            fsm->run(ctx);
        });
    } else {
        // context 不在等待状态，数据入队或丢弃
        std::cerr << "Message for context " << msg.uuid << " not waiting, is_empty=" << ctx->is_empty << std::endl;
    }
}

void TrainingControlBlock::async_write(int player_id, const std::string& data,
                                       BattleContext* ctx, BattleFsm* fsm) {
    boost::asio::async_write(*socket_, boost::asio::buffer(data),
        [ctx, fsm](boost::system::error_code ec, std::size_t) {
            if (ec) {
                std::cerr << "Write error: " << ec.message() << std::endl;
                return;
            }
            fsm->post([ctx, fsm]() {
                fsm->run(ctx);
            });
        });
}

void TrainingControlBlock::broadcast(const std::string& data) {
    boost::asio::async_write(*socket_, boost::asio::buffer(data),
        [](boost::system::error_code ec, std::size_t) {
            if (ec) std::cerr << "Broadcast error: " << ec.message() << std::endl;
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
    auto it = contexts_.find(uuid);
    if (it == contexts_.end()) {
        return nullptr;
    }
    auto new_ctx = std::make_unique<BattleContext>(*(it->second));
    new_ctx->uuid = std::chrono::system_clock::now().time_since_epoch().count();
    return new_ctx;
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

void BattleControlBlock::wait_for_input(BattleContext* ctx) {
    // 设置当前等待的 context 和 socket
    waiting_uuid_[ctx->current_player_id_] = ctx->uuid;
}

void BattleControlBlock::start_read_loop(int player_id) {
    auto socket = sockets_[player_id].get();

    socket->async_read_some(boost::asio::buffer(buffers_[player_id]),
        [this, player_id](boost::system::error_code ec, std::size_t bytes_transferred) {
            if (ec) {
                std::cerr << "Read error: " << ec.message() << std::endl;
                return;
            }

            // 保护：超过上限就清空重置
            if (buffers_[player_id].size() > 1024 * 1024) {
                buffers_[player_id].clear();
                std::cerr << "Buffer overflow, clearing" << std::endl;
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
        case Command::DUPLICATE_CONTEXT: {
            auto it = contexts_.find(msg.uuid);
            if (it != contexts_.end()) {
                auto new_ctx = std::make_unique<BattleContext>(*(it->second));
                new_ctx->uuid = std::chrono::system_clock::now().time_since_epoch().count();
                new_ctx->is_empty = true;
                register_context(std::move(new_ctx));
            }
            return;
        }
        case Command::HEARTBEAT:
            return;
        default:
            break;
    }

    route_and_run(msg);
}

void BattleControlBlock::route_and_run(const ParsedMessage& msg) {
    auto it = contexts_.find(msg.uuid);
    if (it == contexts_.end()) {
        std::cerr << "Context not found: " << msg.uuid << std::endl;
        return;
    }

    auto& ctx = it->second;

    // 检查是否在等待状态
    int player_id = ctx->current_player_id_;
    if (waiting_uuid_[player_id] == msg.uuid && ctx->is_empty) {
        ctx->m_buffer = msg.params;
        ctx->is_empty = false;
        waiting_uuid_[player_id] = 0;

        fsm_->post([ctx = ctx.get(), fsm = fsm_.get()]() {
            fsm->run(ctx);
        });
    } else {
        std::cerr << "Message for context " << msg.uuid << " not waiting" << std::endl;
    }
}

void BattleControlBlock::async_write(int player_id, const std::string& data,
                                     BattleContext* ctx, BattleFsm* fsm) {
    auto socket = sockets_[player_id].get();
    boost::asio::async_write(*socket, boost::asio::buffer(data),
        [ctx, fsm](boost::system::error_code ec, std::size_t) {
            if (ec) {
                std::cerr << "Write error: " << ec.message() << std::endl;
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
            boost::asio::async_write(*socket, boost::asio::buffer(data),
                [](boost::system::error_code ec, std::size_t) {
                    if (ec) std::cerr << "Broadcast error: " << ec.message() << std::endl;
                });
        }
    }
}

void BattleControlBlock::broadcast_to(const std::vector<int>& player_ids, const std::string& data) {
    for (int id : player_ids) {
        if (id >= 0 && id < 2 && sockets_[id]) {
            boost::asio::async_write(*sockets_[id], boost::asio::buffer(data),
                [](boost::system::error_code ec, std::size_t) {
                    if (ec) std::cerr << "Broadcast error: " << ec.message() << std::endl;
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
    auto it = contexts_.find(uuid);
    if (it == contexts_.end()) {
        return nullptr;
    }
    auto new_ctx = std::make_unique<BattleContext>(*(it->second));
    new_ctx->uuid = std::chrono::system_clock::now().time_since_epoch().count();
    return new_ctx;
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
