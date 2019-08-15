#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <vector>
#include <utility>

#include <boost/asio.hpp>

class IConnection;
using ConnectionPtr = std::shared_ptr<IConnection>;

class IIncomingData {
 public:
  virtual ~IIncomingData() = default;

  virtual std::uint32_t GetType() const = 0;

  // Read payload from connection
  // callback must be called as read completes, in any case
  virtual void ReadPayload(ConnectionPtr connection, std::function<void()> callback) = 0;
};

using IncomingDataPtr = std::shared_ptr<IIncomingData>;


class IOutgoingData {
 public:
  virtual ~IOutgoingData() = default;

  virtual std::uint32_t GetType() const = 0;

  virtual std::size_t GetPayloadSize() const = 0;
  // Read part of payload data
  // can be called multiple times, each time new data is returned
  // returns empty buffer if no more data to read
  virtual void ReadData(boost::asio::mutable_buffer buffer,
                        std::function<void(boost::system::error_code, std::size_t)> callback) = 0;
};

using OutgoingDataPtr = std::shared_ptr<IOutgoingData>;


class IConnectionBase {
 public:
  virtual ~IConnectionBase() = default;

  virtual void Run() = 0;
};

using BaseConnectionPtr = std::shared_ptr<IConnectionBase>;


class IConnection : public IConnectionBase {
 public:
  virtual void Write(OutgoingDataPtr data) = 0;
  virtual void Write(OutgoingDataPtr data, std::function<void()> callback) = 0;
  virtual void Read(boost::asio::mutable_buffer buffer,
                    std::function<void(boost::system::error_code, std::size_t)> callback) = 0;
  virtual void Close() = 0;
  virtual bool IsOpen() const = 0;
};


class IHeader {
 public:
  virtual ~IHeader() = default;

  virtual std::size_t size() const = 0;
};

class IIncomingHeader : public IHeader {
 public:
  virtual void* data() = 0;

  virtual void Decode() = 0;
};

class IOutgoingHeader : public IHeader {
 public:
  virtual const void* data() const = 0;

  virtual void Fill(OutgoingDataPtr data) = 0;
};


class IRequestFactory {
 public:
  virtual ~IRequestFactory() = default;

  virtual IncomingDataPtr CreateRequest(const IIncomingHeader& header) = 0;
};


class IProcessor {
 public:
  virtual ~IProcessor() = default;

  virtual void ProcessRequest(IncomingDataPtr request, std::function<void(OutgoingDataPtr)> callback) = 0;
};


using boost::asio::ip::tcp;

template<class IncomingHeader, class OutgoingHeader>
class Connection : public IConnection, public std::enable_shared_from_this<Connection<IncomingHeader, OutgoingHeader> > {
 public:
  Connection(tcp::socket socket, IRequestFactory* factory, IProcessor* processor)
      : socket_(std::move(socket)), request_factory_(factory), processor_(processor), payload_buffer_(8192) {
    assert(request_factory_);
    assert(processor_);
  }

  void Read(boost::asio::mutable_buffer buffer, std::function<void(boost::system::error_code, std::size_t)> callback) override {
    auto sthis = this->shared_from_this();
    boost::asio::post(
        socket_.get_executor(),
        [this, sthis, buffer, callback]() {
          boost::asio::async_read(socket_, buffer, callback);
        });
  }

  void Run() override {
    ReadRequestHeader();
  }

  void Write(OutgoingDataPtr reply) override {
    Write(reply, []() {});
  }

  void Write(OutgoingDataPtr reply, std::function<void()> callback) override {
    if (!reply)
      return;

    auto sthis = this->shared_from_this();
    boost::asio::post(
        socket_.get_executor(),
        [this, sthis, reply, callback]() {
          bool write_in_progress = !send_queue_.empty();
          send_queue_.push({reply, callback});
          if (!write_in_progress)
            sthis->DoSendReply();
        });
  }

