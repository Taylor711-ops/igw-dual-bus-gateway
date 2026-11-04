"""C web + actual RS485/CAN owners with simulated nodes, slots and MMIO.
Does not test physical boards or real-time hardware."""
import time
import unittest
import test_multi as multi

class FullStackSlotTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):multi.MultiTests.setUpClass()
 @classmethod
 def tearDownClass(cls):multi.MultiTests.tearDownClass()
 def setUp(self):
  self.f=multi.MultiTests('test_rs485_only_third_node_and_isolation')
  self.f.enable_coordinated_uploads=True;self.f.enable_unit3_upload=True;self.f.setUp()
 def tearDown(self):self.f.tearDown()
 def write(self,address,value,unit=3):
  f=self.f;q=f.command(unit,op='WRITE',bus='RS485',address=address,value=value)
  self.assertEqual(f.submit(q)[0],202);result=f.wait(q)
  self.assertEqual(result['status'],'PASS',result);return result
 def wait_value(self,value,after=0):
  f=self.f;end=time.monotonic()+12
  while time.monotonic()<end:
   _,s=f.request('GET','/api/state');u=s['rs485_uplink']
   if u['available']:
    n=u['snapshot']['devices'][2]
    if n['valid'] and n['value']==value and n['accepted']>after:return n
   time.sleep(.04)
  self.fail('matching node3 event did not reach the HTTP state')
 def test_controlled_upload_and_original_nodes(self):
  f=self.f
  self.write(29,54321);self.write(27,0xa526)
  first=self.wait_value(54321)
  self.assertGreater(f.producer.accepted,0)
  # The strict Web counter checks remain active while all three sources run.
  self.write(16,4321)
  q=f.command(1);self.assertEqual(f.submit(q)[0],202);j=f.wait(q)
  self.assertEqual(j['status'],'PASS',j)
  self.write(27,0xa527);self.wait_value(123,after=first['accepted'])
  self.assertTrue(any(r[1]==0x45 for r in f.rs_requests))
  self.assertTrue(all(ident!=0x213 for ident,_,_ in f.frames))

if __name__=='__main__':unittest.main(verbosity=2)
