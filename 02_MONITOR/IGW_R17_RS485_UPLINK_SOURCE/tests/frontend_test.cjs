/* Frontend component execution with DOM/fetch stubs; not a browser render test. */
const fs=require('fs'),vm=require('vm'),assert=require('assert'),crypto=require('crypto');
const html=fs.readFileSync(require('path').join(__dirname,'../web/index.html'),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements=new Map();
class Element{
 constructor(tag='div'){this.tag=tag;this.textContent='';this.className='';this.value='';this.disabled=false;this.children=[];this.clientWidth=600;this.clientHeight=180;this.onclick=null}
 appendChild(x){this.children.push(x)}replaceChildren(...x){this.children=x}get rows(){return this.children}get cells(){return this.children}
 getContext(){return new Proxy({},{get:()=>()=>{}})}
}
for(const id of ['bus','address','value'])elements.set(id,new Element());
elements.get('bus').value='CAN';elements.get('address').value='16';elements.get('value').value='12345';
let posts=[],offline=false,lostReply=false;
const snap=()=>({valid:true,age_ms:20,rtt_us:530,values:Array(32).fill(0),stats:{attempts:3,ok:3,timeout:0,tx_fail:0,bad_reply:0,crc:0,uart:0,gap:0,overrun:0,watchdog:0}});
let state={session:'abcde123'.repeat(4),token:'1234abcd'.repeat(4),uptime_ms:4500,sample_interval_ms:3000,busy:false,buses:{CAN:snap(),RS485:snap()},kernel_can:{available:false},jobs:[],history:[{ms:2000,CAN:530,RS485:33000}]};
const storage=new Map();
const context={console,Uint8Array,crypto:crypto.webcrypto,document:{getElementById:id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id)},createElement:tag=>new Element(tag)},window:{devicePixelRatio:1,addEventListener:()=>{}},sessionStorage:{getItem:k=>storage.get(k)||null,setItem:(k,v)=>storage.set(k,v),removeItem:k=>storage.delete(k)},setInterval:()=>0,setTimeout:fn=>{queueMicrotask(fn);return 0},fetch:async(path,opts={})=>{
 if(offline)throw Error('network offline');
 let data=state;
 if(opts.method==='POST'){
  const q=JSON.parse(opts.body);posts.push(q);assert.strictEqual(opts.headers['X-IGW-Token'],state.token);
  const j={...q,status:'PASS',execution:q.op==='READ'?'read':'verified',readback:q.op==='READ'?0:q.value,before:0,write_count_before:0,write_count_after:1};state.jobs.push(j);data={job:j};
  if(lostReply)throw Error('response lost');
 }else if(path.startsWith('/api/job/')){data={job:state.jobs.find(x=>x.id===path.slice(9))}}
 return {ok:true,status:200,json:async()=>JSON.parse(JSON.stringify(data))};
}};
vm.createContext(context);vm.runInContext(script,context);
const tick=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 await tick();assert.strictEqual(posts.length,0);assert.strictEqual(elements.get('link').textContent,'已连接主机板');assert.strictEqual(elements.get('registers').rows.length,25);
 await elements.get('write').onclick();assert.strictEqual(posts.length,1);assert.strictEqual(posts[0].value,12345);assert(elements.get('result').textContent.includes('独立读回通过'));assert.strictEqual(elements.get('write').disabled,false);
 elements.get('address').value='31';await elements.get('cross').onclick();assert.strictEqual(posts.length,2);assert.strictEqual(posts[1].bus,'BOTH');assert.strictEqual(posts[1].op,'CROSS');
 elements.get('value').value='';await elements.get('write').onclick();assert.strictEqual(posts.length,2);
 elements.get('value').value='1';elements.get('address').value='15';await elements.get('write').onclick();assert.strictEqual(posts.length,2);
 elements.get('address').value='16';lostReply=true;await elements.get('write').onclick();assert.strictEqual(posts.length,3);assert(elements.get('result').textContent.includes('response lost'));
 await vm.runInContext('refresh()',context);await tick();assert.strictEqual(posts.length,3,'refresh must never re-POST uncertain write');
 state.npu={enabled:true,total:1,rejected:0,window_filled:2,window_rounds:5,status:'collecting',history:[{at_ms:4000,reply:{status:'ok',label:'normal',backend:'Alnpu',max_diff:1,enqueue_us:123}}]};
 await vm.runInContext('refresh()',context);assert.strictEqual(elements.get('npuClass').textContent,'正常');assert(elements.get('npuStatus').textContent.includes('分类待验证'));
 state.npu.history[0].window_phase='transition';state.npu.history[0].reply.label='can_link_open';state.npu.history[0].reply.clipped_dims=3;
 await vm.runInContext('refresh()',context);assert.strictEqual(elements.get('npuClass').textContent,'切换中');assert.strictEqual(elements.get('npuRaw').textContent,'CAN 链路异常');assert.strictEqual(elements.get('npuClipped').textContent,3);assert(elements.get('npuStatus').textContent.includes('等待稳定窗口'));
 state.npu.status='npu_reply_unavailable';await vm.runInContext('refresh()',context);assert.strictEqual(elements.get('npuClass').textContent,'—');
 state.device_count=2;state.devices=[{unit:1,buses:state.buses},{unit:2,buses:{CAN:snap(),RS485:snap()}}];
 state.devices[1].buses.CAN.values[16]=4321;
 await vm.runInContext('refresh()',context);elements.get('device').value='2';elements.get('device').onchange();
 assert.strictEqual(elements.get('registers').rows[9].cells[2].textContent,'4321');
 lostReply=false;await elements.get('read').onclick();assert.strictEqual(posts.at(-1).unit,2);
 await vm.runInContext('refresh()',context);assert.strictEqual(elements.get('device').value,'2');
 state.uplink={available:true,snapshot:{devices:[{unit:1,valid:true,value:1357,accepted:9,duplicates:1,sequence:9,age_ms:100}]}};
 await vm.runInContext('refresh()',context);assert.strictEqual(elements.get('uplinkRows').rows[0].cells[1].textContent,1357);assert.strictEqual(elements.get('uplinkStatus').textContent,'上报服务运行');
 state.rs485_uplink={available:true,snapshot:{devices:[{unit:2,valid:true,value:2468,accepted:4,duplicates:0,sequence:4,age_ms:50}]}};
 await vm.runInContext('refresh()',context);assert.strictEqual(elements.get('rsUplinkRows').rows[0].cells[1].textContent,2468);
 offline=true;await vm.runInContext('refresh()',context);assert.strictEqual(elements.get('npuClass').textContent,'—');assert(elements.get('link').textContent.includes('中断'));assert.strictEqual(elements.get('write').disabled,true);assert.strictEqual(elements.get('canRtt').textContent,'—');for(const row of elements.get('registers').rows)assert.strictEqual(row.cells[2].textContent,'—');
 assert.strictEqual(elements.get('uplinkRows').rows.length,0);assert.strictEqual(elements.get('rsUplinkRows').rows.length,0);assert(elements.get('uplinkStatus').textContent.includes('不可用'));
 console.log('PASS FRONTEND_COMPONENT: initial read-only, manual write/cross, bounds/blank validation, uncertain-response no retry, disconnected data cleared. DOM/fetch stubs; browser render NOT_RUN.');
})().catch(e=>{console.error(e);process.exitCode=1});
