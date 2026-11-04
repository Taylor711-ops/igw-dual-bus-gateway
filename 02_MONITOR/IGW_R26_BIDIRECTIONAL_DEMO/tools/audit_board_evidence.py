#!/usr/bin/env python3
"""Offline independent R12 board-evidence audit. No hardware inference.
Window categories derive from observed gateway availability, NOT independent
physical ground truth. Counts are consistency diagnostics, not general accuracy.
"""
from pathlib import Path
import hashlib,json,math,statistics,collections
R=Path(__file__).resolve().parents[1]
M=json.loads((R/'npu/generated/model_quantized.json').read_text())
def rnd(x):return math.floor(x+.5) if x>=0 else math.ceil(x-.5)
def sat(x):return max(0,min(255,x))
def p95(v):
 if not v:return 0
 v=sorted(v);z=.95*(len(v)-1);i=int(z);return v[i]+(v[min(i+1,len(v)-1)]-v[i])*(z-i)
def crc(a):
 c=65535
 for v in a:
  c^=v
  for _ in range(8):c=(c>>1)^(0xa001 if c&1 else 0)
 return c
def audit(p):
 data=p.read_bytes();d=json.loads(data);wlist=d['npu']['history'];summary=collections.Counter();transitions=[];clips=[];times=[];maxdiff=0;source_rows=[]
 assert d['software_revision']=='R12' and d['npu']['model_id']==M['model_id'] and 'token' not in d
 prevgen=None
 for w in wlist:
  rows=w['samples'];assert len(rows)==5 and all(len(a)==2 for a in rows)
  assert w['last_generation']==w['first_generation']+4
  if prevgen is not None:assert w['first_generation']==prevgen+1
  prevgen=w['last_generation'];features=[]
  for b in [0,1]:
   group=[a[b] for a in rows];ok=[a for a in group if a['status']=='ok'];bad=[a for a in group if a['status']!='ok'];l=[a['local_us']/1000 for a in ok];g=[a['bus_us']/1000 for a in ok]
   assert all(a['status'] in ['ok','timeout','cooldown'] for a in group)
   features += [sum(a['status']==s for a in group)/5 for s in ['ok','timeout','cooldown']]+[statistics.mean(l) if l else 0,p95(l),statistics.mean(g) if g else 0,p95(g),statistics.mean([a['local_us']/1000 for a in bad]) if bad else 0]
  assert all(abs(a-b)<1e-9 for a,b in zip(features,w['features']))
  raw=[rnd((v-a)/s/.0625)+128 for v,a,s in zip(features,M['mean'],M['std'])];x=list(map(sat,raw))
  h=[sat(rnd((bias+sum((v-128)*(q-128) for v,q in zip(x,weights)))*.015625)) for weights,bias in zip(M['weight1']['bytes'],M['bias1']['values'])]
  cpu=[sat(rnd((bias+sum(v*(q-128) for v,q in zip(h,weights)))*.0078125)+128) for weights,bias in zip(M['weight2']['bytes'],M['bias2']['values'])]
  q=w['reply'];diff=[abs(a-b) for a,b in zip(cpu,q['logits'])];pred=max(range(3),key=q['logits'].__getitem__)
  assert q['session']==d['session'] and q['model_id']==M['model_id'] and q['backend']=='Alnpu' and q['id']==w['id'] and q['inference_count']==w['id']
  assert x==q['input_u8'] and cpu==q['cpu_logits'] and q['max_diff']==max(diff) and all(v<=t for v,t in zip(diff,M['logit_lsb_tolerance']))
  assert q['status']=='ok' and q['outside_budget']==0 and q['class']==q['cpu_class']==pred and q['label']==M['labels'][pred]
  assert q['clipped_dims']==sum(v<0 or v>255 for v in raw)
  phase='transition' if any((a[b]['status']=='ok')!=(rows[0][b]['status']=='ok') for a in rows[1:] for b in [0,1]) else 'stable'
  avail=[rows[0][b]['status']=='ok' for b in [0,1]]
  category='transition' if phase=='transition' else 'both_available' if avail==[True,True] else 'can_unavailable_rs_available' if avail==[False,True] else 'can_available_rs_unavailable' if avail==[True,False] else 'both_unavailable'
  summary[(category,q['label'])]+=1
  if phase=='transition':transitions.append({'id':w['id'],'at_ms':w['at_ms'],'fractions':[features[:3],features[8:11]],'raw_prediction':q['label'],'numeric_status':q['status']})
  if q['clipped_dims']:clips.append({'id':w['id'],'dimensions':q['clipped_dims']})
  maxdiff=max(maxdiff,max(diff));times.append(q['enqueue_us']);source_rows.extend(rows)
 frame_count=0
 for job in d['jobs']:
  for t in job['trace']:
   q=t['reply'];assert q['seq']==t['seq'];a=bytes.fromhex(q['data'])
   if q['status']!='ok':assert job['execution']=='not_written' and q['op']=='READ';continue
   frame_count+=1
   if t['bus']=='CAN':
    req=bytes.fromhex(q['request_data']);assert len(req)==len(a)==8 and q['request_can_id']==529 and q['reply_can_id']==401 and int.from_bytes(req[:4],'little')==t['seq'] and a[:4]==req[:4] and a[4]==req[4]|128 and a[5]==q['address']
    if q['op']=='WRITE':assert a[6:]==req[6:] and int.from_bytes(a[6:],'big')==q['value']
    else:assert q['registers']==[int.from_bytes(a[6:],'big')]
   else:
    assert crc(a)==0 and a[0]==1
    if q['op']=='READ':assert a[1]==3 and len(a)==5+2*q['count'] and a[2]==q['count']*2 and q['registers']==[int.from_bytes(a[3+2*k:5+2*k],'big') for k in range(q['count'])]
    else:assert a[1]==6 and len(a)==8 and int.from_bytes(a[2:4],'big')==q['address'] and int.from_bytes(a[4:6],'big')==q['value']
  if job['execution']=='unchanged':assert job['before']==job['readback']==job['value'] and job['write_count_before']==job['write_count_after'] and not any(t['reply']['op']=='WRITE' for t in job['trace'])
  elif job['op']=='WRITE' and job['status']=='PASS':assert job['readback']==job['value'] and job['write_count_after']==(job['write_count_before']+1)%65536
  elif job['op']=='CROSS' and job['status']=='PASS':assert job['readback']==job['rs485_written'] and job['write_count_after']==(job['write_count_before']+2)%65536
 return {'file':p.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'session':d['session'],'uptime_ms':d['uptime_ms'],'reported_online_total':d['npu']['total'],'retained_windows_audited':len(wlist),'retained_ids':[wlist[0]['id'],wlist[-1]['id']],'observed_availability_consistency':[{'observed':a,'raw_prediction':b,'count':n} for (a,b),n in summary.items()],'transitions_raw_preserved':transitions,'clipped_windows':clips,'maximum_numeric_diff':maxdiff,'enqueue_us':{'minimum':min(times),'median':statistics.median(times),'maximum':max(times)},'raw_success_frames_checked':frame_count,'gateway_stats':{b:d['buses'][b]['stats'] for b in ['CAN','RS485']},'kernel_can_cumulative':d['kernel_can'],'reported_hardware_irq':d['npu']['hardware_irq'],'jobs':[ {k:j[k] for k in ['id','bus','op','address','value','status','execution','before','readback','write_count_before','write_count_after','begin_ms','end_ms']} for j in d['jobs']],'scope':'Numeric/feature/raw-frame audit and consistency with observed request availability. Not independent physical ground truth, full accuracy certification, FPGA timing signoff, or R12.2 board validation.'}
if __name__=='__main__':
 result=[audit(p) for p in sorted((R/'evidence/board_r12').glob('*.json'))]
 print(json.dumps({'result':'PASS_AUDIT','audited_sessions':result},ensure_ascii=False,indent=2))
