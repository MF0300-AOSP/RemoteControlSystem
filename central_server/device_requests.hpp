#ifndef DEVICE_REQUESTS_HPP
#define DEVICE_REQUESTS_HPP

#include <array>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "device_manager.hpp"

namespace server {

// for debug
std::ostream& operator<<(std::ostream& out, const DeviceLocation& location) {
  out << location.latitude() << "/" << location.longitude() << "/" << location.city() << "/" << location.country();
  return out;
}

class DeviceRequestProcessor : public IProcessor {
 public:
  void ProcessRequest(IncomingDataPtr request, std::function<void(OutgoingDataPtr)> callback) override {
    DeviceRequestType req_type = static_cast<DeviceRequestType>(request->GetType());

    std::list<HandlerType> reply_handlers;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      auto iter = reply_handlers_.find(req_type);
      if (iter != reply_handlers_.end()) {
        reply_handlers.swap(iter->second);
        reply_handlers_.erase(iter);
      }
    }

    for (auto& handler : reply_handlers) {
      if (handler) {
        handler(request);
      }
    }

    callback(OutgoingDataPtr());  // nothing must be send back to device
  }

  using HandlerType = std::function<void(IncomingDataPtr)>;

  void WaitDeviceReply(DeviceRequestType device_reply, HandlerType handler) {
    std::unique_lock<std::mutex> lock(mutex_);
    reply_handlers_[device_reply].push_back(handler);
  }

 private:
  std::mutex mutex_;
  std::map<DeviceRequestType, std::list<HandlerType> > reply_handlers_;
};


class UpdateLocationRequest final : public IIncomingData, public std::enable_shared_from_this<UpdateLocationRequest> {
 public:
  UpdateLocationRequest(DeviceManager* device_manager, std::size_t payload_size)
      : device_manager_(device_manager), payload_(payload_size, '\0') {}

  std::uint32_t GetType() const override {
    return static_cast<std::uint32_t>(DeviceRequestType::kUpdateLocation);
  }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    auto sthis = shared_from_this();
    connection->Read(
        boost::asio::buffer(payload_),
        [this, sthis, callback, connection](boost::system::error_code error, std::size_t) {
          if (!error) {
            DeviceLocation location = DeviceLocation::Deserialize(payload_);
            std::cout << location << std::endl;
            device_manager_->UpdateDeviceLocation(connection.get(), location);
          }
          callback();
        });
  }

 private:
  DeviceManager* device_manager_;
  std::string payload_;
};


class UpdateSystemInfoRequest final : public IIncomingData, public std::enable_shared_from_this<UpdateSystemInfoRequest> {
 public:
  UpdateSystemInfoRequest(DeviceManager* device_manager, std::size_t payload_size)
      : device_manager_(device_manager), payload_size_(payload_size) {}

  std::uint32_t GetType() const override {
    return static_cast<std::uint32_t>(DeviceRequestType::kSystemInfo);
  }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    if (payload_size_ == 0) {
      callback();
      return;
    }

    connection_ = connection;
    read_callback_ = callback;

    ReadFieldSizes();
  }

  void ReadFieldSizes() {
    auto sthis = shared_from_this();
    connection_->Read(
        boost::asio::buffer(field_sizes_),
        [this, sthis](boost::system::error_code error, std::size_t) {
          if (!error) {
            os_version_.resize(field_sizes_[0]);
            serial_number_.resize(field_sizes_[1]);
            build_number_.resize(field_sizes_[2]);

            sthis->ReadOsVersion();
          } else {
            sthis->CallCallback();
          }
        });
  }

  void ReadOsVersion() {
    auto sthis = shared_from_this();
    connection_->Read(
        boost::asio::buffer(os_version_),
        [sthis](boost::system::error_code error, std::size_t) {
          if (!error)
            sthis->ReadSerialNumber();
          else
            sthis->CallCallback();
        });
  }

  void ReadSerialNumber() {
    auto sthis = shared_from_this();
    connection_->Read(
        boost::asio::buffer(serial_number_),
        [sthis](boost::system::error_code error, std::size_t) {
          if (!error)
            sthis->ReadBuildNumber();
          else
            sthis->CallCallback();
        });
  }

  void ReadBuildNumber() {
    auto sthis = shared_from_this();
    connection_->Read(
        boost::asio::buffer(build_number_),
        [this, sthis](boost::system::error_code error, std::size_t) {
          if (!error)
            device_manager_->UpdateSystemInfo(connection_.get(),
                                              SystemInfo(std::move(os_version_),
                                                         std::move(build_number_),
                                                         std::move(serial_number_)));
          sthis->CallCallback();
        });
  }

  void CallCallback() {
    decltype(read_callback_) callback;
    read_callback_.swap(callback);
    callback();
  }

 private:
  DeviceManager* device_manager_;
  std::size_t payload_size_;

  ConnectionPtr connection_;
  std::function<void()> read_callback_;

  std::array<std::uint8_t, 4> field_sizes_;
  std::string os_version_;
  std::string serial_number_;
  std::string build_number_;
};

}  // namespace server

#endif  // DEVICE_REQUESTS_HPP
