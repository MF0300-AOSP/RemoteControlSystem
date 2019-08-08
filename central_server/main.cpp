#include <memory>
#include <utility>

#include "device_commands.hpp"
#include "device_requests.hpp"
#include "tcp_server.hpp"
#include "http_session.hpp"
#include "web_api_handler.hpp"

namespace server {

class DeviceRequestFactory : public IRequestFactory {
 public:
  explicit DeviceRequestFactory(DeviceManager* device_manager)
      : device_manager_(device_manager) {}

  IncomingDataPtr CreateRequest(const IIncomingHeader& iheader) override {
    const auto& header = static_cast<const DeviceRequestHeader&>(iheader);
    switch (static_cast<DeviceRequestType>(header.GetRequestType())) {
      case DeviceRequestType::kSystemInfo:
        return std::make_shared<UpdateSystemInfoRequest>(device_manager_, header.GetPayloadSize());
      case DeviceRequestType::kUpdateLocation:
        return std::make_shared<UpdateLocationRequest>(device_manager_, header.GetPayloadSize());
      case DeviceRequestType::kInstallPackageReply:
        return std::make_shared<InstallPackageReply>(header.GetPayloadSize());
      case DeviceRequestType::kUninstallPackageReply:
        return std::make_shared<UninstallPackageReply>(header.GetPayloadSize());
      case DeviceRequestType::kListInstalledPackagesReply:
        return std::make_shared<ListInstalledPackagesReply>(header.GetPayloadSize());
      case DeviceRequestType::kRebootReply:
        return std::make_shared<RebootReply>();
      case DeviceRequestType::kLogcatReply:
        return std::make_shared<LogcatReply>(header.GetPayloadSize());
      case DeviceRequestType::kDmesgReply:
        return std::make_shared<DmesgReply>(header.GetPayloadSize());
    }
    return IncomingDataPtr();
  }

 private:
  DeviceManager* device_manager_;
};


class DeviceConnectionFactory : public IConnectionFactory {
 public:
  DeviceConnectionFactory(DeviceManager* device_manager, DeviceRequestProcessor* processor)
      : requests_factory_(device_manager),
        request_processor_(processor),
        connection_tracker_(device_manager) {}

  BaseConnectionPtr CreateConnection(tcp::socket socket) override {
    return std::make_shared<DeviceConnection>(std::move(socket), &requests_factory_, request_processor_, connection_tracker_);
  }

 private:
  DeviceRequestFactory requests_factory_;
  DeviceRequestProcessor* request_processor_;
  IConnectionTracker* connection_tracker_;
};


class HttpSessionFactory : public IConnectionFactory {
 public:
  HttpSessionFactory(DeviceManager* device_manager, DeviceRequestProcessor* processor)
      : api_handler_(device_manager, processor) {}

  BaseConnectionPtr CreateConnection(tcp::socket socket) override {
    return std::make_shared<HttpSession<ApiHandler>>(std::move(socket), &api_handler_);
  }

 private:
  ApiHandler api_handler_;
};


class Server {
 public:
  explicit Server(boost::asio::io_context& io_context)
      : device_connection_factory_(&device_manager_, &device_processor_),
        http_session_factory_(&device_manager_, &device_processor_),
        device_server_(io_context, 7878, &device_connection_factory_),
        web_server_(io_context, 8080, &http_session_factory_) {
  }

 private:
  DeviceManager device_manager_;
  DeviceRequestProcessor device_processor_;

  DeviceConnectionFactory device_connection_factory_;
  HttpSessionFactory http_session_factory_;

  TcpServer device_server_;
  TcpServer web_server_;
};

}  // namespace server


int main() {
  boost::asio::io_context io_context;
  server::Server s(io_context);
  io_context.run();
  return 0;
}
