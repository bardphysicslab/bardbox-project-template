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
