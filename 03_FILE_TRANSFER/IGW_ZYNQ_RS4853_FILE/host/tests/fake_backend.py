#!/usr/bin/env python3
"""CLI contract fixture: real HTTP/fork/validation, no CAN/MMIO simulated claim."""
from pathlib import Path
import hashlib,os,signal,sys,time
a=sys.argv[1:]
if a[:2]==['--can','can0']:
 assert a[2]=='--unit';unit=int(a[3]);bus='can'
else:
 assert a[0]=='--unit' and a[2:4]==['--baud','9600'];unit=int(a[1]);bus='rs485'
op=a[4];root=Path(__file__).resolve().parent
with (root/'calls.txt').open('a') as f:f.write('%s %d %s\n'%(bus,unit,op))
slot=root/('node%d.bin'%unit);fault=os.environ.get('IGW_WEB_TEST_FAULT','')
def stopped(sig,frame):
 time.sleep(.2);(root/'cleanup.txt').write_text('BACKEND_CLEANUP_DONE');sys.exit(3)
signal.signal(signal.SIGTERM,stopped)
(root/'ready.txt').write_text('ready')
if fault=='exit':sys.exit(3)
if op=='put':data=Path(a[5]).read_bytes()
elif op=='get':
 if not slot.exists():sys.exit(3)
 data=slot.read_bytes()
elif op=='generate':data=bytes((i*71+unit*13)&255 for i in range(int(a[5])))
else:sys.exit(3)
if fault=='slow':
 print('PROGRESS bytes=%d/%d'%(len(data)//2,len(data)),flush=True);time.sleep(1.5)
if op in ['put','generate']:slot.write_bytes(data)
if op in ['get','generate']:Path(a[6] if op=='generate' else a[5]).write_bytes(data if fault!='corrupt' else data+b'x')
sha=hashlib.sha256(data).hexdigest()
if fault=='hash':sha='0'*64
print('PROGRESS bytes=%d/%d'%(len(data),len(data)),flush=True)
if bus=='can':print('RESULT=PASS_CAN_BLOCK_FILE unit=%d bytes=%d duration_ms=1 packets=1 wire_tx=1 link_retries=0 packet_retries=0 ignored=0 sha256=%s effective_Bps=1'%(unit,len(data),sha))
else:print('RESULT=PASS_RS485_FILE unit=%d bytes=%d baud=9600 duration_ms=1 requests=1 retries=0 ignored=0 sha256=%s effective_Bps=1'%(unit,len(data),sha))
