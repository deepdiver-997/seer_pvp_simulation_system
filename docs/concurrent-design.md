# 战斗系统并发架构设计 v2

## 设计目标
- `IControlBlock` 接口模式，FSM 与 IO 完全解耦
- 控制块全程持有 `BattleContext` unique_ptr，FSM 使用 raw pointer
- TCP 粘包处理：控制块自有 buffer + 消息头部长度检查
- 命令执行在 FSM run **之前**，避免多线程干扰
- 支持多 context 共享连接（战局复制场景）

## 核心组件

### 1. IControlBlock 接口
```cpp
class IControlBlock {
    // 等待输入 - 设置当前需要输入的 context uuid
    virtual void wait_for_input(BattleContext* ctx) = 0;

    // 异步发送 - 完成后继续 run
    virtual void async_write(int player_id, const std::string& data,
                            BattleContext* ctx, BattleFsm* fsm) = 0;

    // 战局复制
    virtual std::unique_ptr<BattleContext> duplicate_context(uint32_t uuid) = 0;

    // context 管理
    virtual void register_context(std::unique_ptr<BattleContext> ctx) = 0;
    virtual std::unique_ptr<BattleContext> unregister_context(uint32_t uuid) = 0;
};
```

### 2. TrainingControlBlock（单 Socket，训练模式）
- 持久连接，无超时
- 单一 context，直接路由
- 启动后无限读取循环

### 3. BattleControlBlock（双 Socket，对战模式）
- 带超时检测（30秒）
- 支持多 context 共享连接
- 每 socket 独立 buffer

### 4. BattleContext
```cpp
class BattleContext {
    unsigned int uuid;
    IControlBlock* control_block_;  // 控制块指针
    int current_player_id_;          // 当前等待输入的玩家
    std::vector<char> m_buffer;     // 消息参数缓冲区
    bool is_empty;                  // 是否正在等待输入
};
```

### 5. BattleFsm
```cpp
class BattleFsm {
    std::shared_ptr<BoostThreadPool> battle_pool_;  // 战斗计算线程池

    void run(BattleContext* battleContext);  // 接受 raw pointer
    void post(std::function<void()> task);    // 提交到线程池
};
```

## 消息格式
```
[4字节: 总长度(含header)][2字节: 命令类型][4字节: UUID][参数...]
```

命令类型：
- `SELECT_SKILL = 1` - 选择技能
- `USE_MEDICINE = 2` - 使用药品
- `CHOOSE_PET = 3` - 切换精灵
- `SEND_EMOJI = 4` - 发送表情
- `SYNC_STATE = 10` - 同步状态
- `HEARTBEAT = 11` - 心跳
- `DUPLICATE_CONTEXT = 100` - 复制战局

## 执行流程

### 连接建立
```
Server.handle_training_connection(socket, robots)
    │
    ├─ 创建 TrainingControlBlock(match_id, socket)
    ├─ 创建 BattleFsm，设置 battle_pool_
    ├─ 创建 BattleContext，register_context()
    ├─ control_block->start_read_loop()  ← 启动持久读取
    │
    └─ battle_pool_->post([ctx, fsm]() {
            fsm->run(ctx);  // 第一次启动
        });
```

### FSM.run 循环
```
FSM.run(BattleContext* ctx)
    │
    └─ while(true)
           │
           ├─ runInternal(ctx) 返回 false
           │    ├─ 执行 handler
           │    └─ continue
           │
           ├─ runInternal(ctx) 返回 true (need_input)
           │    ├─ ctx->control_block_->wait_for_input(ctx)
           │    └─ return（等待消息到达）
           │
           └─ ctx->currentState == FINISHED
                └─ return（结束）
```

### 消息到达后的处理
```
ControlBlock.start_read_loop()  ← 持久读取
    │
    ├─ async_read_some(buffer_)
    │
    ├─ has_complete_message()? ──No──→ 继续读取
    │    ↓Yes
    ├─ parse_message() → 提取 uuid + command + params
    │    ↓
    ├─ handle_message(msg)
    │    │
    │    ├─ command == DUPLICATE_CONTEXT
    │    │    ├─ 解析 uuid
    │    │    ├─ 复制新 context
    │    │    └─ return（不启动 run）
    │    │
    │    ├─ command == HEARTBEAT
    │    │    └─ return（直接响应）
    │    │
    │    └─ 其他命令
    │         └─ route_and_run(msg)
    │              │
    │              ├─ waiting_uuid_ == msg.uuid && ctx->is_empty?
    │              │    ↓Yes
    │              ├─ ctx->m_buffer = msg.params
    │              ├─ ctx->is_empty = false
    │              └─ battle_pool_->post([ctx, fsm]() {
    │                      fsm->run(ctx);
    │                  });
```

## TCP 粘包处理

### Buffer 管理
```
收到数据: [部分msg1][部分msg2][完整msg3][完整msg4]...
            ↓
has_complete_message() → 检查 total_length
            ↓Yes
parse_message() → 提取完整 msg3，移除
            ↓
buffer: [部分msg1][部分msg2][完整msg4]...
            ↓
has_complete_message() → No（数据不足）
            ↓
等待下一批数据...
```

### 完整性检查
```cpp
bool has_complete_message() const {
    if (buffer_.size() < 10) return false;  // 最小 header
    auto header = parse_header(buffer_);
    return buffer_.size() >= header.total_length;
}
```

## 命令优先执行

关键设计：**命令处理在 FSM run 启动之前**

```
收到 DUPLICATE_CONTEXT 消息
    ↓
handle_message()
    ├─ 创建新 context
    ├─ register_context()
    └─ return（不启动 run，避免多线程干扰）
    ↓
稍后通过 wait_for_input 正式启动
```

这样做的好处：
1. 战局复制在消息层面完成，FSM 无需知道复制逻辑
2. 避免在 run 执行过程中创建/修改 context 导致数据竞争

## 所有权关系

```
Server
    │
    ├─ shared_ptr<BoostThreadPool> battle_pool_
    │
    ├─ shared_ptr<IControlBlock> control_block
    │    │
    │    ├─ contexts_: unordered_map<uuid, unique_ptr<BattleContext>>
    │    │
    │    └─ fsm_: shared_ptr<BattleFsm>
    │
    └─ battle_pool_->post([ctx, fsm]() {
            fsm->run(ctx);  // ctx 是 raw pointer
        });
```

**关键点**：
- ControlBlock 持有所有 context unique_ptr
- FSM 只接收 raw pointer，不拥有任何对象
- 消息路由时通过 uuid 查找对应 context
- ControlBlock 的 start_read_loop() 是永久循环，直到连接断开

## 超时处理（BattleControlBlock）

```
wait_for_input(ctx)
    ↓
ctx->current_player_id_ 指定读取哪个 socket
waiting_uuid_[player_id] = ctx->uuid
    ↓
数据到达时检查 uuid 是否匹配
    ↓
超时时（30秒）：
    ctx->back_to_last_state()
    fsm->run(ctx)
```

## 安全保护

### Buffer 溢出保护
```cpp
if (buffer_.size() > 1024 * 1024) {  // 1MB 上限
    buffer_.clear();
    return;
}
```

### 错误处理
- Socket 错误：输出日志，重置读取循环
- 未知 uuid：输出日志，丢弃消息
- Context 不在等待状态：输出日志，丢弃消息
