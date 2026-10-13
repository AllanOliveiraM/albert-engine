#pragma once
#include "core/bridge.hpp"
#include <memory>
namespace albert::catalog {class Manager;}
namespace albert::rpc {
class Server {
public:
    Server(core::Bridge& bridge,unsigned port,catalog::Manager* catalog=nullptr);
    ~Server();
    unsigned port() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
