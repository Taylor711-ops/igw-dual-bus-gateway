#pragma once
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace igwwire {
inline double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool hex(const std::string& s,unsigned n) {
    return s.size()==n && s.find_first_not_of("0123456789abcdef")==std::string::npos;
}
struct Request {
    bool hello=false;
    std::string session,server;
    std::uint32_t id=0;
    std::array<double,16> features{};
};
inline Request parse(const std::string& raw) {
    if(raw.size()>1024 || raw.find('\0')!=std::string::npos)throw std::runtime_error("bad_size");
    std::istringstream in(raw);std::string magic,command,token,extra;Request r;
    if(!(in>>magic>>command) || magic!="IGWNPU1")throw std::runtime_error("bad_protocol");
    if(command=="HELLO") {
        r.hello=true;
        if(!(in>>r.session) || !hex(r.session,32) || (in>>extra))throw std::runtime_error("bad_hello");
        return r;
    }
    if(command!="INFER" || !(in>>r.session>>token>>r.server) || !hex(r.session,32) || !hex(r.server,16))
        throw std::runtime_error("bad_request");
    if(token.empty() || token.size()>10 || token.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("bad_id");
    auto id=std::stoull(token);
    if(id==0 || id>0xffffffffULL)throw std::runtime_error("bad_id");
    r.id=static_cast<std::uint32_t>(id);
    for(auto& x:r.features)if(!(in>>x) || !std::isfinite(x) || x<0 || x>5000)throw std::runtime_error("bad_features");
    if(in>>extra)throw std::runtime_error("extra_fields");
    for(unsigned b:{0U,8U}) {
        double sum=0;
        for(unsigned i=0;i<3;++i) {
            double x=r.features[b+i];
            if(x>1 || std::abs(x*5-std::round(x*5))>1e-8)throw std::runtime_error("bad_fractions");
            sum+=x;
        }
        if(std::abs(sum-1)>1e-8)throw std::runtime_error("unrepresented_status");
        if(r.features[b]==0) {
            for(unsigned i=3;i<=6;++i)if(r.features[b+i]!=0)throw std::runtime_error("no_success_rtt_must_be_zero");
        }
        if(r.features[b]==1 && r.features[b+7]!=0)throw std::runtime_error("no_failure_rtt_must_be_zero");
    }
    return r;
}
struct Entry {std::uint32_t id;std::string body,reply;};
class Cache {
    std::string owner;
    double last=-1e9;
    std::uint32_t highest=0;
    std::deque<Entry> entries;
public:
    // Cached replays do not refresh the lease or execute hardware work.
    std::string lookup(const std::string& who,const Request& r,const std::string& body,double now) {
        if(now-last>=30) {owner.clear();entries.clear();highest=0;}
        if(!owner.empty() && who!=owner)throw std::runtime_error("client_busy_wait_30s");
        for(const auto& e:entries)if(e.id==r.id) {
            if(e.body!=body)throw std::runtime_error("request_id_conflict");
            return e.reply;
        }
        if(r.id<=highest)throw std::runtime_error("stale_request_id");
        if(now-last<0.1)throw std::runtime_error("rate_limited");
        return {};
    }
    void put(const std::string& who,const Request& r,const std::string& body,const std::string& reply,double now) {
        owner=who;last=now;highest=r.id;entries.push_back({r.id,body,reply});
        if(entries.size()>128)entries.pop_front();
    }
};
template<class T> std::string array_json(const T& a) {
    std::ostringstream out;out<<std::setprecision(17)<<'[';
    for(unsigned i=0;i<a.size();++i) {if(i)out<<',';out<<+a[i];}
    out<<']';return out.str();
}
}
