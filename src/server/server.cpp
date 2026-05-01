#include <server.h>
#include <iostream>
#include <fsm/battleFsm.h>
#include <fsm/iControlBlock.h>
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

void log_server(const std::string& level, const std::string& msg) {
    std::cout << "[" << ts_now() << "] [" << level << "] [server] " << msg << std::endl;
}

} // namespace

Server::Server()
    : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 4399))
{
    log_server("INFO", std::string("booting at ") + IP_ + ":" + std::to_string(PORT_));

    battle_pool_ = std::make_shared<BoostThreadPool>(4);
    io_pool_ = std::make_shared<IOThreadPool>(4);
    battle_pool_->start();
    io_pool_->start();
    log_server("INFO", "thread pools started: battle=4 io=4");
}

Server::~Server() {
    stop();
}

void Server::stop() {
    log_server("INFO", "stopping server");
    battle_pool_->stop();
    io_pool_->stop();
    log_server("INFO", "server stopped");
}

void Server::start() {
    log_server("INFO", "entering accept loop");
    async_accept_training();
    io_context_.run();
}

void Server::async_accept_training() {
    if (!acceptor_.is_open()) {
        log_server("WARN", "acceptor is closed; skip accept");
        return;
    }

    auto socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket = std::move(socket)](boost::system::error_code ec) mutable {
        if (!ec) {
            try {
                const auto remote = socket->remote_endpoint();
                log_server("INFO", "accepted connection from " + remote.address().to_string() + ":" + std::to_string(remote.port()));
                handle_training_connection(std::move(socket));
            } catch (const std::exception& ex) {
                log_server("ERROR", std::string("training connection setup failed: ") + ex.what());
            }
        } else {
            log_server("ERROR", std::string("accept error: ") + ec.message());
        }
        this->async_accept_training();
    });
}

void Server::handle_training_connection(std::unique_ptr<boost::asio::ip::tcp::socket> socket) {
    int match_id = next_match_id();
    log_server("INFO", "create training match id=" + std::to_string(match_id));

    // 创建控制块
    auto control_block = std::make_shared<TrainingControlBlock>(match_id, std::move(socket));

    // 创建 FSM（持有battle_pool引用）
    auto fsm = std::make_shared<BattleFsm>(true);
    fsm->battle_pool_ = battle_pool_;
    control_block->set_fsm(fsm);

    // 启动持久读取循环，后续消息继续由控制块路由到 BattleContext
    {
        std::lock_guard<std::mutex> lock(matches_mutex_);
        active_matches_[match_id] = control_block;
        log_server("INFO", "active matches=" + std::to_string(active_matches_.size()));
    }

    control_block->start_read_loop();
}

void Server::handle_battle_connection(std::unique_ptr<boost::asio::ip::tcp::socket> socket1,
                                     std::unique_ptr<boost::asio::ip::tcp::socket> socket2) {
    int match_id = next_match_id();
    log_server("INFO", "create battle match id=" + std::to_string(match_id));

    auto control_block = std::make_shared<BattleControlBlock>(match_id,
        std::move(socket1), std::move(socket2));

    auto fsm = std::make_shared<BattleFsm>(true);
    fsm->battle_pool_ = battle_pool_;
    control_block->set_fsm(fsm);

    control_block->start_read_loop(0);
    control_block->start_read_loop(1);

    {
        std::lock_guard<std::mutex> lock(matches_mutex_);
        active_matches_[match_id] = control_block;
        log_server("INFO", "active matches=" + std::to_string(active_matches_.size()));
    }
}
