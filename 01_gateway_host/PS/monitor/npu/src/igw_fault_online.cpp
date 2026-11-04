#include "engine.hpp"
#include "protocol.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stopping=0;
void signal_stop(int) {stopping=1;}
void heartbeat(const char* state) {
    std::ofstream f("state.tmp");
    if(!f)throw std::runtime_error("heartbeat_open_failed");
    f<<state<<std::endl;f.close();
    if(!f || std::rename("state.tmp","state")!=0)throw std::runtime_error("heartbeat_write_failed");
}
std::string random_id() {
    unsigned char bytes[8];std::ifstream f("/dev/urandom",std::ios::binary);
    if(!f.read(reinterpret_cast<char*>(bytes),sizeof(bytes)))throw std::runtime_error("random_id_failed");
    std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto b:bytes)s<<std::setw(2)<<+b;return s.str();
}
struct Socket {
    int fd;
    Socket():fd(::socket(AF_INET,SOCK_DGRAM,0)) {if(fd<0)throw std::runtime_error("socket_failed");}
    ~Socket() {::close(fd);}
};
int run() {
    const std::string server=random_id();
    igwref::verify_export();
    {igwengine::Engine hidden(false);hidden.self_test(false);}
    igwengine::Engine engine(true);engine.self_test(true);
    std::cout<<"SELF_TEST=PASS HIDDEN_CASES=150 CLASSIFIER_CASES=150"<<std::endl;
    // Wait for wrapper to verify startup hardware IRQ progress before opening UDP.
    heartbeat("self_test_done");
    while(!stopping && ::access("allow",F_OK)!=0) {::poll(nullptr,0,100);}
    if(stopping)return 0;
    Socket sock;sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(16002);
    if(::inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr)!=1 ||
       ::bind(sock.fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0)
        throw std::runtime_error(std::string("bind_127.0.0.1_16002_failed:")+std::strerror(errno));
    in_addr peer{};::inet_pton(AF_INET,"127.0.0.1",&peer);
    std::ofstream log("online.jsonl");if(!log)throw std::runtime_error("online_log_failed");
    igwwire::Cache cache;unsigned count=0,reviews=0,replays=0,invalid=0;
    heartbeat("idle");
    std::cout<<"READY ONLINE_NPU UDP=127.0.0.1:16002 PEER=127.0.0.1 SERVER_ID="<<server<<std::endl;
    while(!stopping) {
        pollfd p{sock.fd,POLLIN,0};int ret=::poll(&p,1,250);
        if(ret<0) {if(errno==EINTR)continue;throw std::runtime_error("poll_failed");}
        if(ret==0)continue;
        char buf[1025];sockaddr_in from{};socklen_t size=sizeof(from);
        auto n=::recvfrom(sock.fd,buf,sizeof(buf),0,reinterpret_cast<sockaddr*>(&from),&size);
        if(n<0) {if(errno==EINTR)continue;throw std::runtime_error("recv_failed");}
        if(from.sin_addr.s_addr!=peer.s_addr)continue;
        std::string body(buf,static_cast<unsigned>(n)),reply;
        igwwire::Request req;
        try {req=igwwire::parse(body);}catch(const std::exception&) {++invalid;continue;}
        std::ostringstream prefix;
        prefix<<"{\"v\":1,\"type\":\"npu_reply\",\"session\":\""<<req.session
              <<"\",\"id\":"<<req.id<<",\"server_id\":\""<<server
              <<"\",\"model_id\":\""<<igwmodel::ModelId<<"\",\"backend\":\"Alnpu\"";
        if(req.hello) {
            reply=prefix.str()+",\"status\":\"ready\",\"self_test\":true,\"hardware_layers\":2,\"inference_count\":"+std::to_string(count)+"}";
        } else {
            try {
                if(req.server!=server)throw std::runtime_error("server_changed");
                std::string who=std::to_string(from.sin_addr.s_addr)+":"+std::to_string(from.sin_port)+":"+req.session;
                reply=cache.lookup(who,req,body,igwwire::seconds());
                if(!reply.empty()) {++replays;}
                else {
                    unsigned clipped=0;auto x=igwref::preprocess(req.features.data(),&clipped);
                    auto cpu=igwref::logits(igwref::hidden(x));
                    heartbeat("busy");long long us=0;auto actual=engine.infer(x,us);heartbeat("idle");
                    igwref::Logits y={{actual[0],actual[1],actual[2]}};
                    auto pred=igwref::argmax(y),cpu_pred=igwref::argmax(cpu);
                    unsigned bad=0,max_diff=0,endpoints=0;
                    for(unsigned i=0;i<3;++i) {
                        unsigned diff=unsigned(std::abs(int(y[i])-int(cpu[i])));
                        max_diff=std::max(max_diff,diff);if(diff>unsigned(igwmodel::LogitTolerance[i]))++bad;
                        if(y[i]==0 || y[i]==255)++endpoints;
                    }
                    auto sorted=y;std::sort(sorted.begin(),sorted.end());unsigned margin=sorted[2]-sorted[1];
                    bool numeric_ok=bad==0 && pred==cpu_pred;
                    ++count;if(!numeric_ok)++reviews;
                    std::ostringstream s;
                    s<<prefix.str()<<",\"status\":\""<<(numeric_ok?"ok":"review")<<"\",\"class\":"<<pred
                     <<",\"label\":\""<<igwmodel::Labels[pred]<<"\",\"cpu_class\":"<<cpu_pred
                     <<",\"logits\":"<<igwwire::array_json(y)<<",\"cpu_logits\":"<<igwwire::array_json(cpu)
                     <<",\"input_u8\":"<<igwwire::array_json(x)<<",\"clipped_dims\":"<<clipped
                     <<",\"max_diff\":"<<max_diff<<",\"outside_budget\":"<<bad
                     <<",\"margin_lsb\":"<<margin<<",\"logit_endpoints\":"<<endpoints<<",\"enqueue_us\":"<<us
                     <<",\"inference_count\":"<<count<<"}";
                    reply=s.str();
                    log<<"{\"uptime_monotonic_s\":"<<std::setprecision(17)<<igwwire::seconds()
                       <<",\"features\":"<<igwwire::array_json(req.features)<<",\"reply\":"<<reply<<"}"<<std::endl;
                    if(!log)throw std::runtime_error("online_log_write_failed");
                    cache.put(who,req,body,reply,igwwire::seconds());
                    std::cout<<"ONLINE id="<<req.id<<" class="<<igwmodel::Labels[pred]<<" enqueue_us="<<us
                             <<" clipped="<<clipped<<" max_diff="<<max_diff<<" numeric="<<(numeric_ok?"PASS":"REVIEW")<<std::endl;
                }
            } catch(const std::exception& e) {
                // Validation/lease errors are recoverable. Hardware/log failures stop the service.
                std::string reason=e.what();
                if(reason!="server_changed" && reason!="client_busy_wait_30s" && reason!="request_id_conflict" &&
                   reason!="stale_request_id" && reason!="rate_limited")throw;
                reply=prefix.str()+",\"status\":\"error\",\"reason\":\""+reason+"\"}";
            }
        }
        if(::sendto(sock.fd,reply.data(),reply.size(),0,reinterpret_cast<sockaddr*>(&from),size)<0)
            std::cout<<"SEND_WARNING="<<std::strerror(errno)<<std::endl;
    }
    std::cout<<"ONLINE_TOTAL="<<count<<" NUMERIC_REVIEW="<<reviews<<" CACHED_REPLIES="<<replays<<" INVALID_DATAGRAMS="<<invalid<<std::endl;
    return reviews?3:0;
}
}
int main(int argc,char**) {
    std::cout.setf(std::ios::unitbuf);
    if(argc!=1) {std::cerr<<"usage: igw_fault_online (fixed IP and port; no CPU fallback)\n";return 2;}
    std::signal(SIGTERM,signal_stop);std::signal(SIGINT,signal_stop);
    std::cout<<"IGW_NPU_LOCAL_R12 HEADER_VERSION="<<ARMNN_VERSION<<" MODEL_ID="<<igwmodel::ModelId
             <<" BACKEND_PREFERENCES=Alnpu"<<std::endl;
    try {return run();}catch(const std::exception& e) {std::cout<<"SERVICE_ERROR="<<e.what()<<std::endl;return 1;}
}
