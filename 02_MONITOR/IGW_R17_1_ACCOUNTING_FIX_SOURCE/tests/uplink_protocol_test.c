#include <assert.h>
#include <stdio.h>
#include "../src/uplink_protocol.h"
static void handshake(struct ul_client *c,struct ul_server_node *s,uint32_t *epoch,uint64_t now){
    struct ul_frame hello,welcome;unsigned changed;
    assert(ul_client_tick(c,now,123,&hello)&&hello.id==UL_HELLO+c->unit);
    assert(ul_server_receive(s,c->unit,epoch,&hello,&welcome,now,&changed)&&!changed);
    uint32_t session=s->epoch;
    /* Retried HELLO before DATA is idempotent. */
    assert(ul_server_receive(s,c->unit,epoch,&hello,&welcome,now,&changed)&&s->epoch==session);
    ul_client_receive(c,&welcome,now,123);assert(c->phase==UL_WAIT_ACK);
}
int main(void){
    uint32_t epochs=1234;struct ul_client c;struct ul_server_node s={0};
    ul_client_init(&c,1,0);handshake(&c,&s,&epochs,0);
    struct ul_frame data,ack,retry;unsigned changed;
    assert(ul_client_tick(&c,0,123,&data));
    assert(ul_server_receive(&s,1,&epochs,&data,&ack,0,&changed)&&changed&&s.accepted==1&&s.value==123);
    /* Lose the first ACK: the retry must repeat the exact same transaction. */
    assert(!ul_client_tick(&c,249,999,&retry));
    assert(ul_client_tick(&c,250,999,&retry)&&!memcmp(&data,&retry,sizeof data));
    assert(ul_server_receive(&s,1,&epochs,&retry,&ack,250,&changed)&&!changed&&s.accepted==1&&s.duplicates==1);
    struct ul_frame wrong=ack;wrong.data[7]^=1;
    ul_client_receive(&c,&wrong,250,999);assert(c.phase==UL_WAIT_ACK);
    ul_client_receive(&c,&ack,250,999);assert(c.acked==1&&c.phase==UL_IDLE);
    assert(!ul_client_tick(&c,1249,456,&data));
    assert(ul_client_tick(&c,1250,456,&data));
    assert(ul_server_receive(&s,1,&epochs,&data,&ack,1250,&changed)&&changed&&s.accepted==2&&s.value==456);
    ul_client_receive(&c,&ack,1250,456);
    /* Old accepted data cannot roll back the current host value. */
    assert(!ul_server_receive(&s,1,&epochs,&retry,&ack,1300,&changed)&&s.value==456&&s.accepted==2);
    /* Device restart may reuse its initial cookie: established sessions rekey. */
    uint32_t old_epoch=s.epoch;ul_client_init(&c,1,2000);handshake(&c,&s,&epochs,2000);assert(s.epoch!=old_epoch);
    assert(ul_client_tick(&c,2000,789,&data));
    assert(ul_server_receive(&s,1,&epochs,&data,&ack,2000,&changed)&&changed);
    ul_client_receive(&c,&ack,2000,789);
    /* Host restart: stale epoch is rejected, client reconnects without blocking. */
    memset(&s,0,sizeof s);assert(ul_client_tick(&c,3000,900,&data));
    assert(ul_server_receive(&s,1,&epochs,&data,&ack,3000,&changed)&&!changed&&ack.id==UL_WELCOME+1);
    ul_client_receive(&c,&ack,3000,900);assert(c.phase==UL_CONNECT);
    handshake(&c,&s,&epochs,3000);
    assert(ul_client_tick(&c,3000,900,&data));
    assert(ul_server_receive(&s,1,&epochs,&data,&ack,3000,&changed)&&changed&&s.value==900);
    ul_client_receive(&c,&ack,3000,900);
    /* Retry exhaustion is bounded and explicitly uncertain, not false success. */
    assert(ul_client_tick(&c,4000,901,&data));
    assert(ul_client_tick(&c,4250,901,&data));assert(ul_client_tick(&c,4500,901,&data));
    assert(!ul_client_tick(&c,4750,901,&data)&&c.unknown==1&&c.phase==UL_IDLE);
    /* Sequence wrap establishes another epoch before sending sequence 1. */
    c.seq=65535;c.due=5000;assert(ul_client_tick(&c,5000,333,&data)&&data.id==UL_HELLO+1);
    assert(c.phase==UL_CONNECT);
    /* All 16 units keep separate epochs, register shadows and duplicate caches. */
    struct ul_server_node nodes[16]={0};
    for(unsigned i=0;i<16;i++){
        struct ul_client client;ul_client_init(&client,i+1,0);handshake(&client,nodes+i,&epochs,0);
        for(unsigned round=0;round<100;round++){
            uint64_t now=(uint64_t)round*1000;
            assert(ul_client_tick(&client,now,(uint16_t)(i*100+round),&data));
            assert(ul_server_receive(nodes+i,i+1,&epochs,&data,&ack,now,&changed)&&changed);
            ul_client_receive(&client,&ack,now,0);
        }
        assert(nodes[i].accepted==100&&client.acked==100);
    }
    for(unsigned i=0;i<16;i++)for(unsigned j=0;j<i;j++)assert(nodes[i].epoch!=nodes[j].epoch);
    puts("PASS: autonomous handshake, lost ACK/dedup, stale/forged ACK rejection, both-end restarts, retry bound, sequence wrap, 16-node isolation/1600 state writes");return 0;
}
