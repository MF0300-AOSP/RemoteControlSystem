#ifndef ANDROID_INFO_IMPL_HPP
#define ANDROID_INFO_IMPL_HPP

#include <string>

std::string GetAndroidVersion() { return std::string("Ubuntu 18.04"); }
std::string GetBuildNumber() { return std::string("4.15.0-54-generic #58-Ubuntu SMP Mon Jun 24 10:55:24 UTC 2019 x86_64 GNU/Linux"); }
std::string GetSerialNumber() { return std::string("unknown"); }

#endif  // ANDROID_INFO_IMPL_HPP
