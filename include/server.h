#ifndef __INCLUDED_SERVER__
#define __INCLUDED_SERVER__

#include <iostream>
#include <boost/asio.hpp>

class Server {
    public:
    explicit Server();
    ~Server() = default;
    void start();
    void async_acception();
    void handle_connection(std::unique_ptr<boost::asio::ip::tcp::socket>&& client_socket);
    private:
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::acceptor acceptor;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> m_workGuard;
    boost::asio::ip::tcp::socket socket;
    unsigned short PORT = 4399;
    const char * IP = "127.0.0.1";
};

Server::Server()
    : acceptor(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(IP), PORT)),
      m_workGuard(std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(io_context.get_executor())),
      socket(io_context) {
    std::cout << "Server started at " << IP << ":" << PORT << std::endl;
}

void Server::start() {
    async_acception();
    io_context.run();
}

void Server::async_acception() {
    if (!acceptor.is_open()) {
        return;
    }
    if (!socket.is_open()) {
        std::cerr << "socket closed" << std::endl;
        return;
    }

    auto client_socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context);
    acceptor.async_accept(*client_socket, [this, client_socket = std::move(client_socket)](boost::system::error_code ec) mutable {
        if (!ec) {
            handle_connection(std::move(client_socket));
        }
        else {
            std::cerr << "Error accepting connection: " << ec.message() << std::endl;
        }
        this->async_acception();
    });
}

#endif