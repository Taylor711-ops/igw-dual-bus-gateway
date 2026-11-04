const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const html=fs.readFileSync(process.argv[2],'utf8'),script=html.match(/<script>([\s\S]*)<\/script>/)[1];
class Element {constructor(){this.children=[];this.disabled=false;this.value='1';this.files=[];this.textContent='';this.listeners={};this.option={disabled:false};}append(...n){this.children.push(...n);}replaceChildren(...n){this.children=n;}click(){if(this.href)downloads.push(this.download);}addEventListener(type,fn){this.listeners[type]=fn;}querySelector(){return this.option;}}
const ids=Object.fromEntries(['bus','unit','state','put','get','generate','refresh','cancel','file','size','jobs'].map(k=>[k,new Element()]));ids.bus.value='can';ids.size.value='4096';
const posts=[],downloads=[],jobs=[];let pendingRead,blobSaved,pollCallback,serverBusy=false,blocked=false;
const payload=Buffer.from([0,255,0,128,1]),sha=require('node:crypto').createHash('sha256').update(payload).digest('hex');
async function fetch(url,options={}){
 const json=v=>({ok:true,json:async()=>v});
 if(url==='/api/session')return json({token:'private',blocked,busy:serverBusy});
 if(url==='/api/jobs')return json({blocked,busy:serverBusy,jobs});
 if(url.startsWith('/api/file?')){
  assert.equal(options.headers['X-IGW-Token'],'private');posts.push({url,options});serverBusy=true;
  const j={ok:false,done:false,phase:'put',progress_bytes:2,total_bytes:5,job_id:'a'.repeat(32),bus:'rs485',unit:2,operation:'put',bytes:payload.length,sha256:''};jobs.push(j);return json({accepted:true,job_id:j.job_id});
 }
 if(url==='/api/cancel'){blocked=true;return json({ok:true});}
 if(url.startsWith('/api/download?')){assert.equal(options.headers['X-IGW-Token'],'private');return {ok:true,blob:async()=>new Blob([payload])};}
 throw Error('Unexpected fetch '+url);
}
vm.runInNewContext(script,{document:{getElementById:k=>ids[k],createElement:()=>new Element()},fetch,Blob,Uint8Array,ArrayBuffer,Number,Error,Array,URL:{createObjectURL:b=>{blobSaved=b;return 'blob:test';},revokeObjectURL:()=>{}},setTimeout:()=>{},setInterval:f=>{pollCallback=f;},crypto:require('node:crypto').webcrypto});
const flush=()=>new Promise(r=>setImmediate(r));
(async()=>{
 await flush();await flush();assert.equal(ids.put.disabled,false);
 ids.unit.value='3';ids.unit.listeners.change();assert.equal(ids.bus.value,'rs485');assert.equal(ids.bus.option.disabled,true);
 ids.unit.value='2';ids.unit.listeners.change();assert.equal(ids.bus.option.disabled,false);
 ids.bus.value='rs485';ids.unit.value='2';ids.file.files=[{size:5,name:'fixture.bin',arrayBuffer:()=>new Promise(r=>{pendingRead=r;})}];
 const first=ids.put.onclick(),second=ids.put.onclick();await second;assert.equal(posts.length,0);assert.equal(ids.put.disabled,true);
 pendingRead(payload.buffer.slice(payload.byteOffset,payload.byteOffset+payload.byteLength));await first;
 assert.equal(posts.length,1);assert.equal(posts[0].url,'/api/file?op=put&unit=2&size=5&bus=rs485');assert.equal(ids.put.disabled,true);assert.equal(ids.cancel.disabled,false);assert.equal(ids.jobs.children[0].children[2].value,2);
 serverBusy=false;Object.assign(jobs[0],{ok:true,done:true,phase:'complete',sha256:sha,progress_bytes:5});pollCallback();await flush();await flush();assert.equal(ids.put.disabled,false);
 await ids.jobs.children[0].children[2].onclick();assert.equal(downloads[0],'fixture.bin');assert.deepEqual(Buffer.from(await blobSaved.arrayBuffer()),payload);
 ids.file.files=[{size:8388609}];await ids.put.onclick();assert.equal(posts.length,1);assert.match(ids.state.textContent,/8388608/);
 serverBusy=true;Object.assign(jobs[0],{ok:false,done:false,phase:'get'});pollCallback();await flush();await ids.cancel.onclick();assert.match(ids.state.textContent,/停止请求/);assert.equal(ids.put.disabled,true);
 console.log('PASS frontend: bus/unit, async progress, duplicate submit prevention, verified download,8MiB limit,cancel');
})().catch(e=>{console.error(e);process.exitCode=1;});
