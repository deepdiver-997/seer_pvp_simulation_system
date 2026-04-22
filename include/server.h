#ifndef __INCLUDED_SERVER__
#define __INCLUDED_SERVER__

#include <memory>
#include <map>
#include <vector>
#include <thread_pool/boost_thread_pool.h>
#include <thread_pool/io_thread_pool.h>
#include <fsm/iControlBlock.h>
#include <fsm/battleFsm.h>
#include <entities/seer-robot.h>

class Server {
public:
    explicit Server();
    ~Server();

    void start();
    void stop();

    // 处理单连接训练模式
    void handle_training_connection(std::unique_ptr<boost::asio::ip::tcp::socket> socket,
                                   const SeerRobot robots[2]);

    // 处理双连接对战模式
    void handle_battle_connection(std::unique_ptr<boost::asio::ip::tcp::socket> socket1,
                                 std::unique_ptr<boost::asio::ip::tcp::socket> socket2,
                                 const SeerRobot robots[2]);

    int next_match_id() { return next_match_id_++; }

private:
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    unsigned short PORT_ = 4399;
    const char* IP_ = "127.0.0.1";

    std::shared_ptr<BoostThreadPool> battle_pool_;
    std::shared_ptr<IOThreadPool> io_pool_;

    // 训练模式监听
    void async_accept_training();

    // 对战模式监听
    void async_accept_battle();

    int next_match_id_ = 0;

    // 多对战管理 - control block 持有所有 context
    std::map<int, std::shared_ptr<IControlBlock>> active_matches_;
    std::mutex matches_mutex_;
};

#endif
