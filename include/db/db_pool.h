#ifndef MAIL_SYSTEM_DB_POOL_H
#define MAIL_SYSTEM_DB_POOL_H

#include <db/db_service.h>
#include <nlohmann/json.hpp> // JSON库
#include <fstream>
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace mail_system {

// 数据库连接池抽象类
class DBPool {
public:
    virtual ~DBPool() = default;

    // 获取数据库连接
    virtual std::shared_ptr<IDBConnection> get_connection() = 0;

    // 释放数据库连接
    virtual void release_connection(std::shared_ptr<IDBConnection> connection) = 0;

    // 获取连接池大小
    virtual size_t get_pool_size() const = 0;

    // 获取当前可用连接数
    virtual size_t get_available_connections() const = 0;

    // 关闭连接池
    virtual void close() = 0;

protected:
    DBPool() = default;

    // 初始化连接池
    virtual void initialize_pool() = 0;

    // 创建新的连接
    virtual std::shared_ptr<IDBConnection> create_connection() = 0;
};

// 数据库连接池配置
struct DBPoolConfig {
    std::string achieve;
    std::string host;
    std::string user;
    std::string password;
    std::string database;
    unsigned int port;
    size_t initial_pool_size;
    size_t max_pool_size;
    unsigned int connection_timeout;
    unsigned int idle_timeout;

    DBPoolConfig()
        : port(3306),
          initial_pool_size(5),
          max_pool_size(10),
          connection_timeout(5),
          idle_timeout(60) {}
    void show() const {
        std::cout << "DBPoolConfig: "
                  << "\n\tachieve = " << achieve
                  << "\n\thost = " << host
                  << "\n\tuser = " << user
                  << "\n\tdatabase = " << database
                  << "\n\tport = " << port
                  << "\n\tinitial_pool_size = " << initial_pool_size
                  << "\n\tmax_pool_size = " << max_pool_size
                  << "\n\tconnection_timeout = " << connection_timeout
                  << "\n\tidle_timeout = " << idle_timeout
                  << std::endl;
    }

    // 从JSON对象加载配置
    bool loadFromJson(const std::string& filename) {
        std::ifstream config_file(filename.c_str());
        if (!config_file.is_open()) {
            std::cerr << "Failed to open config file: " << filename << std::endl;
            return false;
        }
        nlohmann::json json;
        config_file >> json;
        config_file.close();
        if (json.is_discarded()) {
            std::cerr << "Failed to parse config file: " << filename << std::endl;
            return false;
        }
        achieve = json.value("achieve", achieve);
        host = json.value("host", host);
        user = json.value("user", user);
        password = json.value("password", password);
        database = json.value("database", database);
        port = json.value("port", port);
        initial_pool_size = json.value("initial_pool_size", initial_pool_size);
        max_pool_size = json.value("max_pool_size", max_pool_size);
        connection_timeout = json.value("connection_timeout", connection_timeout);
        idle_timeout = json.value("idle_timeout", idle_timeout);
        return true;
    }
};

// 数据库连接池工厂接口
class DBPoolFactory {
public:
    virtual ~DBPoolFactory() = default;

    // 创建连接池
    virtual std::shared_ptr<DBPool> create_pool(
        const DBPoolConfig& config,
        std::shared_ptr<DBService> db_service
    ) = 0;

protected:
    DBPoolFactory() = default;
};

} // namespace mail_system

#endif // MAIL_SYSTEM_DB_POOL_H