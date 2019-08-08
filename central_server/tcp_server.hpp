#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <utility>

#include "connection.hpp"

namespace server {

class IConnectionFactory {
 public:
  virtual ~IConnectionFactory() = default;

  virtual BaseConnectionPtr CreateConnection(tcp::socket socket) = 0;
};


class TcpServer {
 public:
  TcpServer(boost::asio::io_context& io_context, unsigned short port, IConnectionFactory* factory)
      : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), connection_factory_(factory) {
    acceptor_.set_option(tcp::no_delay(true));
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
    DoAccept();
  }

 private:
  void DoAccept() {
    acceptor_.async_accept(
        boost::asio::make_strand(acceptor_.get_executor()),
        [this](boost::system::error_code ec, tcp::socket socket) {
          if (!ec)
            connection_factory_->CreateConnection(std::move(socket))->Run();

          DoAccept();
        });
  }

  tcp::acceptor acceptor_;
  IConnectionFactory* connection_factory_;
};

}  // namespace server

#endif  // TCP_SERVER_HPP
