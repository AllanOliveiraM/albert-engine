#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

namespace albert {
// FIPS 180-4 SHA-256, used to verify assets on both OSes. Incremental, so large
// download containers are checked in blocks without a complete copy in RAM.
class Sha256 {
public:
    Sha256& update(std::string_view input) {
        auto data=reinterpret_cast<const uint8_t*>(input.data());size_t size=input.size();length_+=static_cast<uint64_t>(size)*8;
        if(used_){const size_t take=std::min(size,size_t(64)-used_);std::copy_n(data,take,buffer_.begin()+static_cast<std::ptrdiff_t>(used_));used_+=take;data+=take;size-=take;if(used_==64){block(buffer_.data());used_=0;}}
        for(;size>=64;data+=64,size-=64)block(data);
        std::copy_n(data,size,buffer_.begin());used_+=size;return *this;
    }
    std::string hex() {
        const uint64_t length=length_;uint8_t padding[72]{0x80};const size_t count=(used_<56?56:120)-used_;
        for(int i=0;i<8;++i)padding[count+static_cast<size_t>(i)]=static_cast<uint8_t>(length>>(56-8*i));
        update(std::string_view(reinterpret_cast<const char*>(padding),count+8));
        std::ostringstream out;out<<std::hex<<std::setfill('0');
        for(auto v:h_) out<<std::setw(8)<<v;
        return out.str();
    }
private:
    void block(const uint8_t* bytes) {
        constexpr std::array<uint32_t,64> k = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::array<uint32_t,64> w{};
        for(size_t i=0;i<16;++i) w[i]=(uint32_t(bytes[4*i])<<24)|(uint32_t(bytes[4*i+1])<<16)|(uint32_t(bytes[4*i+2])<<8)|bytes[4*i+3];
        for(size_t i=16;i<64;++i) {
            const auto x=w[i-15], y=w[i-2];
            const auto s0=std::rotr(x,7)^std::rotr(x,18)^(x>>3), s1=std::rotr(y,17)^std::rotr(y,19)^(y>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        auto [a,b,c,d,e,f,g,v]=h_;
        for(size_t i=0;i<64;++i) {
            const auto t1=v+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            const auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h_[0]+=a;h_[1]+=b;h_[2]+=c;h_[3]+=d;h_[4]+=e;h_[5]+=f;h_[6]+=g;h_[7]+=v;
    }
    std::array<uint32_t,8> h_={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<uint8_t,64> buffer_{};size_t used_=0;uint64_t length_=0;
};
inline std::string sha256(std::string_view input) {return Sha256().update(input).hex();}
}
