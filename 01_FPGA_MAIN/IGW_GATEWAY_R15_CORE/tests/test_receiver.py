"""Independent CAN polynomial long-division encoder and serial RTL tests."""
import argparse, json, random, shutil, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def bits(n,width):return [(n>>i)&1 for i in range(width-1,-1,-1)]
def crc15(seq):
    n=0
    for b in seq:n=(n<<1)|b
    n <<= 15
    for i in range(n.bit_length()-1,14,-1):
        if n&(1<<i):n ^= 0xc599 << (i-15)
    return n

def encode(ident,data=(),dlc=None,rtr=0,mutation=None):
    dlc=len(data) if dlc is None else dlc
    plain=[0]+bits(ident,11)+[rtr,int(mutation=='extended'),int(mutation=='fd')]+bits(dlc,4)
    if not rtr:
        for x in data:plain+=bits(x,8)
    crc=crc15(plain)
    seq=plain+bits(crc^(1 if mutation=='crc' else 0),15)
    stuffed=[];indices=[];last=None;run=0
    for b in seq:
        stuffed.append(b);run=run+1 if last==b else 1;last=b
        if run==5:
            indices.append(len(stuffed));stuffed.append(1-b);last=1-b;run=1
    final_stuff=bool(indices and indices[-1]==len(stuffed)-1)
    if mutation=='stuff':stuffed[indices[0]]=stuffed[indices[0]-1]
    tail=[int(mutation!='crc_delim'),int(mutation=='ack'),int(mutation!='ack_delim')]+[1]*7
    if mutation=='eof':tail[-3]=0
    return stuffed+tail,crc,final_stuff

TB='''`timescale 1ns/1ps
module tb;
reg clk=0;always #10 clk=~clk;
reg resetn=0,rx=1;reg[63:0]ticks=0;
wire valid,active,ready,rtr,ack;wire[10:0]id;wire[3:0]dlc;wire[14:0]crc;wire[63:0]data,sof;wire[31:0]cycles;wire[2:0]err;
igw_can_rx_observer dut(clk,resetn,rx,ticks,valid,id,dlc,rtr,ack,crc,data,sof,cycles,err,active,ready);
always @(posedge clk) begin ticks<=ticks+1;if(valid)$display("FRAME %d %d %d %d %h %h %d %d",id,dlc,rtr,ack,crc,data,sof,cycles);if(err)$display("ERROR %d",err);end
integer f,n,v,rc;reg[2047:0]path;
initial begin if(!$value$plusargs("STIM=%s",path))$fatal; repeat(5)@(negedge clk);resetn=1;f=$fopen(path,"r");
while(!$feof(f))begin rc=$fscanf(f,"%d %d\\n",n,v);if(rc==2)begin rx=v;repeat(n)@(negedge clk);end end
$fclose(f);$finish;end
initial begin #2000000000;$fatal(1,"simulation watchdog");end
endmodule
'''

def run(a):
    rng=random.Random(1422);stim=[(1400,1)];expected=[];malformed=0;finalstuff=0
    def add(ident,data=(),dlc=None,rtr=0,mutation=None,period=100,jitter=False,idle=1400):
        nonlocal malformed,finalstuff
        seq,c,fs=encode(ident,data,dlc,rtr,mutation);finalstuff+=fs
        for b in seq:stim.append((period+(rng.choice([-1,0,1]) if jitter else 0),b))
        stim.append((idle,1))
        if mutation:malformed+=1
        else:expected.append((ident,len(data) if dlc is None else dlc,rtr,1,c,sum(v<<(8*i) for i,v in enumerate(data)) if not rtr else 0))
    for per in [100,99,101]:
        for dlc in range(9):
            for pattern in [0,255,85,170]:add(rng.choice([0,0x7ff,0x211,0x191,0x212,0x192]),[pattern]*dlc,period=per,jitter=True)
        for dlc in range(9):add(rng.randrange(2048),dlc=dlc,rtr=1,period=per,jitter=True)
    for i in range(160):
        per=[99,100,101][i%3]
        add(rng.randrange(2048),[rng.randrange(256) for _ in range(rng.randrange(9))],period=per,idle=3*per)
    for mutation in ['stuff','crc','crc_delim','ack','ack_delim','eof','extended','fd']:
        add(0,[0]*8,mutation=mutation);add(0x211,[0x22,0x19])
    add(0,dlc=9,mutation='dlc');add(0x191,[])
    # A truncated stream must not produce a frame and subsequent traffic recovers.
    seq,_,_=encode(0x211,[12]*8)
    stim += [(100,b) for b in seq[:25]]+[(22000,1)]
    add(0x212,[3,4,5])
    with tempfile.TemporaryDirectory(prefix='igw-can-rx-') as td:
        td=Path(td);(td/'tb.sv').write_text(TB);(td/'stim.txt').write_text(''.join(f'{n} {b}\n' for n,b in stim))
        cmd=[a.iverilog]+(['-B',a.ivl_base] if a.ivl_base else [])+['-g2012','-s','tb','-o',str(td/'tb.vvp'),str(ROOT/'rtl/igw_can_rx_observer.v'),str(td/'tb.sv')]
        subprocess.run(cmd,check=True,capture_output=True,text=True)
        p=subprocess.run([a.vvp,str(td/'tb.vvp'),'+STIM='+str(td/'stim.txt')],capture_output=True,text=True,check=True,timeout=90)
    (ROOT/'validation/receiver_trace.log').write_text(p.stdout)
    actual=[];errors={}
    for line in p.stdout.splitlines():
        if line.startswith('FRAME '):
            x=line.split()[1:];actual.append(tuple(int(v,16 if i in [4,5] else 10) for i,v in enumerate(x[:6])))
        if line.startswith('ERROR '):errors[line.split()[1]]=errors.get(line.split()[1],0)+1
    for i,(x,y) in enumerate(zip(expected,actual)):
        if x!=y:raise AssertionError(f'frame {i}: expected {x}, got {y}')
    assert len(expected)==len(actual),(len(expected),len(actual),p.stdout[-1000:])
    assert all(errors.get(str(n),0)>0 for n in [1,2,3,4,5]),errors
    assert finalstuff>0
    result={'scope':'serial receive simulation only','valid_frames':len(actual),'malformed_cases':malformed+1,'final_crc_stuff_cases':finalstuff,'bit_periods_cycles':[99,100,101],'jitter_cycles':1,'minimum_intermission_bits':3,'errors_by_code':errors,'result':'PASS'}
    (ROOT/'validation/receiver.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--iverilog',default=shutil.which('iverilog'));p.add_argument('--vvp',default=shutil.which('vvp'));p.add_argument('--ivl-base');run(p.parse_args())
