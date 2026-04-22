#include <server.h>
#include <iostream>
#include <fsm/battleFsm.h>
#include <fsm/battleContext.h>

Server::Server()
    : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(IP_), PORT_))
{
    std::cout << "Server started at " << IP_ << ":" << PORT_ << std::endl;

    battle_pool_ = std::make_shared<BoostThreadPool>(4);
    io_pool_ = std::make_shared<IOThreadPool>(4);
    battle_pool_->start();
    io_pool_->start();
}

Server::~Server() {
    stop();
}

void Server::stop() {
    battle_pool_->stop();
    io_pool_->stop();
}

void Server::start() {
    async_accept_training();
    io_context_.run();
}

void Server::async_accept_training() {
    if (!acceptor_.is_open()) {
        return;
    }

    auto socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket = std::move(socket)](boost::system::error_code ec) mutable {
        if (!ec) {
            SeerRobot robots[2] = {};  // TODO: 从客户端读取
            handle_training_connection(std::move(socket), robots);
        } else {
            std::cerr << "Accept error: " << ec.message() << std::endl;
        }
        this->async_accept_training();
    });
}

void Server::handle_training_connection(std::unique_ptr<boost::asio::ip::tcp::socket> socket,
                                        const SeerRobot robots[2]) {
    int match_id = next_match_id();

    // 创建控制块
    auto control_block = std::make_shared<TrainingControlBlock>(match_id, std::move(socket));

    // 创建 FSM（持有battle_pool引用）
    auto fsm = std::make_shared<BattleFsm>(true);
    fsm->set_battle_pool(battle_pool_);

    // 创建 Context（由控制块持有）
    auto ctx = std::make_unique<BattleContext>(control_block.get(), robots);

    // 注册 context 到控制块
    control_block->register_context(std::move(ctx));

    // 注册控制块到管理容器
    {
        std::lock_guard<std::mutex> lock(matches_mutex_);
        active_matches_[match_id] = control_block;
    }

    // 获取刚注册的 context 的 raw pointer
    auto registered_ctx = control_block->get_context();

    // 开始战斗循环 - battle_pool_ 执行
    battle_pool_->post([registered_ctx, fsm, match_id, this]() {
        fsm->run(registered_ctx);

        // 结束后清理
        std::lock_guard<std::mutex> lock(this->matches_mutex_);
        this->active_matches_.erase(match_id);
    });
}

void Server::handle_battle_connection(std::unique_ptr<boost::asio::ip::tcp::socket> socket1,
                                     std::unique_ptr<boost::asio::ip::tcp::socket> socket2,
                                     const SeerRobot robots[2]) {
    int match_id = next_match_id();

    auto control_block = std::make_shared<BattleControlBlock>(match_id,
        std::move(socket1), std::move(socket2));

    auto fsm = std::make_shared<BattleFsm>(true);
    fsm->set_battle_pool(battle_pool_);

    auto ctx = std::make_unique<BattleContext>(control_block.get(), robots);
    control_block->register_context(std::move(ctx));

    {
        std::lock_guard<std::mutex> lock(matches_mutex_);
        active_matches_[match_id] = control_block;
    }

    auto registered_ctx = control_block->get_context();

    battle_pool_->post([registered_ctx, fsm, match_id, this]() {
        fsm->run(registered_ctx);

        std::lock_guard<std::mutex> lock(this->matches_mutex_);
        this->active_matches_.erase(match_id);
    });
}
