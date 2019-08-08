#ifndef UPDATE_ANDROID_INFO_REQUEST_HPP
#define UPDATE_ANDROID_INFO_REQUEST_HPP

#include <array>
#include <string>

#include "android_info_impl.hpp"
#include "connection.hpp"
#include "device_protocol.h"

namespace client {

class UpdateAndroidInfoRequest final : public IOutgoingData {
 public:
  UpdateAndroidInfoRequest() {
    std::string os_version = GetAndroidVersion();
    std::string serial_number = GetSerialNumber();
    std::string build_number = GetBuildNumber();

    // 1st - OS version string length
    // 2nd - device serial number length
    // 3rd - OS build info string length
    // 4th - unused, reserved
    std::array<std::uint8_t, 4> field_sizes;
    field_sizes[0] = os_version.length() & 0xFF;
    field_sizes[1] = serial_number.length() & 0xFF;
    field_sizes[2] = build_number.length() & 0xFF;
    field_sizes[3] = 0xff;

    std::ostream out(&payload_);
    out.write(reinterpret_cast<char*>(field_sizes.data()), field_sizes.size());
    out << os_version << serial_number << build_number;
  }

  std::uint32_t GetType() const override { return static_cast<std::uint32_t>(DeviceRequestType::kSystemInfo); }

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

}  // namespace client

#endif  // UPDATE_ANDROID_INFO_REQUEST_HPP
