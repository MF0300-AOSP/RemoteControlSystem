#ifndef WEB_API_HANDLER_H
#define WEB_API_HANDLER_H

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/beast.hpp>

#include <nlohmann/json.hpp>

#include "device_commands.hpp"

namespace server {

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>


std::string GetDeviceNameFromSerial(const std::string& serial) {
  std::string code = serial.substr(0, 2);
  if (code == "HT")
    return std::string("Echo");
  if (code == "PP")
    return std::string("Elite");
  return std::string("unknown");
}

template<class Body, class Allocator>
http::response<http::string_body> CreateResponse(
    const http::request<Body, http::basic_fields<Allocator>>& req,
    http::status status, beast::string_view content, beast::string_view mimetype) {
  http::response<http::string_body> res{status, req.version()};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, mimetype);
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(req.keep_alive());
  res.body() = std::string(content);
  res.prepare_payload();
  return res;
}

template<class Body, class Allocator>
http::response<http::string_body> CreateBadRequestResponse(
    const http::request<Body, http::basic_fields<Allocator>>& req,
    beast::string_view why) {
  return CreateResponse(req, http::status::bad_request, why, "text/html");
}

template<class Body, class Allocator>
http::response<http::string_body> CreateNotFoundResponse(
    const http::request<Body, http::basic_fields<Allocator>>& req,
    beast::string_view target) {
  std::string body = "The resource '" + std::string(target) + "' was not found.";
  return CreateResponse(req, http::status::not_found, body, "text/html");
}

template<class Body, class Allocator>
http::response<http::string_body> CreateServerErrorResponse(
    const http::request<Body, http::basic_fields<Allocator>>& req,
    beast::string_view what) {
  std::string body = "An error occurred: '" + std::string(what) + "'";
  return CreateResponse(req, http::status::internal_server_error, body, "text/html");
}

template<class Body, class Allocator>
http::response<http::string_body> CreateHttpOkResponse(
    const http::request<Body, http::basic_fields<Allocator>>& req,
    beast::string_view body, beast::string_view mimetype) {
  return CreateResponse(req, http::status::ok, body, mimetype);
}


class ApiHandler {
 public:
  ApiHandler(DeviceManager* device_manager, DeviceRequestProcessor* processor)
    : known_commands_({
          {"statistic", http::verb::get},
          {"list", http::verb::get},
          {"dmesg", http::verb::put},
          {"logcat", http::verb::put},
          {"restart", http::verb::put},
          {"applist", http::verb::get},
          {"appinstall", http::verb::post},
          {"appuninstall", http::verb::post}
      }),
      device_manager_(device_manager),
      device_processor_(processor) {}

  template<class Body, class Allocator, class Send>
  void HandleRequest(
      http::request<Body, http::basic_fields<Allocator>>&& req,
      Send&& send) {
    std::string target = std::string(req.target());
    boost::algorithm::trim_if(target, boost::is_any_of("/"));
    std::vector<std::string> parts;
    boost::algorithm::split(parts, target, boost::is_any_of("/"));

    if (parts.empty() || parts.front() != "devices") {
      send(CreateBadRequestResponse(req, "invalid prefix"));
      return;
    }

    auto command_iter = known_commands_.find(parts.back());
    if (command_iter == known_commands_.end()) {
      send(CreateBadRequestResponse(req, "unknown command"));
      return;
    }

    if (req.method() != command_iter->second) {
      send(CreateBadRequestResponse(req, "unsupported method"));
      return;
    }

    // /devices/statistic
    if (parts.back() == "statistic") {
      if (parts.size() != 2) {    // [devices, statistic]
        send(CreateBadRequestResponse(req, "invalid argument"));
        return;
      }

      DevicesStatistic(req, send);
      return;
    }

    // /devices/list
    if (parts.back() == "list") {
      if (parts.size() != 2) {    // [devices, list]
        send(CreateBadRequestResponse(req, "invalid argument"));
        return;
      }

      ListDevices(req, send);
      return;
    }

    // following API endpoints consist of 3 parts: /devices/<serian number>/<command>
    if (parts.size() != 3) {
      send(CreateBadRequestResponse(req, "invalid argument"));
      return;
    }

    const std::string& device_serial = parts[1];
    IConnection* device_connection = device_manager_->GetConnection(device_serial);
    if (!device_connection) {
      send(CreateNotFoundResponse(req, device_serial));
      return;
    }

    // /devices/<SN>/dmesg
    if (parts.back() == "dmesg") {
      DownloadDmesgLog(req, device_connection, device_serial, send);
      return;
    }

    // /devices/<SN>/logcat
    if (parts.back() == "logcat") {
      DownloadLogcatLog(req, device_connection, device_serial, send);
      return;
    }

    // /devices/<SN>/restart
    if (parts.back() == "restart") {
      RestartDevice(req, device_connection, send);
      return;
    }

    // /devices/<SN>/applist
    if (parts.back() == "applist") {
      ListInstalledPackages(req, device_connection, send);
      return;
    }

    // /devices/<SN>/appinstall
    if (parts.back() == "appinstall") {
      InstallPackage(req, device_connection, send);
      return;
    }

    // /devices/<SN>/appuninstall
    if (parts.back() == "appuninstall") {
      UninstallPackage(req, device_connection, send);
      return;
    }
  }

