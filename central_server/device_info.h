#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <string>

#include "device_location.hpp"

class IDeviceInfo {
 public:
  virtual ~IDeviceInfo() = default;

  virtual std::string GetAndroidVersion() const = 0;
  virtual std::string GetSerialNumber() const = 0;
  virtual std::string GetBuildNumber() const = 0;

  enum class DeviceStatus {
    kOnline = 10,
    kOffline = 20,
  };

  virtual DeviceStatus GetStatus() const = 0;

  virtual DeviceLocation* GetLocation() const = 0;
};

#endif  // DEVICE_INFO_H
