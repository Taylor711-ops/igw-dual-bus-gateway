from pathlib import Path
import json,os,struct,subprocess,tempfile
BASE=Path(__file__).resolve().parents[1]
# LeakSanitizer cannot inspect sandbox PID namespaces; bounds/UB checks remain enabled.
os.environ['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1'
os.environ['UBSAN_OPTIONS']='halt_on_error=1'
with tempfile.TemporaryDirectory() as td:
 td=Path(td);exe=td/'capture'
 subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-o',str(exe),str(BASE/'host/igw_can_pl_capture.c')],check=True)
 flags=(0x4321<<16)|(1<<7)|(1<<6)|8
 words=[15,0x211,flags,0x12345678,2,0x67452301,0xefcdab89,11080]
 fixture=td/'records.bin';fixture.write_bytes(struct.pack('<8I',*words)+struct.pack('<8I',16,0x192,(0x123<<16)|0xd4,3,0,0,0,6000))
 r=subprocess.run([str(exe),'--decode',str(fixture)],capture_output=True,text=True,check=True);rows=list(map(json.loads,r.stdout.splitlines()))
 assert rows[0]=={'seq':15,'id':529,'dlc':8,'rtr':0,'ack':1,'crc15':0x4321,'sof_ticks':0x212345678,'duration_cycles':11080,'data':'0123456789ABCDEF'},rows
 assert rows[1]['rtr']==1 and rows[1]['dlc']==4 and rows[1]['data']==''
 fixture.write_bytes(b'broken');r=subprocess.run([str(exe),'--decode',str(fixture)],capture_output=True,text=True);assert r.returncode==2
 for args in [[],['--capture-seconds','0'],['--capture-seconds','601'],['--capture-seconds','1x']]:
  assert subprocess.run([str(exe),*args],capture_output=True).returncode==2
 for env in [{},{'R15_BOOT_SHA256':'7b81535086c3096ded1e15061118236978730d56f9132d2616bc573cc8dd734c'}]:
  r=subprocess.run(['sh',str(BASE/'host/guarded_capture.sh')],capture_output=True,text=True,env={**os.environ,**env});assert r.returncode==2
 result={'result':'PASS','scope':'host decoder and safe argument/old-BOOT rejection only; no /dev/mem access','ASan_UBSan':True,'LeakSanitizer_enabled':False,'record_byte_order':True,'remote_frame_no_payload':True,'truncated_record_rejected':True,'invalid_arguments_rejected':True,'missing_and_R13_BOOT_hash_rejected':True}
 (BASE/'validation/host.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
