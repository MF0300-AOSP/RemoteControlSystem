#ifndef UPLOAD_FILE_REPLY_HPP
#define UPLOAD_FILE_REPLY_HPP

#include <fstream>
#include <string>

#include "connection.hpp"

namespace client {

class UploadFileReply : public IOutgoingData {
 public:
  explicit UploadFileReply(const std::string& filename, bool remove_afetr_upload = false)
      : file_stream_(filename, std::ios::binary),
        filename_(filename),
        remove_after_upload_(remove_afetr_upload) {
    file_stream_.seekg(0, std::ios::end);
    file_size_ = static_cast<std::size_t>(file_stream_.tellg());
    file_stream_.seekg(0);
  }

  ~UploadFileReply() override {
    file_stream_.close();
    if (remove_after_upload_)
      remove(filename_.c_str());
  }

  std::size_t GetPayloadSize() const override { return file_size_; }

  void ReadData(boost::asio::mutable_buffer buffer,
                std::function<void(boost::system::error_code, std::size_t)> callback) override {
    file_stream_.read(static_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    namespace errc = boost::system::errc;
    callback(errc::make_error_code(errc::success), static_cast<std::size_t>(file_stream_.gcount()));
  }

 private:
  std::size_t file_size_;
  std::ifstream file_stream_;
  std::string filename_;
  bool remove_after_upload_;
};


class SimpleReply : public IOutgoingData {
 public:
  explicit SimpleReply(std::string payload) {
    std::ostream out(&payload_);
    out << payload;
  }

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

#endif  // UPLOAD_FILE_REPLY_HPP
