#ifndef DEVICE_CONNECTION_HPP
#define DEVICE_CONNECTION_HPP

#include <utility>

#include <boost/endian/conversion.hpp>

#include "connection.hpp"
#include "device_protocol.h"

namespace server {

class DeviceRequestHeader final : public IIncomingHeader {
 public:
  void* data() override { return &header_; }
  std::size_t size() const override { return sizeof(header_); }

  void Decode() override {
    header_.request_type = boost::endian::big_to_native(header_.request_type);
    header_.payload_size = boost::endian::big_to_native(header_.payload_size);
  }

  std::uint32_t GetRequestType() const { return header_.request_type; }
  std::uint32_t GetPayloadSize() const { return header_.payload_size; }

 private:
  DeviceDataHeader header_;
};


class ServerMessageHeader final : public IOutgoingHeader {
 public:
  const void* data() const override { return &header_; }
  std::size_t size() const override { return sizeof(header_); }

  void Fill(OutgoingDataPtr data) override {
    header_.message_type = boost::endian::native_to_big(data->GetType());
    header_.payload_size = boost::endian::native_to_big(static_cast<std::uint32_t>(data->GetPayloadSize()));
  }

 private:
  ServerDataHeader header_;
};


class IConnectionTracker {
 public:
  virtual ~IConnectionTracker() = default;

  virtual void ConnectionCreated(IConnection* connection) = 0;
  virtual void ConnectionDestroyed(IConnection* connection) = 0;
};


class DeviceConnection : public Connection<DeviceRequestHeader, ServerMessageHeader> {
 public:
  DeviceConnection(tcp::socket socket, IRequestFactory* factory, IProcessor* processor, IConnectionTracker* tracker)
      : Connection<DeviceRequestHeader, ServerMessageHeader>(std::move(socket), factory, processor),
        connection_tracker_(tracker) {
    if (connection_tracker_)
      connection_tracker_->ConnectionCreated(this);
  }

  ~DeviceConnection() {
    if (connection_tracker_)
      connection_tracker_->ConnectionDestroyed(this);
  }

 private:
  IConnectionTracker* connection_tracker_;
};

}  // namespace server

#endif  // DEVICE_CONNECTION_HPP
