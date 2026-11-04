from test_rw import adu

class Unit3:
    def __init__(self):
        self.reg = [0]*32
        self.reg[0]=0x1234;self.reg[1]=3;self.reg[4]=0x0801
        self.reg[6]=0xffff;self.reg[8]=0x0301;self.reg[9]=0x0d02
        self.sequence=0;self.last_nonce=0;self.behavior='ok'
    def __call__(self, req):
        if len(req)!=8 or req[0]!=3 or adu(req[:6])!=req:return b''
        fc=req[1];a=int.from_bytes(req[2:4],'big');n=int.from_bytes(req[4:6],'big')
        if fc==3 and 0<=a<=31 and 1<=n<=8 and a+n<=32:
            return adu(bytes([3,3,n*2])+b''.join(x.to_bytes(2,'big') for x in self.reg[a:a+n]))
        if fc==6 and (16<=a<=26 or a in (27,29,31)):
            self.reg[a]=n;self.reg[5]+=1;self.reg[6]=a;self.reg[7]=n;return req
        if fc==0x45 and self.reg[27] in (0xa526,0xa527):
            nonce=int.from_bytes(req[2:6],'little')
            if not nonce or nonce==self.last_nonce or self.behavior=='drop':return b''
            self.last_nonce=nonce;self.sequence=(self.sequence+1)&65535
            val=self.reg[29] if self.reg[27]==0xa526 else 123
            if self.behavior=='wrong_nonce':nonce=(nonce+1)&0xffffffff
            return adu(bytes([3,0x46])+nonce.to_bytes(4,'little')+self.sequence.to_bytes(2,'big')+val.to_bytes(2,'big'))
        return b''

