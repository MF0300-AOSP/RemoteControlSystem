#ifndef DEVICE_LOCATION_HPP
#define DEVICE_LOCATION_HPP

#include <string>
#include <sstream>
#include <utility>

class DeviceLocation {
 public:
  DeviceLocation(double latitude, double longitude, std::string city, std::string country)
      : latitude_(latitude), longitude_(longitude),
        city_(std::move(city)), country_(std::move(country)) {}

  double latitude() const { return latitude_; }
  double longitude() const { return longitude_; }

  std::string city() const { return city_; }
  std::string country() const { return country_; }

  static std::string Serialize(const DeviceLocation& location) {
    std::stringstream ss;
    ss << location.latitude() << "\n"
       << location.longitude() << "\n"
       << location.city() << "\n"
       << location.country();
    return ss.str();
  }

  static DeviceLocation Deserialize(std::string buffer) {
    double latitude, longitude;
    std::string city, country;

    std::stringstream ss(buffer);
    ss >> latitude >> longitude;
    ss.seekg(1, std::ios_base::cur);   // skip '\n'
    std::getline(ss, city);
    std::getline(ss, country);

    return DeviceLocation(latitude, longitude, city, country);
  }

 private:
  double latitude_;
  double longitude_;

  std::string city_;
  std::string country_;
};

#endif  // DEVICE_LOCATION_HPP
