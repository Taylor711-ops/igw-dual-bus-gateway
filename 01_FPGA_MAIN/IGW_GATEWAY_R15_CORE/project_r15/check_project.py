"""Read-only R15 source/integration verification; no board access."""
from pathlib import Path
import hashlib,json,re,xml.etree.ElementTree as E,sys,argparse
HERE=Path(__file__).resolve().parent
PKG=HERE.parent
def ports(body):return dict(re.findall(r'^\s*\.(\w+)\((.*)\),?\s*$',body,re.M))
def instances(text):return [(m,i,ports(b)) for m,i,b in re.findall(r'^\s*(\w+)\s+(\w+)\s*\((.*?)\);',text,re.M|re.S) if m!='module']
def interface(text):
    h=text.split(');',1)[0]
    result={};direction=None
    for field in h.split(','):
        m=re.search(r'\b(input|output|inout)\b(.*)',field,re.S)
        if m:direction=m[1];field=m[2]
        if direction:
            m=re.search(r'(\w+)\s*$',field)
            if m:result[m[1]]=direction
    return result
def headers(root):
    result={}
    for f in (root/'design_0/ip').rglob('*.v'):
        if '.enc.' in f.name:continue
        text=f.read_text(errors='replace')
        for match in re.finditer(r'\bmodule\s+(\w+)\b.*?\)\s*;',text,re.S):
            result.setdefault(match[1],match[0])
    for f in (root/'src/igw').glob('*.v'):
        text=f.read_text();m=re.search(r'\bmodule\s+(\w+)\b.*?\)\s*;',text,re.S)
        if m:result[m[1]]=m[0]
    result['DR1_LOGIC_IOTRIBUF']='module DR1_LOGIC_IOTRIBUF(input i,t,output o,inout io);'
    return result
