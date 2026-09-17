// Unit tests with a minimal DOM double; not a visual/browser acceptance test.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');

class Element {
  constructor(tag='div'){this.tag=tag;this.children=[];this.value='';this.textContent='';}
  append(...nodes){this.children.push(...nodes);}
  replaceChildren(...nodes){this.children=[...nodes];}
  setAttribute(){}
}
function page(){
  const elements=new Map(), pending=[];
  const context=vm.createContext({
    document:{getElementById(id){if(!elements.has(id))elements.set(id,new Element());return elements.get(id);},
              createElement(tag){return new Element(tag);}},
    fetch(){return new Promise(resolve=>pending.push(data=>resolve({ok:true,json:async()=>data})));}
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname,'../software/app/static/firmware.js'),'utf8'),context);
  return {elements,pending,context};
}
const settle=()=>new Promise(resolve=>setImmediate(resolve));

test('an older response cannot overwrite a newer status refresh',async()=>{
  const {elements,pending,context}=page(); // Initial refresh is deliberately delayed.
  const latest=vm.runInContext('refresh()',context);
  pending[1]({devices:[],releases:[],audit:[]});
  await latest;
  assert.equal(elements.get('notice').textContent,'0 registered devices.');
  pending[0]({devices:[{uid:'outdated-device'}],releases:[],audit:[]});
  await settle();
  assert.equal(elements.get('notice').textContent,'0 registered devices.');
  assert.equal(elements.get('devices').children[0].children[0].textContent,'No OTA devices registered yet.');
});

test('review requires a selected compatible release',async()=>{
  const {elements,pending}=page();
  const compatibility={component:'app',target:'s3',layout:'dual',config_schema:'1',queue_schema:'bq1'};
  pending[0]({devices:[{uid:'node-one',...compatibility,slot_bytes:2097152}],
              releases:[{release_id:'r1',version:'0.7.0',...compatibility,size:100}],audit:[]});
  await settle();
  const [select,button]=elements.get('devices').children[0].children[5].children;
  assert.equal(select.children.length,2);
  assert.equal(button.disabled,true);
  select.value='r1';select.onchange();assert.equal(button.disabled,false);
  select.value='';select.onchange();assert.equal(button.disabled,true);
});

test('activity uses readable descriptions and malformed entries are harmless',async()=>{
  const {elements,pending,context}=page();
  pending[0]({devices:[{uid:'node-one',slot_bytes:1}],releases:[],audit:[
    {created:0,actor:'operator',action:'assign',details:JSON.stringify({uid:'node-one',release_id:'v1',generation:4})},
    {created:0,actor:'operator',action:'stop_delivery',details:'not JSON'},
    {created:0,actor:'operator',action:'stop_delivery',details:'null'}]});
  await settle();
  assert.equal(elements.get('notice').textContent,'1 registered device.');
  const entries=elements.get('audit').children.map(e=>e.textContent);
  assert.equal(entries[0],'1970-01-01 00:00:00 UTC · operator · Assigned v1 to node-one.');
  assert(entries.slice(1).every(text=>text.endsWith('Update activity recorded.')));
  assert.equal(vm.runInContext(`activityText({action:'stop_delivery',details:'{"uid":"node-one"}'})`,context),'Stopped further downloads for node-one.');
});

test('previous firmware version does not confirm a new assignment',async()=>{
  const {elements,pending}=page();
  pending[0]({devices:[{uid:'node-one',slot_bytes:1,assignment:{release_id:'next',active:0},
    status:null,last_report:{running_version:'0.8.1',state:'confirmed'}}],releases:[],audit:[]});
  await settle();
  const cells=elements.get('devices').children[0].children;
  assert.equal(cells[1].textContent,'0.8.1');
  assert.equal(cells[3].textContent,'assigned · delivery stopped');
});
