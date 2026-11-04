"""Compliant synthetic device RTS/RS DATA/DONE against the real C RS owner.
No physical CAN/UART timing, no trained-model hardware: tests only ownership accounting.
"""
import socket,struct,threading,time

def crc(p):
 c=65535
 for v in p:
  c^=v
  for _ in range(8):c=(c>>1)^(0xa001 if c&1 else 0)
 return c
class UploadProducer:
 def __init__(self,control,peer):
  self.control=control;self.peer=peer;self.control.settimeout(.005);self.quit=threading.Event();self.failed=[];self.accepted=0;self.paused=False
  self.nodes={u:dict(seq=0,epoch=100+u,nonce=0,pending=False,next=0,value=0) for u in (1,2)}
  self.thread=threading.Thread(target=self.run);self.thread.start()
 def send(self,ident,p):self.control.send(struct.pack('=IB3x8s',ident,8,p))
 def inject(self,p):
  # Only after GRANT: the real C owner guarantees no normal TX is in flight.
  if self.peer.r[0x1c]&1:raise AssertionError('RX mailbox busy despite upload grant')
  self.peer.r[0x40]=len(p)
  for i in range(0,len(p),4):self.peer.r[0x100+i]=int.from_bytes(p[i:i+4].ljust(4,b'\0'),'little')
  self.peer.r[0x50]=self.peer.r.get(0x50,0)+1
  self.peer.r[0x1c]|=1
 def run(self):
  try:
   while not self.quit.is_set():
    now=time.monotonic()
    for unit,n in self.nodes.items():
     if not self.paused and not n['pending'] and now>=n['next']:
      n['seq']+=1;n['value']=unit*1000+n['seq'];n['pending']=True
      self.send(0x400+unit,struct.pack('<IHH',n['epoch'],n['seq'],0x1752))
    try:raw=self.control.recv(16)
    except TimeoutError:continue
    ident,dlc,p=struct.unpack('=IB3x8s',raw)
    if 0x440<ident<=0x450:
     unit=ident-0x440;n=self.nodes[unit];nonce,epoch=struct.unpack('<II',p)
     assert n['pending'] and epoch==n['epoch'];n['nonce']=nonce
     frame=bytes((unit,0x41))+struct.pack('<IIH',nonce,epoch,n['seq'])+n['value'].to_bytes(2,'big')
     self.inject(frame+crc(frame).to_bytes(2,'little'))
    elif 0x480<ident<=0x490:
     unit=ident-0x480;n=self.nodes[unit];nonce,seq=struct.unpack('<IH',p[:6]);assert (nonce,seq)==(n['nonce'],n['seq']);self.accepted+=1
     self.send(0x4c0+unit,struct.pack('<IHH',nonce,seq,0x1744))
    elif 0x500<ident<=0x510:
     n=self.nodes[ident-0x500];n['pending']=False;n['next']=now+.025
    elif 0x540<ident<=0x550:
     unit=ident-0x540;nonce,seq,_=struct.unpack('<IHH',p)
     self.send(0x4c0+unit,struct.pack('<IHH',nonce,seq,0x1744))
  except (OSError,AssertionError) as error:
   if not self.quit.is_set():self.failed.append(repr(error))
 def pause(self):
  self.paused=True;end=time.monotonic()+3
  while time.monotonic()<end:
   if all(not n['pending'] for n in self.nodes.values()):return
   time.sleep(.005)
  raise AssertionError('device simulator did not release its slots')
 def close(self):self.quit.set();self.thread.join(2);self.control.close();assert not self.thread.is_alive();assert not self.failed,self.failed
