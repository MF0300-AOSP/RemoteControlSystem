#ifndef DEVICE_COMMANDS_HPP
#define DEVICE_COMMANDS_HPP

#include <list>
#include <sstream>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/core/ignore_unused.hpp>

#include "connection.hpp"
#include "device_requests.hpp"

namespace server {

template<DeviceCommand Command>
class EmptyRequest final : public IOutgoingData {
 public:
  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(Command); }

  std::size_t GetPayloadSize() const override { return 0; }

  void ReadData(boost::asio::mutable_buffer buffer,
                std::function<void(boost::system::error_code, std::size_t)> callback) override {
    boost::ignore_unused(buffer);
    callback(boost::system::errc::make_error_code(boost::system::errc::success), 0);
  }
};


template<DeviceCommand Command>
class SimpleRequest final : public IOutgoingData {
 public:
  explicit SimpleRequest(const std::string& payload) {
    std::ostream out(&payload_);
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  }

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(Command); }

  std::size_t GetPayloadSize() const override { return payload_.size(); }

  void ReadData(boost::asio::mutable_buffer buffer,
                std::function<void(boost::system::error_code, std::size_t)> callback) override {
    std::istream in(&payload_);
    in.read(static_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    namespace errc = boost::system::errc;
    callback(errc::make_error_code(errc::success), static_cast<std::size_t>(in.gcount()));
  }

 private:
  boost::asio::streambuf payload_;
};


template<DeviceRequestType Reply>
class EmptyReply final : public IIncomingData {
 public:
  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(Reply); }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    boost::ignore_unused(connection);
    callback();
  }
};


class ReplyBase : public IIncomingData, public std::enable_shared_from_this<ReplyBase> {
 public:
  explicit ReplyBase(std::size_t payload_size) : payload_(payload_size, '\0') {}

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) final {
    auto sthis = this->shared_from_this();
    connection->Read(
        boost::asio::buffer(payload_),
        [this, sthis, callback](boost::system::error_code error, std::size_t) {
          read_error_ = error;
          if (!error)
            sthis->ProcessPayload(payload_);
          callback();
        });
  }

  const boost::system::error_code& GetLastError() const { return read_error_; }
  const std::string& GetRawPayload() const { return payload_; }

 protected:
  virtual void ProcessPayload(const std::string& payload) {
    boost::ignore_unused(payload);
  }

 private:
  std::string payload_;
  boost::system::error_code read_error_;
};

template<DeviceRequestType Reply>
class SimpleReply : public ReplyBase {
 public:
  using ReplyBase::ReplyBase;

  std::uint32_t GetType() const override final { return static_cast<std::uint32_t>(Reply); }
};


// server -> device
using InstallPackageRequest = SimpleRequest<DeviceCommand::kInstallPackage>;
// device -> server
using InstallPackageReply = SimpleReply<DeviceRequestType::kInstallPackageReply>;


// server -> device
using UninstallPackageRequest = SimpleRequest<DeviceCommand::kUninstallPackage>;
// device -> server
using UninstallPackageReply = SimpleReply<DeviceRequestType::kUninstallPackageReply>;

// server -> device
using ListInstalledPackagesRequest = EmptyRequest<DeviceCommand::kListInstalledPackages>;
// device -> server
class ListInstalledPackagesReply final : public SimpleReply<DeviceRequestType::kListInstalledPackagesReply> {
 public:
  using SimpleReply::SimpleReply;

  const std::list<std::string>& GetPackagesList() const { return packages_; }

 protected:
  void ProcessPayload(const std::string& payload) override {
    std::stringstream ss(payload);
    std::string line;
    while (std::getline(ss, line)) {
      boost::algorithm::trim(line);
      if (!line.empty())
        packages_.push_back(line);
    }
    packages_.sort();
  }

 private:
  std::list<std::string> packages_;
};


// server -> device
using RebootRequest = EmptyRequest<DeviceCommand::kReboot>;
// device -> server
using RebootReply = EmptyReply<DeviceRequestType::kRebootReply>;

// server -> device
using LogcatRequest = EmptyRequest<DeviceCommand::kLogcat>;
// device -> server
using LogcatReply = SimpleReply<DeviceRequestType::kLogcatReply>;

// server -> device
using DmesgRequest = EmptyRequest<DeviceCommand::kDmesg>;
// device -> server
using DmesgReply = SimpleReply<DeviceRequestType::kDmesgReply>;

}  // namespace server

#endif  // DEVICE_COMMANDS_HPP
