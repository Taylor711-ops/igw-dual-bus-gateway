#include <stdio.h>
#include <string.h>
#include "file_engine.h"
#include "rsf_wire.h"
#define IGW_NODE_COUNT 1
#define IGW_FIRST_NODE 3
#include "sd_store.inc"
#include "rsf_sd_protocol.inc"

static struct rsf_store File;
static uint64_t sd_now_ms(void) { return rs3_clock_ms(); }

int rs3_file_init(void) {
    int r=sd_storage_init();
    memset(&File,0,sizeof File);
    File.unit=3;
    File.bank=&SdBanks[0];
    rsf_restore(&File);
    return r;
}
void rs3_file_tick(void) { rsf_expire(&File,rs3_clock_ms()); }
const char *rs3_storage_reason(void) { return SdReason; }
unsigned rs3_storage_error(void) { return (unsigned)SdLast; }

/* Only file requests addressed to 3 are handled.  FC03/06/45 remain in
 * the established PL design; reply 0x43 is never interpreted as a request. */
size_t rs3_file_request(const uint8_t *frame,size_t len,uint8_t out[256]) {
    struct rsf_packet q;
    if (!rsf_parse(frame,len,0,&q) || q.unit!=3) return 0;
    if (q.op==RSF_SPEED) {
        /* The three physically separate slaves share a single bus.  They
         * cannot all be switched by one addressed SPEED command. */
        unsigned err=(q.status||q.length||q.count||q.offset!=9600)?RSF_BAD:RSF_OK;
        rsf_expire(&File,rs3_clock_ms());
        if (rsf_active(&File)) err=RSF_BUSY;
        return rsf_frame(out,3,1,q.op,err,q.token,q.offset,0,NULL,0);
    }
    return rsf_handle(&File,frame,len,out,rs3_clock_ms());
}
