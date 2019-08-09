#ifndef WEB_API_HANDLER_H
#define WEB_API_HANDLER_H

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <tuple>
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

nlohmann::json FormatDeviceInfo(const IDeviceInfo& device_info) {
  nlohmann::json device_node;
  device_node["sn"] = device_info.GetSerialNumber();
  device_node["deviceName"] = GetDeviceNameFromSerial(device_info.GetSerialNumber());
  device_node["osVersion"] = device_info.GetAndroidVersion();
  device_node["buildNumber"] = device_info.GetBuildNumber();
  device_node["status"] = static_cast<int>(device_info.GetStatus());

  if (auto location = device_info.GetLocation()) {
    device_node["city"] = location->city();
    device_node["country"] = location->country();

    nlohmann::json location_node;
    location_node["lat"] = location->latitude();
    location_node["lng"] = location->longitude();

    device_node["location"] = location_node;
  }
  return device_node;
}

nlohmann::json FormatAppsList(const ListInstalledPackagesReply::AppsListType& apps_list) {
  nlohmann::json apps_list_node;
  for (auto& app : apps_list) {
    nlohmann::json app_node;
    app_node["buildName"] = app;
    apps_list_node.emplace_back(app_node);
  }
  return apps_list_node;
}

using ResponseType = http::response<http::string_body>;

ResponseType CreateResponse(http::status status, beast::string_view content, beast::string_view mimetype) {
  http::response<http::string_body> res{status, 11};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, mimetype);
  res.set(http::field::access_control_allow_origin, "*");
  res.body() = std::string(content);
  res.prepare_payload();
  return res;
}

ResponseType CreateBadRequestResponse(beast::string_view why) {
  return CreateResponse(http::status::bad_request, why, "text/html");
}

ResponseType CreateNotFoundResponse(beast::string_view target) {
  std::string body = "The resource '" + std::string(target) + "' was not found.";
  return CreateResponse(http::status::not_found, body, "text/html");
}

ResponseType CreateServerErrorResponse(beast::string_view what) {
  std::string body = "An error occurred: '" + std::string(what) + "'";
  return CreateResponse(http::status::internal_server_error, body, "text/html");
}

ResponseType CreateHttpOkResponse(beast::string_view body, beast::string_view mimetype) {
  return CreateResponse(http::status::ok, body, mimetype);
}

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

class ApiHandler {
 public:
  ApiHandler(DeviceManager* device_manager, DeviceRequestProcessor* processor)
    : known_entries_({
          ApiEntry(std::regex("/devices/statistic"), http::verb::get, std::bind(&ApiHandler::DevicesStatistic, this, _1, _2, _3)),
          ApiEntry(std::regex("/devices/list"), http::verb::get, std::bind(&ApiHandler::ListDevices, this, _1, _2, _3)),
          ApiEntry(std::regex("/devices/(\\w+)"), http::verb::get, std::bind(&ApiHandler::DeviceInfo, this, _1, _2, _3)),
          ApiEntry(std::regex("/devices/(\\w+)/logs/dmesg"), http::verb::get, std::bind(&ApiHandler::DownloadDmesgLog, this, _1, _2, _3)),
          ApiEntry(std::regex("/devices/(\\w+)/logs/logcat"), http::verb::get, std::bind(&ApiHandler::DownloadLogcatLog, this, _1, _2, _3)),
          ApiEntry(std::regex("/devices/(\\w+)/restart"), http::verb::put, std::bind(&ApiHandler::RestartDevice, this, _1, _2, _3)),
          ApiEntry(std::regex("/devices/(\\w+)/applist"), http::verb::get, std::bind(&ApiHandler::ListInstalledPackages, this, _1, _2, _3)),
          ApiEntry(std::regex("/devices/(\\w+)/appinstall"), http::verb::post, std::bind(&ApiHandler::InstallPackage, this, _1, _2, _3)),
          ApiEntry(std::regex("/devices/(\\w+)/appuninstall"), http::verb::post, std::bind(&ApiHandler::UninstallPackage, this, _1, _2, _3))
      }),
      device_manager_(device_manager),
      device_processor_(processor) {}

