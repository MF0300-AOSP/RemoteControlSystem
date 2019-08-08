#ifndef DEVICE_CONNECTION_HPP
#define DEVICE_CONNECTION_HPP

#include <boost/endian/conversion.hpp>

#include "connection.hpp"
#include "device_protocol.h"

namespace client {

using boost::asio::ip::tcp;

template<class IncomingHeader, class OutgoingHeader>
class ClientConnection : public Connection<IncomingHeader, OutgoingHeader> {
 public:
  using Connection<IncomingHeader, OutgoingHeader>::Connection;

  void Connect(const tcp::resolver::results_type& endpoints) {
    auto sthis = std::static_pointer_cast<ClientConnection>(this->shared_from_this());
    boost::asio::async_connect(
        this->socket(), endpoints,
        [this, sthis](boost::system::error_code error, tcp::endpoint) {
          if (!error) {
            this->socket().set_option(tcp::no_delay(true));
            sthis->Run();
          }
        });
  }

  void Close() {
    auto sthis = std::static_pointer_cast<ClientConnection>(this->shared_from_this());
    boost::asio::post(this->socket().get_executor(), [this, sthis]() { this->socket().close(); });
  }
};


class DeviceRequestHeader final : public IOutgoingHeader {
 public:
  const void* data() const override { return &header_; }
  std::size_t size() const override { return sizeof(header_); }

  void Fill(OutgoingDataPtr data) override {
    header_.request_type = boost::endian::native_to_big(data->GetType());
    header_.payload_size = boost::endian::native_to_big(static_cast<std::uint32_t>(data->GetPayloadSize()));
  }

 private:
  DeviceDataHeader header_;
};


class ServerMessageHeader final : public IIncomingHeader {
 public:
  void* data() override { return &header_; }
  std::size_t size() const override { return sizeof(header_); }

  void Decode() override {
    header_.message_type = boost::endian::big_to_native(header_.message_type);
    header_.payload_size = boost::endian::big_to_native(header_.payload_size);
  }

  std::uint32_t GetMessageType() const { return header_.message_type; }
  std::uint32_t GetPayloadSize() const { return header_.payload_size; }

 private:
  ServerDataHeader header_;
};


using DeviceClientConnection = ClientConnection<ServerMessageHeader, DeviceRequestHeader>;

}  // namespace client

#endif  // DEVICE_CONNECTION_HPP
