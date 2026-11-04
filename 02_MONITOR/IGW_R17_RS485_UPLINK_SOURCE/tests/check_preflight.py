#!/usr/bin/env python3
"""Host-only negative tests. Uses private fake DT/files/MMIO commands; no board IO."""
from pathlib import Path
import os, subprocess, tempfile, json

ROOT = Path(__file__).resolve().parents[1]
BOOT = '83a016557c5e93f9bf26ef5017fce67eb4221a28322c24bdf8b19c81e8075215'
DTB = '771d4692c3de93c00095515359f769becf2cba8aee144752905dec984745a1af'
REGS = {'0xf8800200':'0x00000005', '0x80400000':'0x32574749',
        '0x80400004':'0x00020000', '0x80400008':'0x0000001F',
        '0x8040000C':'0x00000000', '0x80400018':'0x02FAF080',
        '0x80400800':'0x4E414349', '0x80400804':'0x00010000',
        '0x80400808':'0x0000001F', '0x80400818':'0x02FAF080',
        '0x8040081C':'0x0007A120', '0x80400844':'0x00000010',
        '0x1f0000020':'0x000000BF'}
with tempfile.TemporaryDirectory() as directory:
    t = Path(directory); (t/'bin').mkdir(); dt = t/'live_dt'; dt.mkdir()
    for name in ['vdmafb@80100000','axi_dynclk@80101000','vdma@80200000','mipicam@80300000']:
        (dt/name).mkdir(); (dt/name/'status').write_bytes(b'disabled\0')
    s = (ROOT/'deploy/check_r15.sh').read_text()
    (t/'check.sh').write_text(s.replace('/sys/firmware/devicetree/base/soc',str(dt)))
    sha = t/'bin/sha256sum'
    sha.write_text('#!/usr/bin/env python3\nimport os,sys\nx=os.environ["TEST_BOOT"] if sys.argv[1].endswith("BOOT.bin") else os.environ["TEST_DTB"]\nprint(x+"  "+sys.argv[1])\n'); sha.chmod(0o755)
    dev = t/'bin/devmem'
    dev.write_text('#!/usr/bin/env python3\nimport os,sys,json\nfrom pathlib import Path\np=Path(os.environ["TEST_MMIO_LOG"])\nwith p.open("a") as f:f.write(" ".join(sys.argv[1:])+"\\n")\nassert len(sys.argv)==3 and sys.argv[2]=="32", "write operation forbidden"\nprint(json.loads(os.environ["TEST_REGS"])[sys.argv[1]])\n'); dev.chmod(0o755)
    base = dict(os.environ, PATH=str(t/'bin')+os.pathsep+os.environ['PATH'],
                TEST_BOOT=BOOT, TEST_DTB=DTB, TEST_REGS=json.dumps(REGS),
                TEST_MMIO_LOG=str(t/'mmio.log'))
    results = []
    def run(name, changes=None, success=False, no_mmio=False):
        log=t/'mmio.log'; log.unlink(missing_ok=True)
        r=subprocess.run(['sh',str(t/'check.sh')],env={**base,**(changes or {})},text=True,capture_output=True)
        assert (r.returncode==0)==success,(name,r.stdout,r.stderr)
        if no_mmio: assert not log.exists(),(name,'MMIO before file/live-DT gate')
        results.append({'case':name,'result':'PASS'})
    run('reviewed_files_and_live_identity',success=True)
    run('old_BOOT_rejected_before_MMIO',{'TEST_BOOT':'7b81535086c3096ded1e15061118236978730d56f9132d2616bc573cc8dd734c'},no_mmio=True)
    run('old_DTB_rejected_before_MMIO',{'TEST_DTB':'5c9b621f3bc61cf36cf1fdc68f383aa89fd71517c493904f4268be6bcbbdd184'},no_mmio=True)
    status=dt/'axi_dynclk@80101000/status';status.write_bytes(b'okay\0')
    run('live_dynclk_enabled_rejected_before_MMIO',no_mmio=True)
    status.write_bytes(b'disabled\0')
    for address,value,name in [('0x8040000C','0x00000001','active_TX_authorization'),('0x80400800','0x00000000','CAN_identity'),('0x80400018','0x05F5E100','wrong_RS485_clock'),('0x80400844','0x00000008','wrong_CAN_depth'),('0x1f0000020','0x00000000','wrong_NPU_capability')]:
        regs={**REGS,address:value};run(name,{'TEST_REGS':json.dumps(regs)})
    report={'result':'PASS_HOST_GUARD_TESTS','cases':results,'real_MMIO_access':False,'board_tested':False}
    (ROOT/'validation/preflight_tests.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
