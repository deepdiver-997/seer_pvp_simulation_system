#ifndef ICONTROLBLOCK_H
#define ICONTROLBLOCK_H

#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/asio.hpp>
#include <thread_pool/boost_thread_pool.h>

class BattleContext;
class BattleFsm;

// 消息格式:
// [4字节: 总长度(含header)][2字节: 命令类型][4字节: UUID][参数...]

struct MessageHeader {
    uint32_t total_length;  // 包含header本身
    uint16_t command;       // 命令类型
    uint32_t uuid;         // 目标context UUID
};

// 命令类型 (小范围整数，与UUID不冲突)
enum class Command : uint16_t {
    INVALID = 0,
    // 操作命令
    SELECT_SKILL = 1,       // 选择技能
    USE_MEDICINE = 2,       // 使用药品
    CHOOSE_PET = 3,         // 切换精灵
    SEND_EMOJI = 4,         // 发送表情
    // 状态查询
    SYNC_STATE = 10,        // 同步状态
    HEARTBEAT = 11,         // 心跳
    // 控制命令
    DUPLICATE_CONTEXT = 100, // 复制战局
};

// 解析后的消息
struct ParsedMessage {
    uint32_t uuid;
    Command command;
    std::vector<char> params;  // 已去除header的数据
};

// 宏控制特性
#ifdef BATTLE_TRAINING_MODE
    #define BATTLE_NEED_TIMEOUT()     0
    #define BATTLE_OP_LOGGING          1
    #define BATTLE_DEFAULT_TIMEOUT_MS 0
#else
    #define BATTLE_NEED_TIMEOUT()     1
    #define BATTLE_OP_LOGGING          1
    #define BATTLE_DEFAULT_TIMEOUT_MS  30000
#endif

// 控制块接口 - FSM 通过此接口与 IO 解耦
// 控制块全程持有 context unique_ptr，FSM 通过 raw pointer 操作
class IControlBlock {
public:
    virtual ~IControlBlock() = default;

    // 等待输入 - 设置 FSM 需要等待的 context，之后数据到达时自动继续
    virtual void wait_for_input(BattleContext* ctx) = 0;

    // 异步发送 - 完成后通过 battle_pool_ 继续 run
    virtual void async_write(int player_id, const std::string& data,
                            BattleContext* ctx, BattleFsm* fsm) = 0;

    // 广播给所有玩家
    virtual void broadcast(const std::string& data) = 0;

    // 广播给指定玩家
    virtual void broadcast_to(const std::vector<int>& player_ids, const std::string& data) = 0;

    // 日志记录
    virtual void log_operation(const std::string& op) = 0;
    virtual std::string get_operation_log() const = 0;

    // 战局复制
    virtual std::unique_ptr<BattleContext> duplicate_context(uint32_t uuid) = 0;

    // 获取当前对局信息
    virtual int match_id() const = 0;
    virtual bool is_training_mode() const = 0;

    // 注册/注销 context
    virtual void register_context(std::unique_ptr<BattleContext> ctx) = 0;
    virtual std::unique_ptr<BattleContext> unregister_context(uint32_t uuid) = 0;

    // 获取线程池（供 Server 注入）
    virtual std::shared_ptr<BattleFsm> get_fsm() const = 0;
    virtual void set_battle_pool(std::shared_ptr<BattleFsm> fsm) = 0;

    // 获取注册的 context（用于启动 battle）
    virtual BattleContext* get_context() = 0;
};

// 训练模式控制块 - 单 Socket，无超时
// 持久连接，控制块自己管理 buffer，处理粘包
class TrainingControlBlock : public IControlBlock {
public:
    TrainingControlBlock(int match_id, std::unique_ptr<boost::asio::ip::tcp::socket> socket);

    void wait_for_input(BattleContext* ctx) override;

    void async_write(int player_id, const std::string& data,
                    BattleContext* ctx, BattleFsm* fsm) override;

    void broadcast(const std::string& data) override;
    void broadcast_to(const std::vector<int>& player_ids, const std::string& data) override;

    void log_operation(const std::string& op) override;
    std::string get_operation_log() const override;

    std::unique_ptr<BattleContext> duplicate_context(uint32_t uuid) override;

    int match_id() const override { return match_id_; }
    bool is_training_mode() const override { return true; }

    void register_context(std::unique_ptr<BattleContext> ctx) override;
    std::unique_ptr<BattleContext> unregister_context(uint32_t uuid) override;

    std::shared_ptr<BattleFsm> get_fsm() const override { return fsm_; }
    void set_battle_pool(std::shared_ptr<BattleFsm> fsm) override { fsm_ = fsm; }

