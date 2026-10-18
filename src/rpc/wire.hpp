#pragma once
#include "flight_wire.hpp"
#include <functional>
#include <memory>
namespace albert::wire {
// Serves one client at a time on 127.0.0.1 (ephemeral port). The handler answers a request
// by returning true; push() sends unsolicited responses (real-time samples) from any thread.
class Server {
public:
    using Handler=std::function<bool(const Request&,Response&)>;
    explicit Server(Handler handler);
    ~Server();
    unsigned port()const;
    bool push(const Response& response); // False without a client.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Blocking client with TCP_NODELAY.
class Connection {
public:
    explicit Connection(unsigned port);
    ~Connection();
    Response exchange(const Request& request);
    void send(const Request& request); // Real-time mode: commands without a reply.
    Response receive();                // Real-time mode: the next pushed sample.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
