#include <iostream>
#include <sstream>
#include <string>
#include <regex>
#include <vector>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/ssl.hpp>

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

#include <nlohmann/json.hpp>

#include "tools_keycdn_com.crt.h"

using boost::asio::ip::tcp;

class HttpsClient {
public:
  virtual ~HttpsClient() = default;

  HttpsClient(boost::asio::io_context& io_context,
              boost::asio::ssl::context& ssl_context,
              const std::string& server, const std::string& path)
      : resolver_(io_context),
        socket_(io_context, ssl_context) {
    // Form the request. We specify the "Connection: close" header so that the
    // server will close the socket after transmitting the response. This will
    // allow us to treat all data up until the EOF as the content.
    std::ostream request_stream(&request_);
    request_stream << "GET " << path << " HTTP/1.0\r\n";
    request_stream << "Host: " << server << "\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: close\r\n\r\n";

    tcp::resolver::query query(server, "443");
    resolver_.async_resolve(query,
                            boost::bind(&HttpsClient::handle_resolve, this,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::iterator));
  }

protected:
  virtual void process_content(const std::string& content) = 0;

private:
  void handle_resolve(const boost::system::error_code& err,
                      tcp::resolver::iterator endpoint_iterator) {
    if (!err) {
      socket_.set_verify_mode(boost::asio::ssl::verify_peer);

      boost::asio::async_connect(socket_.lowest_layer(), endpoint_iterator,
                                 boost::bind(&HttpsClient::handle_connect, this,
                                             boost::asio::placeholders::error));
    }
  }

  void handle_connect(const boost::system::error_code& err) {
    if (!err) {
      socket_.async_handshake(boost::asio::ssl::stream_base::client,
                              boost::bind(&HttpsClient::handle_handshake, this,
                                          boost::asio::placeholders::error));
    }
  }

  void handle_handshake(const boost::system::error_code& err) {
    if (!err) {
      // The handshake was successful. Send the request.
      boost::asio::async_write(socket_, request_,
                               boost::bind(&HttpsClient::handle_write_request, this,
                                           boost::asio::placeholders::error));
    }
  }

  void handle_write_request(const boost::system::error_code& err) {
    if (!err) {
      // Read the response status line. The response_ streambuf will
      // automatically grow to accommodate the entire line. The growth may be
      // limited by passing a maximum size to the streambuf constructor.
      boost::asio::async_read_until(socket_, response_, "\r\n",
                                    boost::bind(&HttpsClient::handle_read_status_line, this,
                                        boost::asio::placeholders::error));
    }
  }

  void handle_read_status_line(const boost::system::error_code& err) {
    if (!err) {
      // Check that response is OK.
      std::istream response_stream(&response_);
      std::string http_version;
      response_stream >> http_version;
      unsigned int status_code;
      response_stream >> status_code;
      std::string status_message;
      std::getline(response_stream, status_message);
      if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
        return;
      }
      if (status_code != 200) {
        return;
      }

      // Read the response headers, which are terminated by a blank line.
      boost::asio::async_read_until(socket_, response_, "\r\n\r\n",
                                    boost::bind(&HttpsClient::handle_read_headers, this,
                                        boost::asio::placeholders::error));
    }
  }

  void handle_read_headers(const boost::system::error_code& err) {
    if (!err) {
      // Process the response headers.
      std::istream response_stream(&response_);
      std::string header;
      while (std::getline(response_stream, header) && header != "\r");

      // Start reading remaining data until EOF.
      boost::asio::async_read(socket_, response_,
                              boost::asio::transfer_at_least(1),
                              boost::bind(&HttpsClient::handle_read_content, this,
                                          boost::asio::placeholders::error));
    }
  }

  void handle_read_content(const boost::system::error_code& err) {
    if (!err) {
      // Continue reading remaining data until EOF.
      boost::asio::async_read(socket_, response_,
                              boost::asio::transfer_at_least(1),
                              boost::bind(&HttpsClient::handle_read_content, this,
                                          boost::asio::placeholders::error));
    } else if (err == boost::asio::error::eof) {
      std::stringstream ss;
      ss << &response_;
      process_content(ss.str());
    }
  }

  tcp::resolver resolver_;
  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;
  boost::asio::streambuf request_;
  boost::asio::streambuf response_;
};


class LocationParser : public HttpsClient {
public:
  LocationParser(boost::asio::io_context& io_context, boost::asio::ssl::context& ssl_context)
      : HttpsClient(io_context, ssl_context, "tools.keycdn.com", "/geo") {}

protected:
  void process_content(const std::string& content) override {
    htmlDocPtr doc = htmlReadDoc(reinterpret_cast<const xmlChar*>(content.c_str()),
                                 nullptr, nullptr,
                                 HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc)
      return;

    xmlXPathContextPtr xpath_context = xmlXPathNewContext(doc);
    if (xpath_context) {
      const xmlChar* xpath = reinterpret_cast<const xmlChar*>("//dl[1]/dd/text()");
      xmlXPathObjectPtr xpath_result = xmlXPathEvalExpression(xpath, xpath_context);
      xmlXPathFreeContext(xpath_context);

      if (xpath_result && !xmlXPathNodeSetIsEmpty(xpath_result->nodesetval)) {
        xmlNodeSetPtr nodeset = xpath_result->nodesetval;
        std::vector<std::string> parts;
        parts.reserve(static_cast<std::size_t>(nodeset->nodeNr));
        for (int i = 0; i < nodeset->nodeNr; i++) {
          parts.emplace_back(std::string(reinterpret_cast<const char*>(nodeset->nodeTab[i]->content)));
        }
        assert(parts.size() >= 6);

        std::regex e("(.*) \\(lat\\) \\/ (.*) \\(long\\)");

        std::smatch sm;
        std::regex_match(parts[5], sm, e);
        assert(sm.size() >= 3);

        nlohmann::json root_node;
        root_node["city"] = parts[0];
        root_node["region"] = parts[1];
        root_node["country"] = parts[3];
        root_node["continent"] = parts[4];
        root_node["latitude"] = std::stod(sm[1]);
        root_node["longitude"] = std::stod(sm[2]);

        std::cout << root_node << std::endl;
      }

      xmlXPathFreeObject(xpath_result);
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();
  }
};


int main() {
  boost::asio::ssl::context ctx(boost::asio::ssl::context::sslv23);
  ctx.add_certificate_authority(boost::asio::const_buffer(tools_keycdn_com_cert, sizeof(tools_keycdn_com_cert)));

  boost::asio::io_context io_context;
  LocationParser c(io_context, ctx);
  io_context.run();
  return 0;
}