def check(root,integrity=True):
    root=Path(root);errors=[]
    a=json.loads((PKG/'audit/integration.json').read_text())
    if integrity:
        m=json.loads((HERE/'manifest.json').read_text())
        for n,h in m['source_sha256'].items():
            p=root/n
            if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=h:errors.append('source hash mismatch: '+n)
    text=(root/'design_0/hdl/design_0.v').read_text()
    inst=instances(text);maps={i:p for m,i,p in inst};hs=headers(root)
    if maps!=a['new_ports']:errors.append('integrated port map differs from reviewed R15 map')
    original=a['baseline_resolved_ports']
    for i,p in maps.items():
        if i not in original:continue
        old=original[i]
        for k,v in p.items():
            allowed=(i=='design_0_ARM_Processor_System_0_Inst' and k.startswith('slave_hp1_axi_') and k!='slave_hp1_axi_aclk' and interface(hs['design_0_ARM_Processor_System_0']).get(k)=='input') or (i=='design_0_Concat_0_Inst' and k in ['In0','In2'])
            expected="1'b0" if allowed else old[k]
            if v!=expected:errors.append('changed retained connection: '+i+'.'+k)
    expected={'design_0_'+x+'_Inst' for x in a['retained_instances']}|{'mipi_iic_i2c0_scl_DR1_LOGIC_IOTRIBUF_0','mipi_iic_i2c0_sda_DR1_LOGIC_IOTRIBUF_0','igw_removed_peripherals'}
    if set(maps)!=expected:errors.append('active instance set mismatch')
    # Count independent drivers and prove every used module input is driven.
    top=interface(text);drivers={n:['external'] for n,d in top.items() if d in ['input','inout']}
    inputs=[]
    for mod,i,p in inst:
        if mod not in hs:errors.append('missing module interface: '+mod);continue
        desc=interface(hs[mod])
        for pin,value in p.items():
            if pin not in desc:errors.append('unknown port '+i+'.'+pin);continue
            if not value:continue
            if desc[pin] in ['output','inout']:
                if not re.fullmatch(r'\w+',value):errors.append('non-net output '+i+'.'+pin)
                else:drivers.setdefault(value,[]).append(i+'.'+pin)
            elif desc[pin]=='input':inputs.append((i+'.'+pin,value))
    assigned=dict(re.findall(r'assign\s+(\w+)\s*=\s*([^;]+);',text))
    for n,v in assigned.items():drivers.setdefault(n,[]).append('assign')
    declared=set(re.findall(r'\bwire\s+(?:\[[^\]]+\]\s*)?(\w+)\s*;',text))|set(top)
    for i,v in inputs:
        for n in re.findall(r'\b[A-Za-z_]\w*\b',v):
            if n in declared and n not in drivers:errors.append('undriven input: '+i+' <- '+n)
    for n,ds in drivers.items():
        if len(ds)>1 and top.get(n)!='inout':errors.append('multiple drivers '+n+': '+str(ds))
    # NPU/GW clocks remain directly on PS p2f_clk0; HP1 remains on old PLL150.
    ps=maps.get('design_0_ARM_Processor_System_0_Inst',{});clk=ps.get('p2f_clk0')
    for cell,pin in [('design_0_igw_rs485_branch_0_Inst','aclk'),('design_0_AXI_Protocol_Converter_0_Inst','aclk'),('design_0_Video_NPU_0_Inst','S_AXILite_ACLK'),('design_0_Video_NPU_0_Inst','M_AXI_ACLK'),('design_0_AXI_Matrix_1_Inst','aclk'),('design_0_AXI_Protocol_Converter_1_Inst','aclk')]:
        if maps.get(cell,{}).get(pin)!=clk:errors.append('50MHz clock changed: '+cell+'.'+pin)
    for pin in ['master_gp0_axi_aclk','master_gp1_axi_aclk','slave_hp0_axi_aclk']:
        if ps.get(pin)!=clk:errors.append('PS 50MHz connection changed: '+pin)
    br=maps.get('design_0_igw_rs485_branch_0_Inst',{});rej=maps.get('igw_removed_peripherals',{})
    for pin,v in rej.items():
        if pin.startswith('s_axi_') and br.get('m_axi_'+pin[6:])!=v:errors.append('reject branch mismatch '+pin)
    if br.get('CAN_RX_OBSERVE')!=ps.get('can0_rx_in'):errors.append('PL/PS CAN RX tap mismatch')
    # Recursively verify only active IP dependencies. Old video DI files are not active.
    al=(root/'soc_prj.al').read_text();listed=re.findall(r'<File Path="([^"]+)"',al)
    m=json.loads((HERE/'manifest.json').read_text())
    if listed!=m['project_inputs']:errors.append('project input list differs from reviewed R15 list')
    if '<MODULE>system_top</MODULE>' not in al:errors.append('top module changed')
    active_ips=re.findall(r'<File Path="([^"]+)"',re.search(r'<IP_FILE>(.*?)</IP_FILE>',al,re.S)[1])
    if active_ips!=a['active_ip_files']:errors.append('active IP list changed')
    pending=[root/x for x in active_ips];seen=set()
    for n in listed:
        if not (root/n).is_file():errors.append('missing project input '+n)
    while pending:
        p=pending.pop()
        if p in seen:continue
        seen.add(p)
        try:
            r=E.parse(p).getroot();ns={'i':r.tag.split('}')[0][1:]}
            for f in r.findall('.//i:file',ns):
                n=f.findtext('i:name','',ns);k=f.findtext('i:fileType','',ns);q=(p.parent/n).resolve()
                if not q.is_file():errors.append('missing IP dependency '+str(q))
                elif k=='IP':pending.append(q)
        except (OSError,E.ParseError) as e:errors.append(str(e))
    sdc=[x.strip() for x in (root/'src/constraint/timing.sdc').read_text().splitlines() if x.strip() and not x.lstrip().startswith('#')]
    if sdc!=['derive_clocks','set_false_path -from [get_nets {design_0_inst/design_0_ARM_Processor_System_0_Inst/p2f_rst0_n}]']:errors.append('unexpected timing constraints')
    adc=(root/'src/constraint/io.adc').read_text();pins={}
    for n,loc in re.findall(r'set_pin_assignment\s*\{\s*([^}]+?)\s*\}\s*\{[^}]*LOCATION\s*=\s*(\w+)',adc):
        n=n.strip()
        if loc in pins:errors.append('pin collision '+loc)
        pins[loc]=n
    for loc,n in {'F20':'can0_tx_out','F19':'can0_rx_in','L17':'RS485_TX0_3V3','N16':'RS485_RX0_3V3','N15':'RS485_DE0_3V3'}.items():
        if pins.get(loc)!=n:errors.append('bus pin changed '+loc)
    return errors
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=HERE/'soc_prj');a=p.parse_args()
    errors=check(a.root)
    for e in errors:print(e)
    print('R15_SOURCE_AND_WIRING='+('FAIL' if errors else 'PASS'))
    print('Source checks only; TD timing, Linux DT review and board tests remain required.')
    sys.exit(bool(errors))
