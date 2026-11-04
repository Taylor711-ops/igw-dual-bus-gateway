from pathlib import Path
import argparse,shutil,subprocess,tempfile,json
from test_receiver import encode
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--iverilog',default=shutil.which('iverilog'));p.add_argument('--vvp',default=shutil.which('vvp'));p.add_argument('--ivl-base');a=p.parse_args()
with tempfile.TemporaryDirectory() as td:
 td=Path(td);seq,_,_=encode(0x211,[1,0x23,0x45,0x67,0x89,0xab,0xcd,0xef]);(td/'stim').write_text('1400 1\n'+''.join(f'100 {b}\n' for b in seq)+'1400 1\n')
 cmd=[a.iverilog]+(['-B',a.ivl_base] if a.ivl_base else [])+['-g2012','-s','tb_can_axi','-o',str(td/'tb'),str(ROOT/'rtl/igw_can_rx_observer.v'),str(ROOT/'rtl/igw_can_observer_axi.v'),str(ROOT/'tests/tb_can_axi.sv')]
 subprocess.run(cmd,check=True,capture_output=True,text=True)
 r=subprocess.run([a.vvp,str(td/'tb'),'+STIM='+str(td/'stim')],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=40)
 (ROOT/'validation/axi.log').write_text(r.stdout);print(r.stdout);r.check_returncode()
 (ROOT/'validation/axi.json').write_text(json.dumps({'result':'PASS','scope':'serial-to-FIFO record plus AXI/FIFO unit simulation; injection used only for FIFO boundaries'},indent=2)+'\n')