  void Close() override {
    auto sthis = this->shared_from_this();
    boost::asio::post(socket_.get_executor(), [this, sthis]() { socket_.close(); });
  }

  bool IsOpen() const override {
    return socket_.is_open();
  }

 protected:
  tcp::socket& socket() { return socket_; }

 private:
  void ReadRequestHeader() {
    auto sthis = this->shared_from_this();
    boost::asio::async_read(
        socket_, boost::asio::buffer(incoming_header_.data(), incoming_header_.size()),
        [this, sthis](boost::system::error_code error, std::size_t /*length*/) {
          if (!CloseOnError(error)) {
            incoming_header_.Decode();
            sthis->ProcessRequest();
          }
        });
  }

  void DoSendReply() {
    OutgoingDataPtr reply = send_queue_.front().first;
    payload_bytes_left_ = reply->GetPayloadSize();

    outgoing_header_.Fill(reply);

    auto sthis = this->shared_from_this();
    boost::asio::async_write(
        socket_, boost::asio::const_buffer(outgoing_header_.data(), outgoing_header_.size()),
        [sthis, reply](boost::system::error_code error, std::size_t /*length*/) {
          if (!sthis->CloseOnError(error)) {
            if (reply->GetPayloadSize() == 0) {
              sthis->CompleteReplySending();
            } else {
              sthis->SendReplyPayload();
            }
          }
        });
  }

  void CompleteReplySending() {
    assert(!send_queue_.empty());
    VoidCallback callback = send_queue_.front().second;
    send_queue_.pop();
    boost::asio::post(socket_.get_executor(), callback);
    if (!send_queue_.empty())
      DoSendReply();
  }

  void SendReplyPayload() {
    assert(!send_queue_.empty());
    assert(payload_bytes_left_ > 0);
    memset(payload_buffer_.data(), 0, payload_buffer_.size());
    auto sthis = this->shared_from_this();
    send_queue_.front().first->ReadData(
        boost::asio::buffer(payload_buffer_.data(), payload_buffer_.size()),
        [this, sthis](boost::system::error_code error, std::size_t length) {
          if (!CloseOnError(error)) {
            payload_bytes_left_ -= length;
            sthis->WritePayloadBuffer(length);
          }
        });
  }

  void WritePayloadBuffer(std::size_t size) {
    assert(size > 0);
    auto sthis = this->shared_from_this();
    boost::asio::async_write(
        socket_, boost::asio::const_buffer(payload_buffer_.data(), size),
        [this, sthis](boost::system::error_code error, std::size_t /*length*/) {
          if (!CloseOnError(error)) {
            if (payload_bytes_left_ == 0) {
              sthis->CompleteReplySending();
            } else {
              sthis->SendReplyPayload();
            }
          }
        });
  }

  void ProcessRequest() {
    assert(request_factory_);
    IncomingDataPtr request = request_factory_->CreateRequest(incoming_header_);
    if (!request) {
      Close();
      return;
    }
    assert(request);
    auto sthis = this->shared_from_this();
    request->ReadPayload(
        sthis,
        [this, sthis, request]() {
          processor_->ProcessRequest(request, [sthis](OutgoingDataPtr reply) { sthis->Write(reply); });
          boost::asio::post(socket_.get_executor(), [sthis]() { sthis->ReadRequestHeader(); });
        });
  }

  bool CloseOnError(boost::system::error_code error) {
    if (error)
      Close();
    return !!error;
  }

  tcp::socket socket_;
  IRequestFactory* request_factory_;
  IProcessor* processor_;

  using VoidCallback = std::function<void()>;
  using SendItem = std::pair<OutgoingDataPtr, VoidCallback>;
  std::queue<SendItem> send_queue_;

  IncomingHeader incoming_header_;
  OutgoingHeader outgoing_header_;

  std::vector<std::uint8_t> payload_buffer_;
  std::size_t payload_bytes_left_;
};

#endif  // CONNECTION_HPP