 private:
  template<class Body, class Allocator, class Send>
  void DevicesStatistic(const http::request<Body, http::basic_fields<Allocator>>& req, Send& send) {
    std::map<std::uint64_t, std::shared_ptr<IDeviceInfo> > devices;
    device_manager_->ListDevices(devices);

    std::unordered_set<std::string> countries;
    std::unordered_set<std::string> cities;
    std::size_t devices_count = 0;

    for (auto iter = devices.begin(); iter != devices.end(); ++iter) {
      if (!iter->second)
        continue;

      if (auto location = iter->second->GetLocation()) {
        countries.insert(location->country());
        cities.insert(location->city());
      }

      devices_count++;
    }

    nlohmann::json json = {};

    json["devicesCount"] = devices_count;
    json["citiesCount"] = cities.size();
    json["countriesCount"] = countries.size();

    send(CreateHttpOkResponse(req, json.dump(), "application/json"));
  }

  template<class Body, class Allocator, class Send>
  void ListDevices(const http::request<Body, http::basic_fields<Allocator>>& req, Send& send) {
    std::map<std::uint64_t, std::shared_ptr<IDeviceInfo> > devices;
    device_manager_->ListDevices(devices);

    nlohmann::json json = nlohmann::json::array();

    for (auto iter = devices.begin(); iter != devices.end(); ++iter) {
      const auto& device_info = iter->second;
      if (!device_info)
        continue;

      nlohmann::json device_node;
      device_node["sn"] = device_info->GetSerialNumber();
      device_node["deviceName"] = GetDeviceNameFromSerial(device_info->GetSerialNumber());
      device_node["osVersion"] = device_info->GetAndroidVersion();
      device_node["buildNumber"] = device_info->GetBuildNumber();
      device_node["status"] = static_cast<int>(device_info->GetStatus());

      if (auto location = device_info->GetLocation()) {
        device_node["city"] = location->city();
        device_node["country"] = location->country();

        nlohmann::json location_node;
        location_node["lat"] = location->latitude();
        location_node["lng"] = location->longitude();

        device_node["location"] = location_node;
      }
      json.push_back(device_node);
    }

    send(CreateHttpOkResponse(req, json.dump(), "application/json"));
  }

  template<class Body, class Allocator, class Send>
  void DownloadDmesgLog(
      const http::request<Body, http::basic_fields<Allocator>>& req,
      IConnection* device_connection, const std::string& serial, Send& send) {
    DownloadLog(req, device_connection,
                std::make_shared<DmesgRequest>(),
                DeviceRequestType::kDmesgReply,
                serial + "-dmesg.log", send);
  }

  template<class Body, class Allocator, class Send>
  void DownloadLogcatLog(
      const http::request<Body, http::basic_fields<Allocator>>& req,
      IConnection* device_connection, const std::string& serial, Send& send) {
    DownloadLog(req, device_connection,
                std::make_shared<LogcatRequest>(),
                DeviceRequestType::kLogcatReply,
                serial + "-logcat.log", send);
  }

