#pragma once
#include "core/bridge.hpp"
#include <memory>
namespace albert::rpc {
class Server {
public:
    Server(core::Bridge& bridge,unsigned port);
    ~Server();
    unsigned port() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
