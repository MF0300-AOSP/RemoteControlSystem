#ifndef ANDROID_INFO_IMPL_HPP
#define ANDROID_INFO_IMPL_HPP

#include <unistd.h>

#include <string>

#include <cutils/properties.h>
#include <cutils/sockets.h>


std::string ReadProperty(const char* name) {
  char value[PROPERTY_VALUE_MAX];
  property_get(name, value, nullptr);
  return std::string(value);
}


std::string GetAndroidVersion() {
  return ReadProperty("ro.build.version.release");
}


std::string GetBuildNumber() {
  return ReadProperty("ro.build.display.id");
}


std::string GetSerialNumber() {
  std::string serial;
  int sernd_socket = socket_local_client("serialnumber", ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
  if (sernd_socket > 0) {
    std::string cmd = "cmd::get::";
    if (write(sernd_socket, cmd.c_str(), cmd.length()) > 0) {
      // sernd sends \r\n after serial number, so read until \r
      char ch;
      while (read(sernd_socket, &ch, 1) > 0 && ch != '\r') {
        serial.push_back(ch);
      }
    }
    close(sernd_socket);
  }
  return serial;
}

#endif  // ANDROID_INFO_IMPL_HPP
