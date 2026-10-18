#pragma once
#include "flight_wire.hpp"
#include <functional>
#include <memory>
namespace albert::wire {
// Serves one client at a time on 127.0.0.1 (ephemeral port), one response per request.
class Server {
public:
    using Handler=std::function<void(const Request&,Response&)>;
    explicit Server(Handler handler);
    ~Server();
    unsigned port()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Blocking client with TCP_NODELAY.
class Connection {
public:
    explicit Connection(unsigned port);
    ~Connection();
    Response exchange(const Request& request);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
