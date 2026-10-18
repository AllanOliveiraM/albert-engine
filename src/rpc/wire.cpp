#include "wire.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket=SOCKET;constexpr Socket invalidSocket=INVALID_SOCKET;
void closeSocket(Socket s){closesocket(s);}
void startSockets(){static const bool started=[]{WSADATA d;if(WSAStartup(MAKEWORD(2,2),&d))throw std::runtime_error("Winsock unavailable");return true;}();(void)started;}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket=int;constexpr Socket invalidSocket=-1;
void closeSocket(Socket s){::close(s);}
void startSockets(){}
#endif
namespace albert::wire {
namespace {
bool exact(Socket s,char* data,std::size_t n,bool send){
    for(std::size_t done=0;done<n;){
        const auto r=send ? ::send(s,data+done,static_cast<int>(n-done),0) : ::recv(s,data+done,static_cast<int>(n-done),0);
        if(r<=0)return false;done+=static_cast<std::size_t>(r);
    }
    return true;
}
void noDelay(Socket s){int one=1;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&one),sizeof one);}
sockaddr_in loopback(unsigned port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(static_cast<unsigned short>(port));a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);return a;}
}
struct Server::Impl {
    Handler handler;Socket listener=invalidSocket;std::atomic<Socket> client{invalidSocket};std::atomic_bool stopping{false};unsigned port=0;std::thread thread;std::mutex sending;
    void serve(){
        while(!stopping){
            const Socket c=::accept(listener,nullptr,nullptr);if(c==invalidSocket){if(stopping)return;continue;}
            noDelay(c);client=c;Request q;Response r;
            while(!stopping&&exact(c,reinterpret_cast<char*>(&q),sizeof q,false)){
                if(std::memcmp(q.magic,"ALBQ",4)!=0)break; // Unknown framing: drop the client.
                if(handler(q,r)){std::lock_guard lock(sending);if(!exact(c,reinterpret_cast<char*>(&r),sizeof r,true))break;}
            }
            {std::lock_guard lock(sending);client=invalidSocket;}closeSocket(c);
        }
    }
};
Server::Server(Handler handler):impl_(std::make_unique<Impl>()){
    startSockets();impl_->handler=std::move(handler);
    impl_->listener=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(impl_->listener==invalidSocket)throw std::runtime_error("Cannot create the flight data socket");
    auto a=loopback(0);
    if(::bind(impl_->listener,reinterpret_cast<sockaddr*>(&a),sizeof a)!=0||::listen(impl_->listener,1)!=0){closeSocket(impl_->listener);throw std::runtime_error("Cannot bind the flight data socket");}
    socklen_t length=sizeof a;getsockname(impl_->listener,reinterpret_cast<sockaddr*>(&a),&length);impl_->port=ntohs(a.sin_port);
    impl_->thread=std::thread([this]{impl_->serve();});
}
Server::~Server(){
    impl_->stopping=true;
#ifdef _WIN32
    closeSocket(impl_->listener);if(const Socket c=impl_->client;c!=invalidSocket)shutdown(c,SD_BOTH);
#else
    shutdown(impl_->listener,SHUT_RDWR);closeSocket(impl_->listener);if(const Socket c=impl_->client;c!=invalidSocket)shutdown(c,SHUT_RDWR);
#endif
    impl_->thread.join();
}
unsigned Server::port()const{return impl_->port;}
bool Server::push(const Response& r){
    std::lock_guard lock(impl_->sending);const Socket c=impl_->client;
    return c!=invalidSocket&&exact(c,const_cast<char*>(reinterpret_cast<const char*>(&r)),sizeof r,true);
}
struct Connection::Impl {Socket s=invalidSocket;};
Connection::Connection(unsigned port):impl_(std::make_unique<Impl>()){
    startSockets();impl_->s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);auto a=loopback(port);
    if(impl_->s==invalidSocket||::connect(impl_->s,reinterpret_cast<sockaddr*>(&a),sizeof a)!=0){if(impl_->s!=invalidSocket)closeSocket(impl_->s);throw std::runtime_error("Cannot connect to the flight data plane");}
    noDelay(impl_->s);
}
Connection::~Connection(){closeSocket(impl_->s);}
void Connection::send(const Request& q){if(!exact(impl_->s,const_cast<char*>(reinterpret_cast<const char*>(&q)),sizeof q,true))throw std::runtime_error("Flight data plane closed");}
Response Connection::receive(){
    Response r;if(!exact(impl_->s,reinterpret_cast<char*>(&r),sizeof r,false))throw std::runtime_error("Flight data plane closed");
    if(std::memcmp(r.magic,"ALBR",4)!=0)throw std::runtime_error("Unexpected flight data frame");
    return r;
}
Response Connection::exchange(const Request& q){
    Response r;
    if(!exact(impl_->s,const_cast<char*>(reinterpret_cast<const char*>(&q)),sizeof q,true)||!exact(impl_->s,reinterpret_cast<char*>(&r),sizeof r,false))throw std::runtime_error("Flight data plane closed");
    if(std::memcmp(r.magic,"ALBR",4)!=0)throw std::runtime_error("Unexpected flight data frame");
    return r;
}
}
