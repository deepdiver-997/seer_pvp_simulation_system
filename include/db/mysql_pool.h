#ifndef MAIL_SYSTEM_MYSQL_POOL_H
#define MAIL_SYSTEM_MYSQL_POOL_H

#include "db/db_pool.h"
#include "db/mysql_service.h"
#include <queue>
#include <chrono>
#include <thread>
#include <atomic>

namespace mail_system {

// MySQL连接池实现
class MySQLPool : public DBPool {
public:
    MySQLPool(const DBPoolConfig& config, std::shared_ptr<DBService> db_service);
    ~MySQLPool() override;

    // DBPool接口实现
    std::shared_ptr<IDBConnection> get_connection() override;
    void release_connection(std::shared_ptr<IDBConnection> connection) override;
    size_t get_pool_size() const override;
    size_t get_available_connections() const override;
    void close() override;

protected:
    // 连接包装类，用于跟踪连接的使用情况
    struct ConnectionWrapper {
        std::shared_ptr<IDBConnection> connection;
        std::chrono::steady_clock::time_point last_used;
        bool in_use;

        ConnectionWrapper(std::shared_ptr<IDBConnection> conn)
            : connection(conn),
              last_used(std::chrono::steady_clock::now()),
              in_use(false) {}
    };

    void initialize_pool() override;
    std::shared_ptr<IDBConnection> create_connection() override;

private:
    DBPoolConfig m_config;
    std::shared_ptr<DBService> m_dbService;
    std::vector<std::shared_ptr<ConnectionWrapper>> m_connections;
    std::queue<std::shared_ptr<ConnectionWrapper>> m_availableConnections;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running;
    std::thread m_maintenanceThread;

    // 连接池维护线程
    void maintenance_thread();
    // 检查并清理空闲连接
    void cleanup_idle_connections();
    // 验证连接是否有效
    bool validate_connection(std::shared_ptr<IDBConnection> connection);
};

// MySQL连接池工厂实现
class MySQLPoolFactory : public DBPoolFactory {
public:
    ~MySQLPoolFactory() override = default;
    std::shared_ptr<DBPool> create_pool(
        const DBPoolConfig& config,
        std::shared_ptr<DBService> db_service
    ) override;

    // 获取单例实例
    static MySQLPoolFactory& get_instance();

private:
    static std::unique_ptr<MySQLPoolFactory> s_instance;
    static std::mutex s_mutex;

    MySQLPoolFactory() = default;

    // 禁止复制和赋值
    MySQLPoolFactory(const MySQLPoolFactory&) = delete;
    MySQLPoolFactory& operator=(const MySQLPoolFactory&) = delete;
};

} // namespace mail_system

#endif // MAIL_SYSTEM_MYSQL_POOL_H