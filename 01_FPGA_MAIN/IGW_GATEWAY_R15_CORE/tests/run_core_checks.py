from pathlib import Path
import argparse,subprocess,sys,tempfile,json,re,importlib.util,os,shutil
PKG=Path(__file__).resolve().parents[1];PRJ=PKG/'project_r15';ROOT=PRJ/'soc_prj';V=PKG/'validation'
p=argparse.ArgumentParser();p.add_argument('--iverilog',required=True);p.add_argument('--vvp',required=True);p.add_argument('--ivl-base');p.add_argument('--yosys');a=p.parse_args()
def iv(*xs):return [a.iverilog]+(['-B',a.ivl_base] if a.ivl_base else [])+list(map(str,xs))
results=[]
def run(name,cmd,timeout=100):
 r=subprocess.run(list(map(str,cmd)),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
 (V/(name+'.log')).write_text(r.stdout);print(name,'PASS' if r.returncode==0 else 'FAIL',flush=True)
 results.append({'name':name,'pass':r.returncode==0})
 if r.returncode:raise RuntimeError(r.stdout[-5000:])
 return r.stdout
run('source',[sys.executable,PRJ/'check_project.py'])
spec=importlib.util.spec_from_file_location('r15check',PRJ/'check_project.py');ck=importlib.util.module_from_spec(spec);spec.loader.exec_module(ck)
core=sorted((ROOT/'src/igw').glob('*.v'))
with tempfile.TemporaryDirectory(prefix='igw-r15-') as tmp:
 tmp=Path(tmp)
 tops=[('tb_gateway_decode',PKG/'tests/tb_gateway_decode.sv'),('tb_can_branch',PKG/'tests/tb_can_branch.sv')]+[(n,PRJ/'tests'/(n+'.sv')) for n in ['tb_rs485_branch','tb_rs485_axi','tb_two_boards_50m','tb_rs485_default_9600']]
 for n,tb in tops:
  dest=tmp/(n+'.vvp');run(n+'_compile',iv('-g2012','-s',n,'-o',dest,*core,tb));run(n,[a.vvp,dest])
 # Actual retained vendor declarations only: no inference/silicon claims from these stubs.
 d=(ROOT/'design_0/hdl/design_0.v').read_text();inst=ck.instances(d);headers=ck.headers(ROOT)
 modules={m for m,i,p in inst}-{ 'design_0_igw_rs485_branch_0','igw_axi_reject'}
 stubs=tmp/'vendor_interfaces_only.v';stubs.write_text('\n'.join(headers[m]+'\nendmodule' for m in sorted(modules)))
 branch=ROOT/'design_0/ip/design_0_igw_rs485_branch_0/design_0_igw_rs485_branch_0.v'
 for top,srcs in [('design_0',[ROOT/'design_0/hdl/design_0.v']),('system_top',[ROOT/'src/system_top.v',ROOT/'design_0/hdl/design_0.v'])]:
  run(top+'_actual_vendor_ports',iv('-g2012','-Wall','-s',top,'-o',tmp/(top+'.vvp'),*srcs,branch,*core,stubs))
 # Guarding the integrated input clock and the new disabled-address terminator.
 for name in ['clock','removed_npu','bad_pin','truncated_address']:
  cp=tmp/name;shutil.copytree(ROOT,cp)
  f=cp/'design_0/hdl/design_0.v';s=f.read_text();original=s
  if name=='clock':s=s.replace('.S_AXILite_ACLK(w_port__ARM_Processor_System_0__p2f_clk0)',".S_AXILite_ACLK(1'b0)")
  elif name=='removed_npu':s=s.replace('design_0_Video_NPU_0 design_0_Video_NPU_0_Inst','removed_ip removed_npu')
  elif name=='truncated_address':s=s.replace('.s_axi_awaddr(w_port__AXI_Protocol_Converter_0__ds_axi_awaddr)', '.s_axi_awaddr({1\'b0,w_port__AXI_Protocol_Converter_0__ds_axi_awaddr[31:0]})')
  else:
   q=cp/'src/constraint/io.adc';q.write_text(q.read_text().replace('LOCATION = L17','LOCATION = F20'))
  if name!='bad_pin':assert s!=original,'mutation made no change: '+name
  f.write_text(s)
  assert ck.check(cp),name
  results.append({'name':'reject_'+name,'pass':True})
 if a.yosys:
  for mod in ['igw_axi_reject','igw_rs485_branch']:
   ys='read_verilog '+' '.join(map(str,core))+'; hierarchy -check -top '+mod+'; proc; opt; check -assert; stat'
   run(mod+'_synthesis',[a.yosys,'-Q','-T','-p',ys])
common=['--iverilog',a.iverilog,'--vvp',a.vvp]+(['--ivl-base',a.ivl_base] if a.ivl_base else [])
for n in ['test_receiver.py','test_axi.py']:
 run(n,[sys.executable,PKG/'tests'/n,*common],timeout=150)
run('host_decoder',[sys.executable,PKG/'tests/test_host.py'])
run('linux_script_syntax',['sh','-n',PKG/'linux/collect_linux_video.sh'])
(V/'r15_checks.json').write_text(json.dumps({'result':'PASS_LOCAL_ONLY','checks':results,'TD_implemented':False,'board_verified':False,'Linux_DT_reviewed':False,'BOOT_generated':False},indent=2)+'\n')
print('R15_LOCAL_CHECKS=PASS; TD and Linux/board validation pending',flush=True)
