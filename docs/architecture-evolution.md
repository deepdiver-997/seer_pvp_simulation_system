# 架构改进讨论记录

## 背景

本项目是赛尔号 PVP 对战系统的 C++ 实现，目标之一是用于训练 AI 模型。需要支持：
- **训练模式**：单连接，无超时
- **对战模式**：双连接，带超时
- **多并发对战**：Server 管理多场同时进行的对战

---

## 演进过程

### 第一阶段：原始设计

最初设计使用 shared_ptr 持有 BattleContext：
```cpp
class BattleContext {
    std::shared_ptr<ThreadPoolBase> battle_pool_;
    std::unique_ptr<tcp::socket> m_socket;
};
```

问题：
- Socket 操作直接在 Context 中，FSM 耦合 IO
- 多线程共享需要额外同步
- 没有战局复制支持

---

### 第二阶段：引入 IControlBlock 接口

**核心思想**：FSM 应该只关心状态流转，IO 操作应该抽象出去。

```cpp
class IControlBlock {
    virtual void async_read(int player_id, std::unique_ptr<BattleContext> ctx,
                           std::function<void(std::unique_ptr<BattleContext>)> callback) = 0;
    virtual void async_write(int player_id, const std::string& data,
                            std::unique_ptr<BattleContext> ctx,
                            std::function<void(std::unique_ptr<BattleContext>)> callback) = 0;
};
```

**发现问题**：
```cpp
void BattleFsm::run(std::unique_ptr<BattleContext> battleContext) {
    // ...
    BattleContext::async_read(std::move(battleContext), [this](std::unique_ptr<BattleContext> ctx) {
        this->run(std::move(ctx));  // 错误！应该是 member function call
    });
}
```

修复为 `battleContext->async_read(...)` 成员函数调用。

---

### 第三阶段：ControlBlock 持有所有 Context

**用户提出**：需要支持战局复制（一个控制块管理多个 context），需要 UUID 到 context 的映射。

**设计决策**：
1. ControlBlock 全程持有 unique_ptr
2. FSM 使用 raw pointer，不拥有任何对象
3. ControlBlock 的 wait_for_input 只设置等待状态，不触发回调

```cpp
class IControlBlock {
    virtual void wait_for_input(BattleContext* ctx) = 0;  // 只设置等待的 uuid
    virtual void register_context(std::unique_ptr<BattleContext> ctx) = 0;
    virtual std::unique_ptr<BattleContext> duplicate_context(uint32_t uuid) = 0;
};
```

---

### 第四阶段：持久读取循环 + TCP 粘包处理

**用户指出问题**：
1. `wait_for_input` 应该只设置等待状态，然后直接退出 run 循环
2. 消息应该先到 ControlBlock 的 buffer，解析完整后才路由
3. **命令执行应该在启动 FSM run 之前**，否则多线程会干扰

```cpp
void handle_message(const ParsedMessage& msg) {
    switch (msg.command) {
        case DUPLICATE_CONTEXT:
            // 命令层面先处理，创建新 context
            auto new_ctx = std::make_unique<BattleContext>(*(it->second));
            register_context(std::move(new_ctx));
            return;  // 不启动 run，避免多线程干扰
        // ...
    }
    // 其他命令才路由到 context 并启动 run
    route_and_run(msg);
}
```

---

### 第五阶段：添加安全保护

**Buffer 溢出保护**：
```cpp
if (buffer_.size() > 1024 * 1024) {  // 1MB 上限
    buffer_.clear();
    return;
}
```

---

## 消息格式设计

```
[4字节总长度][2字节命令][4字节UUID][参数...]
```

**设计考量**：
1. 总长度字段在开头，可以边收边检查完整性
2. UUID 大整数空间（最大 4294967295），命令用小整数（0-65535），不会冲突
3. 参数长度从总长度计算，无需单独存储

---

## 最终架构

```
Server
    │
    ├─ shared_ptr<BoostThreadPool> battle_pool_
    │
    ├─ shared_ptr<IControlBlock> control_block
    │    │
    │    ├─ contexts_: unordered_map<uuid, unique_ptr<Context>>
    │    ├─ buffer_: vector<char>（TCP 粘包处理）
    │    ├─ fsm_: shared_ptr<BattleFsm>
    │    │
    │    └─ start_read_loop()  // 永久循环
    │         │
    │         ├─ async_read_some(buffer_)
    │         ├─ has_complete_message()
    │         ├─ parse_message()
    │         ├─ handle_message()
    │         │    └─ route_and_run()
    │         │         └─ battle_pool_->post([ctx, fsm] { fsm->run(ctx); })
    │         │
    │         └─ start_read_loop()  // 递归继续
    │
    └─ active_matches_: map<match_id, shared_ptr<IControlBlock>>
```

---

## 关键设计决策总结

| 决策 | 选择 | 原因 |
|------|------|------|
| Context 所有权 | ControlBlock 持有 unique_ptr | 方便战局复制，统一生命周期管理 |
| FSM 接口 | 接收 raw pointer | 不需要知道所有权细节 |
| 读取模式 | 持久循环 + ControlBlock buffer | 处理 TCP 粘包，命令优先执行 |
| 命令执行时机 | FSM run 启动之前 | 避免多线程干扰 |
| Buffer 保护 | 1MB 上限清空 | 防止恶意客户端撑爆内存 |
| 线程池引用 | BattleFsm 持有 shared_ptr | FSM 需要能 post 任务 |

---

## 待完善功能

1. **async_accept_battle()**：双连接对战模式的监听
2. **心跳响应**：HEARTBEAT 命令目前只有日志
3. **SYNC_STATE 命令**：同步状态给客户端
4. **多 socket 独立 timeout**：当前 BattleControlBlock 的超时逻辑需要完善
