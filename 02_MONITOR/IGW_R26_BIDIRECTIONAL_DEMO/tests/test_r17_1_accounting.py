"""Actual C Web + both actual C gateways + RS uploads concurrently; fake NPU only."""
import json,time,unittest
import test_multi as multi
from test_npu import FakeNpu
class AccountingIntegrationTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):multi.MultiTests.setUpClass()
 @classmethod
 def tearDownClass(cls):multi.MultiTests.tearDownClass()
 def setUp(self):
  self.f=multi.MultiTests('test_16_nodes_sampled_and_cross_protocol_banks_are_independent');self.f.enable_coordinated_uploads=True;self.f.setUp();self.fake=None
 def tearDown(self):
  try:self.f.tearDown()
  finally:
   if self.fake:self.fake.close()
 def test_cross_repeated_while_real_rs_owner_accounts_uploads(self):
  f=self.f
  for i in range(12):
   q=f.command(i%2+1,address=16);self.assertEqual(f.submit(q)[0],202);j=f.wait(q)
   self.assertEqual(j['status'],'PASS',j);self.assertEqual(j['write_count_after']-j['write_count_before'],2);self.assertEqual(len(j['trace']),10)
   replies=[t['reply'] for t in j['trace'] if t['bus']=='RS485']
   self.assertTrue(all(r['pl_accounting']['mode']=='ordinary_plus_validated_uplink' for r in replies))
  self.assertGreater(f.producer.accepted,5)
  self.assertEqual((f.get(1,5),f.get(2,5)),(12,12))
 def test_five_sample_npu_window_progresses_with_rs_uploads(self):
  f=self.f;f.stop_web();self.fake=FakeNpu()
  f.web=f.launch('igw_web',['--nodes','2','--npu-port',str(self.fake.port),'--bind','127.0.0.1','--peer','127.0.0.1','--port',str(f.port_http),'--can-port',str(f.can_port),'--rs-port',str(f.rs_port),'--udp-timeout-ms','1500'])
  end=time.monotonic()+24
  while time.monotonic()<end:
   try:
    _,e=f.request('GET','/api/evidence')
    if e['npu']['total']:break
   except OSError:pass
   time.sleep(.03)
  else:self.fail('NPU online window never progressed with legitimate RS uploads')
  self.assertEqual(e['npu']['rejected'],0,e['npu']);self.assertGreater(f.producer.accepted,10)
  self.assertEqual(e['npu']['history'][-1]['reply']['backend'],'Alnpu') # fixture identity, NOT hardware evidence
  self.assertTrue(all(row[1]['status']=='ok' for row in e['npu']['history'][-1]['samples']))
 def test_unaccounted_extra_good_frame_still_blocks_write(self):
  f=self.f;f.stop_web()
  # Establish the real Web baseline by reading metadata through the worker,
  # then stop synthetic uploads and let the last scheduled slot drain.
  f.producer.pause();time.sleep(.04)
  f.web=f.launch('igw_web',['--nodes','2','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(f.port_http),'--can-port',str(f.can_port),'--rs-port',str(f.rs_port)])
  end=time.monotonic()+5
  while time.monotonic()<end:
   try:
    _,state=f.request('GET','/api/state')
    if all(d['buses']['RS485']['valid'] for d in state['devices']):f.initial=state;break
   except OSError:pass
   time.sleep(.02)
  else:self.fail('baseline not established')
  # Raw hardware count jumps without an accounted physical upload.
  f.rs_peer.r[0x50]=f.rs_peer.r.get(0x50,0)+1
  q=f.command(1,address=16);before=f.get(1,5);self.assertEqual(f.submit(q)[0],202);j=f.wait(q)
  self.assertEqual((j['status'],j['error'],j['execution']),('REVIEW','pl_counter_reset_or_extra_frame','not_written'));self.assertEqual(f.get(1,5),before)
if __name__=='__main__':unittest.main(verbosity=2)
