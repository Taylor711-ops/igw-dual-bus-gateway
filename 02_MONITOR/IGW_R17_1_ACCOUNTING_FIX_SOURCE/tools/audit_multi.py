#!/usr/bin/env python3
"""Audit exported R16 logical-node evidence; not physical-device certification."""
import argparse
import hashlib
import json
from pathlib import Path

def crc16(a):
 c=65535
 for x in a:
  c^=x
  for _ in range(8):c=(c>>1)^(0xa001 if c&1 else 0)
 return c

def audit(d,count=2):
 assert d['software_revision']=='R17.1_RS485_UPLINK' and 'token' not in d
 assert 2<=count<=d['device_count']<=16 and d['npu_scope_unit']==1
 devices={v['unit']:v for v in d['devices']}
 assert len(devices)==d['device_count'] and set(devices)==set(range(1,d['device_count']+1))
 for unit in range(1,count+1):
  for b in ('CAN','RS485'):
   s=devices[unit]['buses'][b]
   assert s['valid'] and 0<=s['age_ms']<30000 and s['values'][0]==0x1234 and s['values'][1]==unit
 crosses=set();reads=set();frames=0;writes={}
 for j in d['jobs']:
  if j['status']!='PASS':continue
  unit=j['unit'];assert unit in devices
  trace=j['trace'];assert trace
  for t in trace:
   q=t['reply'];assert q['status']=='ok' and q['unit']==unit and q['seq']==t['seq']
   a=bytes.fromhex(q['data']);frames+=1
   if t['bus']=='CAN':
    p=bytes.fromhex(q['request_data'])
    assert len(a)==len(p)==8 and q['request_can_id']==0x210+unit and q['reply_can_id']==0x190+unit
    assert int.from_bytes(p[:4],'little')==t['seq'] and a[:4]==p[:4] and a[4]==(p[4]|128) and a[5]==p[5]==q['address']
    assert p[4]==(2 if q['op']=='WRITE' else 1)
    if q['op']=='WRITE':assert a[6:]==p[6:] and int.from_bytes(a[6:],'big')==q['value']
    else:assert p[6:]==b'\0\0' and q['registers']==[int.from_bytes(a[6:],'big')]
   else:
    assert t['bus']=='RS485' and len(a)>=5 and a[0]==unit and crc16(a)==0
    if q['op']=='READ':
     n=q['count'];assert a[1]==3 and len(a)==5+2*n and a[2]==2*n
     assert q['registers']==[int.from_bytes(a[3+2*i:5+2*i],'big') for i in range(n)]
    else:assert q['op']=='WRITE' and len(a)==8 and a[1]==6 and int.from_bytes(a[2:4],'big')==q['address'] and int.from_bytes(a[4:6],'big')==q['value']
  if j['op']=='READ':
   assert len(trace)==1 and trace[0]['bus']==j['bus']
   q=trace[0]['reply'];assert q['op']=='READ' and q['address']==j['address'] and q['registers']==[j['readback']]
  elif j['op']=='WRITE':
   issued=[(i,t['reply']) for i,t in enumerate(trace) if t['reply']['op']=='WRITE']
   if j['execution']=='unchanged':
    assert not issued and j['before']==j['readback']==j['value'] and j['write_count_before']==j['write_count_after']
   else:
    assert j['execution']=='verified' and len(issued)==1 and j['readback']==j['value']
    i,q=issued[0];assert trace[i]['bus']==j['bus'] and q['address']==j['address'] and q['value']==j['value']
    assert any(t['bus']==j['bus'] and t['reply']['op']=='READ' and t['reply']['address']==j['address'] and t['reply']['registers']==[j['readback']] for t in trace[i+1:])
    assert j['write_count_after']==(j['write_count_before']+1)%65536
  elif j['op']=='CROSS':
   expected=[('CAN','READ',8),('RS485','READ',4),('RS485','READ',j['address']),('CAN','READ',j['address']),('CAN','WRITE',j['address']),('RS485','READ',j['address']),('RS485','READ',4),('RS485','WRITE',j['address']),('CAN','READ',j['address']),('RS485','READ',4)]
   assert [(t['bus'],t['reply']['op'],t['reply']['address']) for t in trace]==expected
   assert trace[4]['reply']['value']==trace[5]['reply']['registers'][0]==j['can_written']
   assert trace[7]['reply']['value']==trace[8]['reply']['registers'][0]==j['rs485_written']==j['readback']
   for index,increment in [(1,0),(6,1),(9,2)]:
    meta=trace[index]['reply']['registers'];assert meta[0]==0x0801 and meta[1]==(j['write_count_before']+increment)%65536
    if increment:assert meta[2:]==[j['address'],j['can_written'] if increment==1 else j['rs485_written']]
   assert j['write_count_after']==(j['write_count_before']+2)%65536
   crosses.add(unit)
  else:raise AssertionError('Unknown job operation')
  if j['op']=='WRITE' and j['address']==16 and j['value']==1111*unit:
   assert j['readback']==1111*unit
   writes[unit]=max(writes.get(unit,0),j['end_ms'])
 # Every node must be read on both buses AFTER all marker writes; this proves
 # distinct banks in the observed sequence rather than assuming IDs imply it.
 assert all(u in writes for u in range(1,count+1))
 after=max(writes[u] for u in range(1,count+1))
 for j in d['jobs']:
  if j['status']=='PASS' and j['op']=='READ' and j['address']==16 and j['begin_ms']>=after:
   assert j['readback']==1111*j['unit'];reads.add((j['unit'],j['bus']))
 assert all(u in crosses and all((u,b) in reads for b in ('CAN','RS485')) for u in range(1,count+1))
 return dict(result='PASS_LOGICAL_MULTI_NODE_EVIDENCE',nodes=count,cross_nodes=sorted(crosses),raw_frames_checked=frames,independent_register_markers='PASS',scope='Logical node routing, bidirectional cross-readback and bank isolation. Physical node count, NPU accuracy, PL CAN parsing, latency and FPGA timing are not certified.')

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('evidence',type=Path);p.add_argument('--nodes',type=int,default=2);a=p.parse_args()
 data=a.evidence.read_bytes();result=audit(json.loads(data),a.nodes);result['sha256']=hashlib.sha256(data).hexdigest();print(json.dumps(result,ensure_ascii=False,indent=2))