  template<class Body, class Allocator, class Send>
  void RestartDevice(
      const http::request<Body, http::basic_fields<Allocator>>& req,
      IConnection* device_connection, Send& send) {
    SendDeviceCommand(
        device_connection, std::make_shared<RebootRequest>(),
        DeviceRequestType::kRebootReply,
        [send, req](IncomingDataPtr) {
          http::response<http::empty_body> res{http::status::ok, req.version()};
          res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
          res.keep_alive(req.keep_alive());
          send(std::move(res));
        });
  }

  template<class Body, class Allocator, class Send>
  void ListInstalledPackages(
      const http::request<Body, http::basic_fields<Allocator>>& req,
      IConnection* device_connection, Send& send) {
    SendDeviceCommand(
        device_connection, std::make_shared<ListInstalledPackagesRequest>(),
        DeviceRequestType::kListInstalledPackagesReply,
        [send, req](IncomingDataPtr reply) {
          auto list_packages_reply = std::static_pointer_cast<ListInstalledPackagesReply>(reply);
          if (list_packages_reply->GetLastError()) {
            send(CreateServerErrorResponse(req, list_packages_reply->GetLastError().message()));
          } else {
            const auto& packages = list_packages_reply->GetPackagesList();
            nlohmann::json json = nlohmann::json::array();
            std::copy(packages.begin(), packages.end(), std::back_inserter(json));
            send(CreateHttpOkResponse(req, json.dump(), "application/json"));
          }
        });
  }

  template<class Body, class Allocator, class Send>
  void InstallPackage(
      const http::request<Body, http::basic_fields<Allocator>>& req,
      IConnection* device_connection, Send& send) {
    SendSimpleDeviceCommand(req, device_connection,
                            std::make_shared<InstallPackageRequest>(req.body()),
                            DeviceRequestType::kInstallPackageReply, send);
  }

  template<class Body, class Allocator, class Send>
  void UninstallPackage(
      const http::request<Body, http::basic_fields<Allocator>>& req,
      IConnection* device_connection, Send& send) {
    SendSimpleDeviceCommand(req, device_connection,
                            std::make_shared<UninstallPackageRequest>(req.body()),
                            DeviceRequestType::kUninstallPackageReply, send);
  }

  template<class Body, class Allocator, class Send>
  void SendSimpleDeviceCommand(
      const http::request<Body, http::basic_fields<Allocator>>& req,
      IConnection* device_connection, OutgoingDataPtr command_request,
      DeviceRequestType expected_reply_type, Send& send) {
    SendDeviceCommand(
        device_connection, command_request,
        expected_reply_type,
        [send, req](IncomingDataPtr reply) {
          auto base_reply = std::static_pointer_cast<ReplyBase>(reply);
          if (base_reply->GetLastError()) {
            send(CreateServerErrorResponse(req, base_reply->GetLastError().message()));
          } else {
            send(CreateHttpOkResponse(req, base_reply->GetRawPayload(), "text/plain"));
          }
        });
  }

  template<class Body, class Allocator, class Send>
  void DownloadLog(
      const http::request<Body, http::basic_fields<Allocator>>& req,
      IConnection* device_connection, OutgoingDataPtr command_request,
      DeviceRequestType expected_reply_type, const std::string& filename,
      Send& send) {
    SendDeviceCommand(
        device_connection, command_request,
        expected_reply_type,
        [send, req, filename](IncomingDataPtr reply) {
          auto base_reply = std::static_pointer_cast<ReplyBase>(reply);
          if (base_reply->GetLastError()) {
            send(CreateServerErrorResponse(req, base_reply->GetLastError().message()));
          } else {
            auto res = CreateHttpOkResponse(req, base_reply->GetRawPayload(), "text/plain");
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_disposition, "attachment; filename=" + filename);
            send(std::move(res));
          }
        });
  }

  void SendDeviceCommand(
      IConnection* device_connection, OutgoingDataPtr command_request,
      DeviceRequestType expected_reply_type,
      DeviceRequestProcessor::HandlerType callback) {
    device_processor_->WaitDeviceReply(expected_reply_type, callback);
    assert(device_connection);
    device_connection->Write(command_request);
  }

  const std::unordered_map<std::string, http::verb> known_commands_;
  DeviceManager* device_manager_;
  DeviceRequestProcessor* device_processor_;
};

}  // namespace server

#endif  // WEB_API_HANDLER_H
