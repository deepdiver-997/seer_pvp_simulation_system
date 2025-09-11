#ifndef MAIL_SYSTEM_MYSQL_SERVICE_H
#define MAIL_SYSTEM_MYSQL_SERVICE_H

#include "db/db_service.h"
#include <mysql/mysql.h>
#include <mutex>

namespace mail_system {

// MySQL查询结果实现
class MySQLResult : public IDBResult {
public:
    explicit MySQLResult(MYSQL_RES* result);
    ~MySQLResult() override;

    // IDBResult接口实现
    size_t get_row_count() const override;
    size_t get_column_count() const override;
    std::vector<std::string> get_column_names() const override;
    std::map<std::string, std::string> get_row(size_t row_index) const override;
    std::vector<std::map<std::string, std::string>> get_all_rows() const override;
    std::string get_value(size_t row_index, const std::string& column_name) const override;

private:
    MYSQL_RES* m_result;
    std::vector<std::string> m_columnNames;
    std::vector<std::vector<std::string>> m_rows;
    size_t m_rowCount;
    size_t m_columnCount;

    void load_result_data();
};

// MySQL连接实现
class MySQLConnection : public IDBConnection {
public:
    MySQLConnection();
    ~MySQLConnection() override;

    // 设置连接参数
    void set_connection_params(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database,
        unsigned int port
    );

    // IDBConnection接口实现
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    std::shared_ptr<IDBResult> query(const std::string& sql) override;
    bool execute(const std::string& sql) override;
    bool begin_transaction() override;
    bool commit() override;
    bool rollback() override;
    std::string get_last_error() const override;
    std::string escape_string(const std::string& str) const override;

private:
    MYSQL* m_mysql;
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_database;
    unsigned int m_port;
    bool m_connected;
    mutable std::mutex m_mutex;

    void init_mysql();
};

// MySQL服务实现
class MySQLService : public DBService {
public:
    MySQLService();
    ~MySQLService() override;

    // DBService接口实现
    std::shared_ptr<IDBConnection> create_connection(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database,
        unsigned int port
    ) override;

    std::string get_service_name() const override;

    // 获取单例实例
    static MySQLService& get_instance();

private:
    static std::unique_ptr<MySQLService> s_instance;
    static std::mutex s_mutex;

    // 禁止复制和赋值
    MySQLService(const MySQLService&) = delete;
    MySQLService& operator=(const MySQLService&) = delete;
};

} // namespace mail_system

#endif // MAIL_SYSTEM_MYSQL_SERVICE_H