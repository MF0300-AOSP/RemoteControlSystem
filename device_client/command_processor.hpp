#ifndef COMMAND_PROCESSOR_HPP
#define COMMAND_PROCESSOR_HPP

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <boost/core/ignore_unused.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/process.hpp>

#include "update_android_info_request.hpp"
#include "device_location.hpp"
#include "upload_file_reply.hpp"

namespace client {

class UpdateLocationRequest final : public IOutgoingData {
 public:
  explicit UpdateLocationRequest(const DeviceLocation& location) {
    std::string buffer = DeviceLocation::Serialize(location);
    std::ostream out(&payload_);
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  }

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceRequestType::kUpdateLocation); }

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


class InstallPackageRequest final : public IIncomingData, public std::enable_shared_from_this<InstallPackageRequest> {
 public:
  explicit InstallPackageRequest(std::size_t payload_size)
    : apk_data_size_(payload_size),
      buffer_(8192),
      apk_file_name_(std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".apk"),
      apk_file_stream_(apk_file_name_, std::ios::binary) {
  }

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceCommand::kInstallPackage); }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    connection_ = connection;
    read_callback_ = callback;

    ReadData();
  }

  std::string GetApkFileName() const { return apk_file_name_; }

 private:
  void ReadData() {
    if (apk_data_size_ == 0) {
      apk_file_stream_.close();
      // don't leave callback as class member, this can prevent object destruction
      // in case when callback holds (captured) shared pointer to this object
      decltype(read_callback_) callback;
      read_callback_.swap(callback);
      assert(!read_callback_);
      callback();
      return;
    }

    auto sthis = shared_from_this();
    std::size_t read_size = std::min(buffer_.size(), apk_data_size_);
    connection_->Read(
        boost::asio::buffer(buffer_.data(), read_size),
        [this, sthis](boost::system::error_code error, std::size_t size) {
          if (!error) {
            apk_data_size_ -= size;
            apk_file_stream_.write(buffer_.data(), static_cast<std::streamsize>(size));
            sthis->ReadData();
          }
        });
  }

  ConnectionPtr connection_;
  std::function<void()> read_callback_;

  std::size_t apk_data_size_;
  std::vector<char> buffer_;

  const std::string apk_file_name_;
  std::ofstream apk_file_stream_;
};

class InstallPackageReply final : public SimpleReply {
 public:
  using SimpleReply::SimpleReply;

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceRequestType::kInstallPackageReply); }
};


class UninstallPackageRequest final : public IIncomingData, public std::enable_shared_from_this<UninstallPackageRequest> {
 public:
  explicit UninstallPackageRequest(std::size_t payload_size) : package_name_(payload_size, '\0') {}

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceCommand::kUninstallPackage); }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    if (package_name_.size() == 0) {
      callback();
      return;
    }

    auto sthis = shared_from_this();
    connection->Read(
        boost::asio::buffer(&package_name_[0], package_name_.size()),
        [sthis, callback](boost::system::error_code, std::size_t) {
          callback();
        });
  }

  std::string GetPackageName() const { return package_name_; }

 private:
  std::string package_name_;
};

class UninstallPackageReply final : public SimpleReply {
 public:
  using SimpleReply::SimpleReply;

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceRequestType::kUninstallPackageReply); }
};


class ListInstalledPackagesRequest : public IIncomingData {
 public:
  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceCommand::kListInstalledPackages); }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    (void) connection;
    callback();
  }
};

class ListInstalledPackagesReply final : public SimpleReply {
 public:
  using SimpleReply::SimpleReply;

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceRequestType::kListInstalledPackagesReply); }
};


class RebootRequest : public IIncomingData {
 public:
  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceCommand::kReboot); }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    (void) connection;
    callback();
  }
};

class RebootReply : public IOutgoingData {
 public:
  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceRequestType::kRebootReply); }

  std::size_t GetPayloadSize() const override { return 2; }

  void ReadData(boost::asio::mutable_buffer buffer,
                std::function<void(boost::system::error_code, std::size_t)> callback) override {
    boost::ignore_unused(buffer);
    callback(boost::system::errc::make_error_code(boost::system::errc::success), 0);
  }
};


class LogcatRequest : public IIncomingData {
 public:
  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceCommand::kLogcat); }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    (void) connection;
    callback();
  }
};

class LogcatReply final : public UploadFileReply {
 public:
  using UploadFileReply::UploadFileReply;

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceRequestType::kLogcatReply); }
};


class DmesgRequest : public IIncomingData {
 public:
  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceCommand::kDmesg); }

  void ReadPayload(ConnectionPtr connection, std::function<void()> callback) override {
    (void) connection;
    callback();
  }
};

class DmesgReply final : public UploadFileReply {
 public:
  using UploadFileReply::UploadFileReply;

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceRequestType::kDmesgReply); }
};


std::string exec(std::string cmd) {
  std::array<char, 512> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (pipe) {
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
      result += buffer.data();
    }
  }
  return result;
}


class ServerCommandProcessor : public IProcessor {
 public:
  void ProcessRequest(IncomingDataPtr request, std::function<void(OutgoingDataPtr)> callback) override {
    OutgoingDataPtr reply;
    switch (static_cast<DeviceCommand>(request->GetType())) {
      case DeviceCommand::kInstallPackage: {
        auto install_request = std::static_pointer_cast<InstallPackageRequest>(request);
        std::string cmd_out = exec("pm install " + install_request->GetApkFileName() + " 2>&1");
        remove(install_request->GetApkFileName().c_str());
        reply = std::make_shared<InstallPackageReply>(cmd_out);
        break;
      }
      case DeviceCommand::kUninstallPackage: {
        auto uninstall_request = std::static_pointer_cast<UninstallPackageRequest>(request);
        std::string cmd_out = exec("pm uninstall " + uninstall_request->GetPackageName() + " 2>&1");
        reply = std::make_shared<UninstallPackageReply>(cmd_out);
        break;
      }
      case DeviceCommand::kListInstalledPackages: {
        // pm returns packages list in which line is prefixed with 'package:'
        // thats ugly, so remove this prefix using sed
        std::string cmd_out = exec("pm list packages | sed 's/^package://g' 2>&1");
        reply = std::make_shared<ListInstalledPackagesReply>(cmd_out);
        break;
      }
      case DeviceCommand::kReboot: {
        std::string reboot_script = "r.sh";
        std::ofstream script_stream(reboot_script);
        script_stream << "sleep 3\n" << "reboot\n";
        script_stream.close();
        // unfortunately, only system() call supports sending process to background (& at the end)
        // any other methods I tried ignore (or somehow interpret) '&' symbol at the end and call
        // blocks until command ends. blocking call is not acceptable here, because I want to sent
        // some reply before device reboot, just to confirm that command was delivered to device.
        system(("nohup sh " + reboot_script + "&").c_str());
        reply = std::make_shared<RebootReply>();
        break;
      }
      case DeviceCommand::kLogcat: {
        std::string log = std::to_string(reinterpret_cast<std::uintptr_t>(request.get())) + ".log";
        boost::process::system("logcat -d", boost::process::std_out > log);
        reply = std::make_shared<LogcatReply>(log, true);
        break;
      }
      case DeviceCommand::kDmesg: {
        std::string log = std::to_string(reinterpret_cast<std::uintptr_t>(request.get())) + ".log";
        boost::process::system("dmesg", boost::process::std_out > log);
        reply = std::make_shared<DmesgReply>(log, true);
        break;
      }
    }
    callback(reply);
  }
};

}  // namespace client

#endif  // COMMAND_PROCESSOR_HPP