    BattleContext* get_context() override;

    // 启动持久读取循环
    void start_read_loop();

private:
    // 解析消息（IO层检查合法性）
    ParsedMessage parse_message();

    // 处理完整消息
    void handle_message(const ParsedMessage& msg);

    // 检查 buffer 是否有一完整消息
    bool has_complete_message() const;

    // 路由到对应 context 并启动 run
    void route_and_run(const ParsedMessage& msg);

    int match_id_;
    std::unique_ptr<boost::asio::ip::tcp::socket> socket_;
    std::shared_ptr<BattleFsm> fsm_;

    // uuid -> owned context
    std::unordered_map<uint32_t, std::unique_ptr<BattleContext>> contexts_;

    // 当前等待输入的 context（一个训练模式同时只服务一个）
    uint32_t waiting_uuid_ = 0;

    // 控制块自己的 buffer，处理 TCP 粘包
    std::vector<char> buffer_;

    std::string operation_log_;
};

// 真实对战控制块 - 双 Socket，带超时
// 可处理多个 context（多个房间共享连接）
class BattleControlBlock : public IControlBlock {
public:
    BattleControlBlock(int match_id,
                      std::unique_ptr<boost::asio::ip::tcp::socket> socket1,
                      std::unique_ptr<boost::asio::ip::tcp::socket> socket2);

    void wait_for_input(BattleContext* ctx) override;

    void async_write(int player_id, const std::string& data,
                    BattleContext* ctx, BattleFsm* fsm) override;

    void broadcast(const std::string& data) override;
    void broadcast_to(const std::vector<int>& player_ids, const std::string& data) override;

    void log_operation(const std::string& op) override;
    std::string get_operation_log() const override;

    std::unique_ptr<BattleContext> duplicate_context(uint32_t uuid) override;

    int match_id() const override { return match_id_; }
    bool is_training_mode() const override { return false; }

    void register_context(std::unique_ptr<BattleContext> ctx) override;
    std::unique_ptr<BattleContext> unregister_context(uint32_t uuid) override;

    std::shared_ptr<BattleFsm> get_fsm() const override { return fsm_; }
    void set_battle_pool(std::shared_ptr<BattleFsm> fsm) override { fsm_ = fsm; }

    BattleContext* get_context() override;

    // 启动持久读取循环
    void start_read_loop(int player_id);

    // 获取 socket
    boost::asio::ip::tcp::socket* socket(int player_id) {
        return sockets_[player_id].get();
    }

private:
    ParsedMessage parse_message();

    void handle_message(const ParsedMessage& msg);

    bool has_complete_message() const;

    void route_and_run(const ParsedMessage& msg);

    int match_id_;
    std::unique_ptr<boost::asio::ip::tcp::socket> sockets_[2];
    std::shared_ptr<BattleFsm> fsm_;

    // uuid -> owned context
    std::unordered_map<uint32_t, std::unique_ptr<BattleContext>> contexts_;

    // 每个 socket 独立的等待状态和 buffer
    uint32_t waiting_uuid_[2] = {0, 0};
    std::vector<char> buffers_[2];

    std::string operation_log_;
};

// 辅助函数：从网络字节序解析 header
inline MessageHeader parse_header(const std::vector<char>& buffer, size_t offset = 0) {
    MessageHeader header = {0, 0, 0};
    if (buffer.size() < offset + 10) return header;
    std::memcpy(&header.total_length, buffer.data() + offset, 4);
    std::memcpy(&header.command, buffer.data() + offset + 4, 2);
    std::memcpy(&header.uuid, buffer.data() + offset + 6, 4);
    header.total_length = ntohl(header.total_length);
    header.command = ntohs(header.command);
    header.uuid = ntohl(header.uuid);
    return header;
}

// 辅助函数：构建消息
inline std::vector<char> build_message(Command cmd, uint32_t uuid, const void* params = nullptr, size_t param_len = 0) {
    std::vector<char> msg;
    msg.resize(10 + param_len);
    uint32_t total = htonl(10 + param_len);
    uint16_t command = htons(static_cast<uint16_t>(cmd));
    uint32_t uid = htonl(uuid);
    std::memcpy(msg.data(), &total, 4);
    std::memcpy(msg.data() + 4, &command, 2);
    std::memcpy(msg.data() + 6, &uid, 4);
    if (params && param_len > 0) {
        std::memcpy(msg.data() + 10, params, param_len);
    }
    return msg;
}

#endif // ICONTROLBLOCK_H
