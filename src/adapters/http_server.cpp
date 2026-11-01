#include "adapters/http_server.hpp"
#include "config.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace cnc {
static std::string decode(std::string s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%') {
      if (i + 2 >= s.size() || !isxdigit(s[i + 1]) || !isxdigit(s[i + 2]))
        throw std::runtime_error("bad URL escape");
      out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else
      out += s[i];
  }
  return out;
}
Http::Http(int port, std::function<Response(const Request &)> h) : handler_(std::move(h)) {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, env("LISTEN_ADDRESS", "0.0.0.0").c_str(), &addr.sin_addr) != 1 ||
      bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) || listen(fd_, 16))
    throw std::runtime_error("HTTP bind failed");
  fcntl(fd_, F_SETFL, O_NONBLOCK);
}
Http::~Http() {
  for (auto &c : clients_)
    close(c.fd);
  close(fd_);
}
void Http::pump() {
  int sock = accept(fd_, nullptr, nullptr);
  if (sock >= 0) {
    if (clients_.size() >= 8)
      close(sock);
    else {
      fcntl(sock, F_SETFL, O_NONBLOCK);
      clients_.push_back({sock, "", "", 0, std::chrono::steady_clock::now()});
    }
  }
  for (auto it = clients_.begin(); it != clients_.end();) {
    auto &c = *it;
    bool done = false;
    auto respond = [&](Response r) {
      c.stream = std::move(r.stream);
      c.output =
          "HTTP/1.1 " + std::to_string(r.status) + " Result\r\nContent-Type: " + r.content_type +
          "\r\n" +
          (c.stream ? std::string("Transfer-Encoding: chunked")
                    : "Content-Length: " + std::to_string(r.body.size())) +
          "\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: "
          "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'\r\n\r\n" +
          r.body;
    };
    if (c.output.empty()) {
      char b[4096];
      int n = recv(c.fd, b, sizeof(b), 0);
      if (n > 0)
        c.input.append(b, n);
      else if (n == 0)
        done = true;
      else if (errno != EAGAIN && errno != EWOULDBLOCK)
        done = true;
      if (c.input.size() > 16384)
        respond({413, "{\"error\":\"request too large\"}"});
      else {
        auto end = c.input.find("\r\n\r\n");
        if (end != std::string::npos) {
          try {
            Request req;
            std::istringstream stream(c.input.substr(0, end));
            std::string first, version;
            std::getline(stream, first);
            std::istringstream line(first);
            line >> req.method >> req.path >> version;
            if (version != "HTTP/1.1" && version != "HTTP/1.0")
              throw std::runtime_error("invalid HTTP");
            std::string header;
            while (std::getline(stream, header)) {
              auto sep = header.find(':');
              if (sep == std::string::npos)
                throw std::runtime_error("invalid header");
              auto key = header.substr(0, sep), value = header.substr(sep + 1);
              std::transform(key.begin(), key.end(), key.begin(), ::tolower);
              while (!value.empty() && isspace(value.front()))
                value.erase(value.begin());
              while (!value.empty() && isspace(value.back()))
                value.pop_back();
              if (req.headers.count(key))
                throw std::runtime_error("duplicate header");
              req.headers[key] = value;
            }
            if (req.headers.count("transfer-encoding"))
              throw std::runtime_error("chunked requests unsupported");
            size_t len = 0;
            if (req.headers.count("content-length")) {
              auto val = req.headers["content-length"];
              if (val.empty() || val.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("invalid content length");
              len = std::stoul(val);
              if (len > 8192)
                throw std::runtime_error("body too large");
            }
            if (c.input.size() >= end + 4 + len) {
              req.body = c.input.substr(end + 4, len);
              auto q = req.path.find('?');
              if (q != std::string::npos) {
                std::istringstream query(req.path.substr(q + 1));
                std::string kv;
                while (std::getline(query, kv, '&')) {
                  auto eq = kv.find('=');
                  if (eq == std::string::npos)
                    throw std::runtime_error("invalid query");
                  auto k = decode(kv.substr(0, eq));
                  if (req.query.count(k))
                    throw std::runtime_error("duplicate query parameter");
                  req.query[k] = decode(kv.substr(eq + 1));
                }
                req.path.resize(q);
              }
              respond(handler_(req));
            }
          } catch (const std::exception &e) {
            respond({400, Json({{"error", e.what()}}).dump()});
          }
        }
      }
    }
    // Bound each client to 16 chunks per pump: fast readers finish large pages
    // without buffering the response or starving the engine.
    for (int budget = 0; budget < 16 && !done && !c.output.empty(); ++budget) {
      int n = send(c.fd, c.output.data() + c.sent, c.output.size() - c.sent, MSG_NOSIGNAL);
      if (n > 0)
        c.sent += n;
      else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        done = true;
      if (n <= 0)
        break;
      if (c.sent == c.output.size()) {
        if (c.stream) {
          try {
            auto chunk = c.stream();
            if (chunk.empty()) {
              c.output = "0\r\n\r\n";
              c.stream = {};
            } else {
              std::ostringstream size;
              size << std::hex << chunk.size();
              c.output = size.str() + "\r\n" + chunk + "\r\n";
            }
            c.sent = 0;
          } catch (...) {
            done = true;
          }
        } else
          done = true;
      }
    }
    if (std::chrono::steady_clock::now() - c.since > std::chrono::seconds(3))
      done = true;
    if (done) {
      close(c.fd);
      it = clients_.erase(it);
    } else
      ++it;
  }
}
} // namespace cnc
