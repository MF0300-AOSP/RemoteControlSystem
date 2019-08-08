#ifndef DEVICE_PROTOCOL_H
#define DEVICE_PROTOCOL_H

#include <cstdint>

// device -> server
struct DeviceDataHeader {
  std::uint32_t request_type;
  std::uint32_t payload_size;
};

// server -> device
struct ServerDataHeader {
  std::uint32_t message_type;
  std::uint32_t payload_size;
};

// device -> server
enum class DeviceRequestType : std::uint32_t {
  kSystemInfo,
  kUpdateLocation,
  kInstallPackageReply,
  kUninstallPackageReply,
  kListInstalledPackagesReply,
  kRebootReply,
  kLogcatReply,
  kDmesgReply,
};

// server -> device
enum class DeviceCommand : std::uint32_t {
  kInstallPackage,
  kUninstallPackage,
  kListInstalledPackages,
  kReboot,
  kLogcat,
  kDmesg,
};

#endif  // DEVICE_PROTOCOL_H