  template<class Body, class Allocator, class Send>
  void HandleRequest(
      http::request<Body, http::basic_fields<Allocator>>&& req,
      Send&& send) {
    std::string target = std::string(req.target());
    boost::algorithm::trim_right_if(target, boost::is_any_of("/"));

    auto send_response = [req, send](ResponseType&& res) {
      res.version(req.version());
      res.keep_alive(req.keep_alive());
      send(std::move(res));
    };

    for (auto& ep : known_entries_) {
      std::regex ep_regex;
      http::verb method;
      Handler handler;
      std::tie(ep_regex, method, handler) = ep;
      std::smatch sm;
      if (std::regex_match(target, sm, ep_regex) && req.method() == method) {
        MatchedGroups args;
        if (sm.size() > 1) {
          args.resize(sm.size() - 1);
          std::transform(++sm.begin(), sm.end(), args.begin(), [](std::smatch::const_reference m) { return m.str(); });
        }
        handler(std::move(args), req.body(), send_response);
        return;
      }
    }

    send_response(CreateBadRequestResponse("invalid request: bad endpoint or method"));
  }

 private:
  using CallbackType = std::function<void(ResponseType&&)>;
  using MatchedGroups = std::vector<std::string>;

  void DevicesStatistic(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    boost::ignore_unused(args);
    boost::ignore_unused(content);

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

    callback(CreateHttpOkResponse(json.dump(), "application/json"));
  }

  void ListDevices(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    boost::ignore_unused(args);
    boost::ignore_unused(content);

    std::map<std::uint64_t, std::shared_ptr<IDeviceInfo> > devices;
    device_manager_->ListDevices(devices);

    nlohmann::json json = nlohmann::json::array();

    for (auto iter = devices.begin(); iter != devices.end(); ++iter) {
      const auto& device_info = iter->second;
      if (!device_info)
        continue;
      json.emplace_back(FormatDeviceInfo(*device_info));
    }

    callback(CreateHttpOkResponse(json.dump(), "application/json"));
  }

  void DeviceInfo(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    boost::ignore_unused(content);

    const std::string& device_serial = args[0];
    if (auto device_info = device_manager_->GetDeviceInfo(device_serial)) {
      nlohmann::json device_info_json = FormatDeviceInfo(*device_info);
      if (device_info->GetStatus() == IDeviceInfo::DeviceStatus::kOnline) {
        IConnection* device_connection = device_manager_->GetConnection(device_serial);
        if (device_connection) {
          CommandAppList(device_connection, [device_info_json, callback](ResponseType&& response) {
            if (response.result() != http::status::ok) {
              callback(std::move(response));
              return;
            }

            nlohmann::json full_json = device_info_json;
            full_json["applications"] = nlohmann::json::parse(response.body());
            callback(CreateHttpOkResponse(full_json.dump(), "application/json"));
          });
          return;
        }
      }
      // device offline - no app list is returned
      callback(CreateHttpOkResponse(device_info_json.dump(), "application/json"));
      return;
    }

    callback(CreateNotFoundResponse(device_serial));
  }

  void DownloadDmesgLog(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    HandleDeviceCommand(DeviceCommand::kDmesg, args[0], content, std::move(callback));
  }

  void DownloadLogcatLog(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    HandleDeviceCommand(DeviceCommand::kLogcat, args[0], content, std::move(callback));
  }

  void RestartDevice(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    HandleDeviceCommand(DeviceCommand::kReboot, args[0], content, std::move(callback));
  }

  void ListInstalledPackages(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    HandleDeviceCommand(DeviceCommand::kListInstalledPackages, args[0], content, std::move(callback));
  }

  void InstallPackage(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    HandleDeviceCommand(DeviceCommand::kInstallPackage, args[0], content, std::move(callback));
  }

  void UninstallPackage(MatchedGroups&& args, const std::string& content, CallbackType&& callback) {
    HandleDeviceCommand(DeviceCommand::kUninstallPackage, args[0], content, std::move(callback));
  }

  void HandleDeviceCommand(DeviceCommand command, const std::string& serial,
                           const std::string& content, CallbackType&& callback) {

    IConnection* device_connection = device_manager_->GetConnection(serial);
    if (!device_connection) {
      callback(CreateNotFoundResponse(serial));
      return;
    }

    switch (command) {
      case DeviceCommand::kDmesg:
        CommandDmesg(device_connection, serial, std::move(callback));
        return;
      case DeviceCommand::kLogcat:
        CommandLogcat(device_connection, serial, std::move(callback));
        return;
      case DeviceCommand::kReboot:
        CommandRestart(device_connection, std::move(callback));
        return;
      case DeviceCommand::kListInstalledPackages:
        CommandAppList(device_connection, std::move(callback));
        return;
      case DeviceCommand::kInstallPackage:
        CommandAppInstall(device_connection, content, std::move(callback));
        return;
      case DeviceCommand::kUninstallPackage:
        CommandAppUninstall(device_connection, content, std::move(callback));
        return;
    }

    callback(CreateBadRequestResponse("unknown command"));
  }

