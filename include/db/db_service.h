#ifndef MAIL_SYSTEM_DB_SERVICE_H
#define MAIL_SYSTEM_DB_SERVICE_H

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace mail_system {

// 数据库查询结果接口
class IDBResult {
public:
    virtual ~IDBResult() = default;
    
    // 获取结果集中的行数
    virtual size_t get_row_count() const = 0;
    // 获取结果集中的列数
    virtual size_t get_column_count() const = 0;
    // 获取列名列表
    virtual std::vector<std::string> get_column_names() const = 0;
    // 获取指定行的数据
    virtual std::map<std::string, std::string> get_row(size_t row_index) const = 0;
    // 获取所有行的数据
    virtual std::vector<std::map<std::string, std::string>> get_all_rows() const = 0;
    // 获取指定行列的数据
    virtual std::string get_value(size_t row_index, const std::string& column_name) const = 0;
};

// 数据库连接接口
class IDBConnection {
public:
    virtual ~IDBConnection() = default;
    
    // 连接到数据库
    virtual bool connect() = 0;
    // 断开连接
    virtual void disconnect() = 0;
    // 检查连接状态
    virtual bool is_connected() const = 0;
    // 执行查询并返回结果
    virtual std::shared_ptr<IDBResult> query(const std::string& sql) = 0;
    // 执行更新操作（插入、更新、删除）
    virtual bool execute(const std::string& sql) = 0;
    // 开始事务
    virtual bool begin_transaction() = 0;
    // 提交事务
    virtual bool commit() = 0;
    // 回滚事务
    virtual bool rollback() = 0;
    // 获取最后一次操作的错误信息
    virtual std::string get_last_error() const = 0;
    // 转义字符串
    virtual std::string escape_string(const std::string& str) const = 0;
};

// 数据库服务抽象类
class DBService {
public:
    virtual ~DBService() = default;

    // 创建数据库连接
    virtual std::shared_ptr<IDBConnection> create_connection(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database,
        unsigned int port
    ) = 0;

    // 获取服务类型名称
    virtual std::string get_service_name() const = 0;

protected:
    DBService() = default;
};

} // namespace mail_system

#endif // MAIL_SYSTEM_DB_SERVICE_H