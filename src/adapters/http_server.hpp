#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace cnc {
struct Request {
  std::string method, path, body;
  std::map<std::string, std::string> headers, query;
};
struct Response {
  int status = 200;
  std::string body, content_type = "application/json";
  std::function<std::string()> stream;
};
class Http {
  struct Client {
    int fd;
    std::string input, output;
    size_t sent = 0;
    std::chrono::steady_clock::time_point since;
    std::function<std::string()> stream;
  };
  int fd_;
  std::vector<Client> clients_;
  std::function<Response(const Request &)> handler_;

public:
  Http(int port, std::function<Response(const Request &)> handler);
  ~Http();
  void pump();
};
} // namespace cnc
