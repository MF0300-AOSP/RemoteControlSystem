#ifndef HTTP_SESSION_HPP
#define HTTP_SESSION_HPP

#include "connection.hpp"

#include <memory>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace server {

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>


template<class RequestHandler>
class HttpSession : public IConnectionBase, public std::enable_shared_from_this<HttpSession<RequestHandler>> {
  // This is the C++11 equivalent of a generic lambda.
  // The function object is used to send an HTTP message.
  struct Sender {
    std::shared_ptr<HttpSession> session;

    explicit Sender(std::shared_ptr<HttpSession> self)
        : session(self) {}

    template<bool isRequest, class Body, class Fields>
    void operator()(http::message<isRequest, Body, Fields>&& msg) const {
      // The lifetime of the message has to extend
      // for the duration of the async operation so
      // we use a shared_ptr to manage it.
      auto sp = std::make_shared<http::message<isRequest, Body, Fields>>(std::move(msg));

      // Store a type-erased version of the shared
      // pointer in the class to keep it alive.
      session->res_ = sp;

      // Write the response
      http::async_write(
          session->stream_,
          *sp,
          beast::bind_front_handler(
              &HttpSession::OnWrite,
              session,
              sp->need_eof()));
    }
  };

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  std::shared_ptr<void> res_;
  RequestHandler* request_handler_;

  // The parser is stored in an optional container so we can
  // construct it from scratch it at the beginning of each new message.
  boost::optional<http::request_parser<http::string_body>> parser_;

 public:
  HttpSession(tcp::socket&& socket, RequestHandler* handler)
      : stream_(std::move(socket)), request_handler_(handler) {}

  void Run() override {
    DoRead();
  }

 private:
  void DoRead() {
    // Make the request empty before reading,
    // otherwise the operation behavior is undefined.
    parser_.emplace();
    parser_->body_limit(25 * 1024 * 1024);    // allow uploads up to 25 MB

    stream_.expires_after(std::chrono::seconds(30));

    http::async_read(stream_, buffer_, *parser_,
        beast::bind_front_handler(
            &HttpSession::OnRead,
            this->shared_from_this()));
  }

  void OnRead(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream) {
      DoClose();
      return;
    }

    if (ec)
      return;

    // Send the response
    Sender sender(this->shared_from_this());
    request_handler_->HandleRequest(std::move(parser_->release()), std::move(sender));
  }

  void OnWrite(bool close, beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec)
      return;

    if (close) {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      DoClose();
      return;
    }

    // We're done with the response so delete it
    res_ = nullptr;

    // Read another request
    DoRead();
  }

  void DoClose() {
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
  }
};

}  // namespace server

#endif  // HTTP_SESSION_HPP
