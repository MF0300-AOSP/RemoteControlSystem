#include <log/log.h>
#include <selinux/android.h>

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <regex>
#include <utility>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "device_connection.hpp"
#include "command_processor.hpp"

namespace client {

class ServerRequestFactory : public IRequestFactory {
 public:
  IncomingDataPtr CreateRequest(const IIncomingHeader& iheader) override {
    const auto& header = static_cast<const ServerMessageHeader&>(iheader);
    switch (static_cast<DeviceCommand>(header.GetMessageType())) {
      case DeviceCommand::kInstallPackage:
        return std::make_shared<InstallPackageRequest>(header.GetPayloadSize());
      case DeviceCommand::kUninstallPackage:
        return std::make_shared<UninstallPackageRequest>(header.GetPayloadSize());
      case DeviceCommand::kListInstalledPackages:
        return std::make_shared<ListInstalledPackagesRequest>();
      case DeviceCommand::kReboot:
        return std::make_shared<RebootRequest>();
      case DeviceCommand::kLogcat:
        return std::make_shared<LogcatRequest>();
      case DeviceCommand::kDmesg:
        return std::make_shared<DmesgRequest>();
    }
    return IncomingDataPtr();
  }
};


class DeviceClient {
 public:
  DeviceClient(boost::asio::io_context& io_context,
               std::string host, std::string port)
      : timer_(io_context),
        location_(
          []() -> DeviceLocation {
              std::string output = exec("location_finder");
              std::stringstream ss(output);

              boost::property_tree::ptree json;
              boost::property_tree::read_json(ss, json);

              std::regex ce("(.*) \\(.*\\)");
              std::smatch csm;
              std::string country = json.get<std::string>("country", "Unknown");
              std::regex_match(country, csm, ce);
              assert(csm.size() >= 2);

              return DeviceLocation(json.get<double>("latitude", 0.0),
                                    json.get<double>("longitude", 0.0),
                                    json.get<std::string>("city", "Unknown"),
                                    csm[1]);
          }()) {
    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(host, port);

    tcp::socket socket(io_context);
    connection_ = std::make_shared<DeviceClientConnection>(std::move(socket), &request_factory_, &processor_);
    connection_->Connect(endpoints);

    StartTimer();
    SendLocation();
    SendSystemInfo();
  }

  ~DeviceClient() {
    connection_->Close();
    timer_.cancel();
  }

 private:
  void StartTimer() {
    timer_.expires_after(std::chrono::seconds(30));
    timer_.async_wait(
        [this](boost::system::error_code error) {
          if (!error) {
            SendLocation();
            StartTimer();
          }
        });
  }

  void SendLocation() {
    // IP based location pretty inaccurate, so try to get coordinates from Android services
    std::regex ll_re(" *network: Location\\[network (-?\\d+\\.\\d+),(-?\\d+.\\d+).*");
    std::smatch sm;

    std::string output = exec("dumpsys location");
    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
      if (std::regex_match(line, sm, ll_re)) {
        assert(sm.size() >= 3);
        location_ = DeviceLocation(std::stod(sm[1]), std::stod(sm[2]), location_.city(), location_.country());
        break;
      }
    }

    connection_->Write(std::make_shared<UpdateLocationRequest>(location_));
  }

  void SendSystemInfo() {
    connection_->Write(std::make_shared<UpdateAndroidInfoRequest>());
  }

  std::shared_ptr<DeviceClientConnection> connection_;
  ServerRequestFactory request_factory_;
  ServerCommandProcessor processor_;
  boost::asio::steady_timer timer_;
  DeviceLocation location_;
};

}  // namespace client

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  std::string port = "7878";

  if (argc >= 2)
    host = argv[1];

  if (argc >= 3)
    port = argv[2];

  // Even with "late_start" class Android init starts this service before network becomes ready.
  // This is not big problem, service can be restarted later (thanks to init), but...
  // when network becomes ready something called "nf_conntrack" get initialized and any
  // already established connections are dropped (looks like this "nf_conntrack" just closes
  // any opened sockets). Right now client can't detect such situation, so I just delayed
  // service startup. Delay timeout must be passed as 3rd argument, value in seconds.
  // 5 seconds (or even 2-3) must be enough, but in rc script I set it to 15. Late init is not a problem.
  if (argc == 4)
    sleep(std::stoi(argv[3]));

  // Service needs some storage to save intermediate files (apk from user or logs for user),
  // so just change working directory to default Android temp directory
  LOG_ALWAYS_FATAL_IF(chdir("/data/local/tmp") < 0, "Could not change working directory");
  // Service must do a lot of stuff that requires a lot of permissions, it is not enough to run service
  // as root, appropriate SELinux domain is also required, use 'su' domain, it default for root user
  LOG_ALWAYS_FATAL_IF(selinux_android_setcon("u:r:su:s0") < 0, "Could not set SELinux context");

  boost::asio::io_context io_context;
  client::DeviceClient client(io_context, host, port);
  io_context.run();
  return 0;
}
