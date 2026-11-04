#!/usr/bin/env python3
"""One explicit logical-node acceptance sequence. Writes volatile R16/R31.
No write retry. Do not run other PC panels or browser write jobs concurrently.
"""
import argparse
import datetime
import hashlib
import http.client
import json
from pathlib import Path
import sys
import time
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_multi import audit

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',default='192.168.31.222');p.add_argument('--port',type=int,default=8080);p.add_argument('--nodes',type=int,default=2);p.add_argument('--output',type=Path)
 a=p.parse_args();p.error('nodes must be 2..16') if not 2<=a.nodes<=16 else None
 out=a.output or Path('igw_r13_accept_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
 out.mkdir(parents=True,exist_ok=False);events=[];pending=None;token=''
 def request(method,path,body=None):
  connection=http.client.HTTPConnection(a.host,a.port,timeout=5)
  try:
   headers={};data=None
   if body is not None:data=json.dumps(body);headers={'Content-Type':'application/json','X-IGW-Token':token}
   connection.request(method,path,body=data,headers=headers);r=connection.getresponse();result=json.loads(r.read())
   if r.status not in (200,202):raise RuntimeError(f'HTTP {r.status}: {result}')
   return result
  finally:connection.close()
 def job(unit,op,bus,address,value=0):
  nonlocal pending
  q=dict(id=uuid.uuid4().hex,unit=unit,op=op,bus=bus,address=address,value=value);pending=q;events.append({'event':'submit_once','command':q})
  request('POST','/api/job',q)  # Deliberately never retried, including on a lost response.
  end=time.monotonic()+30
  while time.monotonic()<end:
   j=request('GET','/api/job/'+q['id'])['job']
   if j['status'] in ('PASS','REVIEW'):
    events.append({'event':'completed','job':j});pending=None
    if j['status']!='PASS':raise RuntimeError(f'Job REVIEW: {j["id"]} {j["error"]}')
    print(f'PASS node={unit} {bus} {op} R{address} readback={j["readback"]}',flush=True);return
   time.sleep(.1)
  raise RuntimeError('Job outcome unknown; do not repeat the write. Query its ID first.')
 try:
  state=request('GET','/api/state');token=state['token']
  if state['software_revision']!='R13' or state['device_count']<a.nodes:raise RuntimeError('R13 and configured node count required')
  if state['busy']:raise RuntimeError('Another operator job is active')
  for d in state['devices'][:a.nodes]:
   if any(not s['valid'] for s in d['buses'].values()):raise RuntimeError(f'Node {d["unit"]} not ready; wait for complete healthy samples')
  for unit in range(1,a.nodes+1):job(unit,'CROSS','BOTH',31)
  for unit in range(1,a.nodes+1):job(unit,'WRITE','CAN' if unit%2 else 'RS485',16,1111*unit)
  for unit in range(1,a.nodes+1):
   for bus in ('CAN','RS485'):job(unit,'READ',bus,16)
  evidence=request('GET','/api/evidence');report=audit(evidence,a.nodes)
  (out/'evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
  (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
  print(report['result']);return 0
 except (Exception,KeyboardInterrupt) as e:
  (out/'REVIEW.json').write_text(json.dumps(dict(error=str(e),pending_command=pending,warning='No command was automatically resubmitted. Query the pending ID; preserve this record.'),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
  print('REVIEW:',str(e),file=sys.stderr);return 2
 finally:
  (out/'events.json').write_text(json.dumps(events,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
  (out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\n' for f in sorted(out.glob('*.json'))),encoding='ascii')
  print('EVIDENCE_FOLDER='+str(out.resolve()),flush=True)

if __name__=='__main__':raise SystemExit(main())