  void CommandDmesg(IConnection* device_connection, const std::string& serial, CallbackType&& callback) {
    DownloadLog(device_connection,
                std::make_shared<DmesgRequest>(),
                DeviceRequestType::kDmesgReply,
                serial + "-dmesg.log", std::move(callback));
  }

  void CommandLogcat(IConnection* device_connection, const std::string& serial, CallbackType&& callback) {
    DownloadLog(device_connection,
                std::make_shared<LogcatRequest>(),
                DeviceRequestType::kLogcatReply,
                serial + "-logcat.log", std::move(callback));
  }

  void CommandRestart(IConnection* device_connection, CallbackType&& callback) {
    SendDeviceCommand(
        device_connection, std::make_shared<RebootRequest>(),
        DeviceRequestType::kRebootReply,
        [callback](IncomingDataPtr) {
          callback(CreateHttpOkResponse("Success", "text/plain"));
        });
  }

  void CommandAppList(IConnection* device_connection, CallbackType&& callback) {
    SendDeviceCommand(
        device_connection, std::make_shared<ListInstalledPackagesRequest>(),
        DeviceRequestType::kListInstalledPackagesReply,
        [callback](IncomingDataPtr reply) {
          auto list_packages_reply = std::static_pointer_cast<ListInstalledPackagesReply>(reply);
          if (list_packages_reply->GetLastError()) {
            callback(CreateServerErrorResponse(list_packages_reply->GetLastError().message()));
          } else {
            nlohmann::json json = FormatAppsList(list_packages_reply->GetPackagesList());
            callback(CreateHttpOkResponse(json.dump(), "application/json"));
          }
        });
  }

  void CommandAppInstall(IConnection* device_connection,
                      const std::string& content,
                      CallbackType&& callback) {
    SendSimpleDeviceCommand(
        device_connection,
        std::make_shared<InstallPackageRequest>(content),
        DeviceRequestType::kInstallPackageReply, std::move(callback));
  }

  void CommandAppUninstall(IConnection* device_connection,
                        const std::string& content,
                        CallbackType&& callback) {
    SendSimpleDeviceCommand(
        device_connection,
        std::make_shared<UninstallPackageRequest>(content),
        DeviceRequestType::kUninstallPackageReply, std::move(callback));
  }

  void SendSimpleDeviceCommand(
      IConnection* device_connection, OutgoingDataPtr command_request,
      DeviceRequestType expected_reply_type, CallbackType&& callback) {
    SendDeviceCommand(
        device_connection, command_request,
        expected_reply_type,
        [callback](IncomingDataPtr reply) {
          auto base_reply = std::static_pointer_cast<ReplyBase>(reply);
          if (base_reply->GetLastError()) {
            callback(CreateServerErrorResponse(base_reply->GetLastError().message()));
          } else {
            callback(CreateHttpOkResponse(base_reply->GetRawPayload(), "text/plain"));
          }
        });
  }

  void DownloadLog(
      IConnection* device_connection, OutgoingDataPtr command_request,
      DeviceRequestType expected_reply_type, const std::string& filename,
      CallbackType&& callback) {
    SendDeviceCommand(
        device_connection, command_request,
        expected_reply_type,
        [callback, filename](IncomingDataPtr reply) {
          auto base_reply = std::static_pointer_cast<ReplyBase>(reply);
          if (base_reply->GetLastError()) {
            callback(CreateServerErrorResponse(base_reply->GetLastError().message()));
          } else {
            auto res = CreateHttpOkResponse(base_reply->GetRawPayload(), "text/plain");
            res.set(http::field::content_disposition, "attachment; filename=" + filename);
            callback(std::move(res));
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

  using Handler = std::function<void(MatchedGroups&&, const std::string&, CallbackType&&)>;
  using ApiEntry = std::tuple<std::regex, http::verb, Handler>;

  std::vector<ApiEntry> known_entries_;

  DeviceManager* device_manager_;
  DeviceRequestProcessor* device_processor_;
};

}  // namespace server

#endif  // WEB_API_HANDLER_H
