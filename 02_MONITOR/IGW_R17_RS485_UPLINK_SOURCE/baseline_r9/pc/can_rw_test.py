"""RW_R9 node1 CAN write/readback and CAN <-> RS485 shared-register evidence.
No automatic retries. Writes only volatile demonstration registers16..31.
"""
import argparse
import datetime
import json
from pathlib import Path
import secrets
import socket
import time
from rs485_rw_test import Client as RSClient, unique_object, integer


class CanClient:
    def __init__(self, host, port=15000, recorder=None):
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.connect((host,port))
        self.seq=secrets.randbits(32)
        self.record=recorder or (lambda row:None)
        self.previous=None

    def close(self):
        self.sock.close()

    def request(self, operation, address, value=0):
        if operation not in ('READ','WRITE') or not integer(address,31) or not integer(value,65535):
            raise ValueError('invalid CAN command')
        if operation=='READ' and value or operation=='WRITE' and address<16:
            raise ValueError('only volatile registers16..31 may be written; READ value must be zero')
        self.seq=(self.seq+1)&0xffffffff
        seq=self.seq
        command=f'IGW2 CAN {operation} {seq} 1 {address}'
        if operation=='WRITE':command+=f' {value}'
        payload=seq.to_bytes(4,'little')+bytes((1 if operation=='READ' else 2,address))+value.to_bytes(2,'big')
        start=time.perf_counter_ns()
        self.record({'event':'request','bus':'CAN','monotonic_ns':start,'request':command})
        self.sock.send(command.encode('ascii'))
        deadline=start+1_500_000_000
        while True:
            left=(deadline-time.perf_counter_ns())/1e9
            if left<=0:raise TimeoutError('no matching CAN response; write outcome unknown, do not resend')
            self.sock.settimeout(left)
            raw=self.sock.recv(4096)
            received=time.perf_counter_ns()
            self.record({'event':'datagram','bus':'CAN','monotonic_ns':received,'request_seq':seq,'raw':raw.hex()})
            try:r=json.loads(raw.decode('ascii'),object_pairs_hook=unique_object)
            except (ValueError,UnicodeError):continue
            if isinstance(r,dict) and type(r.get('seq')) is int and r['seq']==seq:break
        expected={'v':2,'type':'can_rw_reply','op':operation,'unit':1,'address':address,
                  'count':1,'value':value,'request_can_id':0x211,'reply_can_id':0x191}
        for k,v in expected.items():
            if type(r.get(k)) is not type(v) or r[k]!=v:raise ValueError('CAN reply identity mismatch: '+k)
        if r.get('status')!='ok':raise RuntimeError('CAN gateway status='+str(r.get('status'))+'; stop, no automatic write retry')
        if r.get('execution')!=('read' if operation=='READ' else 'acknowledged'):
            raise ValueError('invalid CAN execution status')
        if bytes.fromhex(r['request_data'])!=payload:raise ValueError('CAN submitted payload mismatch')
        b=bytes.fromhex(r['data'])
        if len(b)!=8 or b[:4]!=payload[:4] or b[4]!=payload[4]|0x80 or b[5]!=address:
            raise ValueError('CAN raw reply mismatch')
        actual=int.from_bytes(b[6:8],'big')
        values=[actual] if operation=='READ' else []
        reported=r.get('registers')
        if not isinstance(reported,list) or not all(integer(v,65535) for v in reported) or reported!=values or operation=='WRITE' and actual!=value:
            raise ValueError('CAN response value/echo mismatch')
        c=r.get('stats')
        names=['attempts','ok','timeout','tx_fail','mismatch','ignored','busy','resets',
               'reset_fail','invalid','udp_fail','duplicate','rtt_min_us','rtt_avg_us','rtt_max_us']
        if not isinstance(c,dict) or any(not integer(c.get(k)) for k in names) or not integer(r.get('can_rtt_us')):
            raise ValueError('invalid CAN counters/timing')
        if self.previous:
            for k in ['attempts','ok']:
                if c[k]!=self.previous[k]+1:raise ValueError('extra CAN sender, counter reset, or missing completion')
            for k in ['timeout','tx_fail','mismatch','ignored','busy','resets','reset_fail','invalid','udp_fail','duplicate']:
                if c[k]!=self.previous[k]:raise ValueError('new CAN gateway error/event: '+k)
        self.previous=c.copy()
        self.record({'event':'validated','bus':'CAN','seq':seq,'operation':operation,
                     'address':address,'argument':value,'values':values,
                     'pc_rtt_us':(received-start)//1000,'gateway_rtt_us':r['can_rtt_us']})
        return values

    def read(self, address):return self.request('READ',address)[0]
    def write(self, address, value):self.request('WRITE',address,value)
    def meta(self):return [self.read(a) for a in range(4,8)]


def require_meta(before,after,address,value,increment=1):
    if before[0]!=0x0801 or after!=[0x0801,(before[1]+increment)&65535,address,value]:
        raise ValueError('shared slave write count/address/value mismatch')


def can_round(can,address):
    if can.read(8)!=0x0901:raise ValueError('RW_R9 slave absent; update SLAVE application first')
    before=can.meta()
    old=can.read(address);new=old^0x5a5a
    can.write(address,new)
    actual=can.read(address);after=can.meta()
    if actual!=new:raise ValueError('CAN independent readback differs from written value')
    require_meta(before,after,address,new)
    result={'event':'can_write_verified','address':address,'before':old,'written':new,
            'readback':actual,'slave_write_count':after[1]}
    can.record(result)
    return result


def cross_round(can,rs,address):
    if can.read(8)!=0x0901:raise ValueError('RW_R9 slave absent')
    before=rs.request('READ',4,4)
    if before[0]!=0x0801:raise ValueError('RS485 register revision mismatch')
    old=can.read(address)
    if rs.request('READ',address,1)!=[old]:raise ValueError('initial CAN/RS485 register views differ')
    first=old^0x5a5a
    can.write(address,first)
    via_rs=rs.request('READ',address,1)[0];middle=rs.request('READ',4,4)
    if via_rs!=first:raise ValueError('CAN write -> RS485 readback failed')
    require_meta(before,middle,address,first)
    second=first^0xa5a5
    rs.request('WRITE',address,second)
    via_can=can.read(address);after=rs.request('READ',4,4)
    if via_can!=second:raise ValueError('RS485 write -> CAN readback failed')
    require_meta(middle,after,address,second)
    result={'event':'cross_verified','address':address,'before':old,
            'can_written':first,'rs485_readback':via_rs,'rs485_written':second,
            'can_readback':via_can,'write_count_before':before[1],'write_count_after':after[1]}
    can.record(result)
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host',default='192.168.31.222')
    ap.add_argument('--mode',choices=['can','cross'],default='can')
    ap.add_argument('--address',type=int,choices=range(16,32),default=16)
    ap.add_argument('--count',type=int,choices=range(1,101),default=1)
    ap.add_argument('--can-port',type=int,default=15000)
    ap.add_argument('--rs-port',type=int,default=15001)
    args=ap.parse_args()
    if not 1<=args.can_port<=65535 or not 1<=args.rs_port<=65535:ap.error('invalid UDP port')
    path=Path(__file__).resolve().parent/('can_rw_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S')+'_'+secrets.token_hex(3)+'.jsonl')
    passed=0
    with path.open('x',encoding='utf8') as log:
        def record(row):log.write(json.dumps(row,ensure_ascii=False)+'\n');log.flush()
        def record_rs(row):record(dict(row,bus='RS485'))
        info=time.get_clock_info('perf_counter')
        record({'event':'clock','tool_revision':'RW_R9_PC1','clock':'perf_counter_ns',
                'implementation':info.implementation,'resolution_seconds':info.resolution,
                'mode':args.mode,'monotonic':info.monotonic,
                'rtt_scope':'PC application RTT including logging/scheduling; not pure forwarding latency'})
        can=CanClient(args.host,args.can_port,record)
        rs=None
        try:
            if args.mode=='cross':rs=RSClient(args.host,args.rs_port,record_rs)
            for i in range(args.count):
                row=can_round(can,args.address) if rs is None else cross_round(can,rs,args.address)
                passed+=1
                print(f'PASS {passed}/{args.count} mode={args.mode} '+json.dumps(row),flush=True)
                if i+1<args.count:time.sleep(1)
            result='PASS_CAN_WRITE_READBACK' if rs is None else 'PASS_CROSS_PROTOCOL_READBACK'
            record({'event':'summary','result':result,'verified':passed})
            print('RESULT='+result)
            print('Scope: node1 volatile shared-register request/response; no autonomous slave push, multi-device, encrypted storage or web claim.')
            return 0
        except (OSError,ValueError,RuntimeError,KeyError,TypeError,KeyboardInterrupt) as error:
            record({'event':'summary','result':'REVIEW','verified':passed,'reason':str(error)})
            print('RESULT=REVIEW reason='+str(error));return 1
        finally:
            can.close()
            if rs:rs.close()
            print('EVIDENCE='+str(path))


if __name__=='__main__':raise SystemExit(main())
