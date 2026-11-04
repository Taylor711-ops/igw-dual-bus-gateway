const fs=require('fs'),vm=require('vm'),assert=require('assert'),crypto=require('crypto');
const html=fs.readFileSync(require('path').join(__dirname,'../web/index.html'),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1],elements=new Map();
class Element{constructor(){this.textContent='';this.className='';this.value='';this.disabled=false;this.children=[];this.clientWidth=600;this.clientHeight=180}appendChild(x){this.children.push(x)}replaceChildren(...x){this.children=x}get rows(){return this.children}get cells(){return this.children}getContext(){return new Proxy({},{get:()=>()=>{}})}}
const el=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id)};
for(const [id,value] of Object.entries({bus:'CAN',address:'16',value:'12345',demoHostBus:'CAN',demoHostAddress:'16',demoHostValue:'12345',demoSlaveBus:'CAN',demoSlaveValue:'54321'}))el(id).value=value;
function bus(){return {valid:true,age_ms:10,rtt_us:530,values:Array(32).fill(0),stats:{attempts:3,ok:3,timeout:0,bad_reply:0,crc:0,uart:0}}}
const buses={CAN:bus(),RS485:bus()};buses.CAN.values[13]=0x1a26;
const node=()=>({unit:1,valid:true,value:1000,accepted:9,sequence:9,epoch:123,age_ms:20,duplicates:0});
const state={session:'a'.repeat(32),token:'b'.repeat(32),uptime_ms:4500,sample_interval_ms:3000,busy:false,device_count:1,devices:[{unit:1,buses}],buses,kernel_can:{available:false},jobs:[],history:[],uplink:{available:true,snapshot:{devices:[node()]}},rs485_uplink:{available:true,snapshot:{devices:[node()]}}};
let posts=[],publish=false,failPost=false,unchanged=false,changeEpoch=false;const memory=new Map();
const c={console,Uint8Array,crypto:crypto.webcrypto,document:{getElementById:el,createElement:()=>new Element()},window:{devicePixelRatio:1,addEventListener:()=>{}},sessionStorage:{getItem:k=>memory.get(k)||null,setItem:(k,v)=>memory.set(k,v),removeItem:k=>memory.delete(k)},setInterval:()=>0,setTimeout:fn=>{queueMicrotask(fn);return 0},fetch:async(path,opts={})=>{
 let data=state;
 if(opts.method==='POST') {let q=JSON.parse(opts.body);posts.push(q);assert.strictEqual(opts.headers['X-IGW-Token'],state.token);if(failPost)throw Error('response lost');const job={...q,status:'PASS',execution:unchanged?'unchanged':'verified',readback:q.value};state.jobs.push(job);data={job}}
 else if(path.startsWith('/api/job/'))data={job:state.jobs.find(j=>j.id===path.slice(9))};
 else if(path==='/api/state'&&publish){for(const [busName,key,reg] of [['CAN','uplink',30],['RS485','rs485_uplink',29]]){let d=state[key].snapshot.devices[0],w=[...posts].reverse().find(x=>x.address===reg);d.accepted++;d.sequence++;if(w)d.value=w.value;if(changeEpoch)d.epoch++}}
 return {ok:true,status:200,json:async()=>JSON.parse(JSON.stringify(data))};}};
vm.createContext(c);vm.runInContext(script,c);const tick=()=>new Promise(r=>setImmediate(r));
(async()=>{await tick();await el('demoHostSend').onclick();assert.strictEqual(posts[0].unit,1);assert.strictEqual(posts[0].address,16);assert(el('demoHostResult').textContent.includes('独立读回通过'));
 unchanged=true;await el('demoHostSend').onclick();assert(el('demoHostResult').textContent.includes('未发送新写入'));unchanged=false;
 el('demoHostAddress').value='28';let n=posts.length;await el('demoHostSend').onclick();assert.strictEqual(posts.length,n);el('demoHostAddress').value='16';
 buses.CAN.values[13]=0;await vm.runInContext('refresh()',c);await el('demoSlaveSend').onclick();assert.strictEqual(posts.length,n);assert(el('demoSlaveResult').textContent.includes('版本未确认'));
 buses.CAN.values[13]=0x1a26;await vm.runInContext('refresh()',c);publish=true;await el('demoSlaveSend').onclick();assert.deepStrictEqual(posts.slice(-2).map(x=>[x.address,x.value]),[[30,54321],[28,0xa526]]);assert(el('demoSlaveResult').textContent.includes('真实上报已由主机接收'));
 el('demoSlaveBus').value='RS485';el('demoSlaveValue').value='2468';await el('demoSlaveSend').onclick();assert.deepStrictEqual(posts.slice(-2).map(x=>[x.address,x.value]),[[29,2468],[27,0xa526]]);assert(el('demoSlaveResult').textContent.includes('真实上报已由主机接收'));
 await el('demoSlaveAuto').onclick();assert.strictEqual(posts.at(-1).address,27);assert.strictEqual(posts.at(-1).value,0);assert(el('demoSlaveResult').textContent.includes('自动计数已恢复'));
 publish=false;await el('demoSlaveSend').onclick();assert(el('demoSlaveResult').textContent.includes('未确认新的匹配上报'));
 publish=true;changeEpoch=true;await el('demoSlaveSend').onclick();assert(!el('demoSlaveResult').textContent.includes('真实上报已由主机接收'));changeEpoch=false;
 failPost=true;n=posts.length;await el('demoSlaveSend').onclick();assert.strictEqual(posts.length,n+1);assert(el('demoSlaveResult').textContent.includes('不会自动重发'));await vm.runInContext('refresh()',c);assert.strictEqual(posts.length,n+1);
 console.log('PASS R26 FRONTEND: independent readback, firmware gate, actual new CAN/RS485 upload, epoch change, stalled upload and uncertain write. DOM/fetch stubs; board and browser rendering not tested.');
})().catch(e=>{console.error(e);process.exitCode=1});
