#ifndef DEVICE_MANAGER_HPP
#define DEVICE_MANAGER_HPP

#include <cassert>
#include <fstream>              // temp, for fake devices
#include <map>
#include <memory>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>    // temp, for fake devices

#include "device_connection.hpp"
#include "device_info.h"

namespace server {

class DeviceInfo final : public IDeviceInfo {
 public:
  std::string GetAndroidVersion() const override { return os_version_; }
  std::string GetBuildNumber() const override { return build_number_; }
  std::string GetSerialNumber() const override { return serial_number_; }
  DeviceStatus GetStatus() const override { return status_; }
  DeviceLocation* GetLocation() const override { return location_.get(); }

  void SetAndroidVersion(std::string version) { os_version_ = std::move(version); }
  void SetBuildNumber(std::string build_number) { build_number_ = std::move(build_number); }
  void SetSerialNumber(std::string serial_number) { serial_number_ = std::move(serial_number); }
  void SetStatus(DeviceStatus status) { status_ = status; }
  void SetLocation(DeviceLocation location) { location_.reset(new DeviceLocation(std::move(location))); }

 private:
  std::string os_version_;
  std::string build_number_;
  std::string serial_number_;
  DeviceStatus status_ = DeviceStatus::kOffline;
  std::unique_ptr<DeviceLocation> location_;
};


class SystemInfo final {
 public:
  SystemInfo(std::string os_version, std::string build_number, std::string serial_number)
      : os_version_(std::move(os_version)),
        build_number_(std::move(build_number)),
        serial_number_(std::move(serial_number)) {}

  std::string GetOsVersion() const { return os_version_; }
  std::string GetBuildNumber() const { return build_number_; }
  std::string GetSerialNumber() const { return serial_number_; }

 private:
  std::string os_version_;
  std::string build_number_;
  std::string serial_number_;
};


class DeviceManager : public IConnectionTracker {
 public:
  void ConnectionCreated(IConnection* connection) {
    devices_[connection] = std::make_shared<DeviceInfo>();
    connections_[GetDeviceId(connection)] = connection;
    assert(devices_.size() == connections_.size());
  }

  void ConnectionDestroyed(IConnection* connection) {
    devices_.erase(connection);
    connections_.erase(GetDeviceId(connection));
    assert(devices_.size() == connections_.size());
  }

  void ListDevices(std::map<std::uint64_t, std::shared_ptr<IDeviceInfo> >& devices) const {
    for (auto citer = connections_.begin(); citer != connections_.end(); ++citer) {
      auto liter = devices_.find(citer->second);
      assert(liter != devices_.end());
      devices.insert({citer->first, liter->second});
    }

    // temp, for fake devices
    std::ifstream in("fake_devices.json");
    if (in) {
      nlohmann::json json;
      in >> json;

      for (auto i : json) {
        auto dev_info = std::make_shared<DeviceInfo>();
        dev_info->SetStatus(static_cast<IDeviceInfo::DeviceStatus>(i["status"].get<int>()));
        DeviceLocation dfake_location(i["location"]["lat"].get<double>(), i["location"]["lng"].get<double>(), i["city"].get<std::string>(), i["country"].get<std::string>());
        dev_info->SetLocation(dfake_location);
        dev_info->SetBuildNumber(i["buildNumber"].get<std::string>());
        dev_info->SetSerialNumber(i["sn"].get<std::string>());
        dev_info->SetAndroidVersion(i["osVersion"].get<std::string>());
        devices.insert({reinterpret_cast<uint64_t>(dev_info.get()), dev_info});
      }
    }
  }

  IConnection* GetConnection(const std::string& serial) const {
    for (auto iter = devices_.begin(); iter != devices_.end(); ++iter) {
      if (iter->second->GetSerialNumber() == serial) {
        return iter->first;
      }
    }
    return nullptr;
  }

  IConnection* GetConnection(std::uint64_t device_id) const {
    auto iter = connections_.find(device_id);
    return iter != connections_.end() ? iter->second : nullptr;
  }

  void UpdateDeviceLocation(IConnection* connection, const DeviceLocation& location) {
    std::shared_ptr<DeviceInfo>& dev_info = devices_[connection];
    assert(dev_info);   // connection must be known
    dev_info->SetLocation(location);
  }

  void UpdateSystemInfo(IConnection* connection, const SystemInfo& sys_info) {
    std::shared_ptr<DeviceInfo>& dev_info = devices_[connection];
    assert(dev_info);   // connection must be known
    dev_info->SetAndroidVersion(sys_info.GetOsVersion());
    dev_info->SetBuildNumber(sys_info.GetBuildNumber());
    dev_info->SetSerialNumber(sys_info.GetSerialNumber());
    dev_info->SetStatus(IDeviceInfo::DeviceStatus::kOnline);
  }

  std::uint64_t GetDeviceId(IConnection* connection) const {
    return reinterpret_cast<std::uint64_t>(connection);
  }

 private:
  std::map<IConnection*, std::shared_ptr<DeviceInfo> > devices_;
  std::map<std::uint64_t, IConnection*> connections_;
};

}  // namespace server

#endif  // DEVICE_MANAGER_HPP
