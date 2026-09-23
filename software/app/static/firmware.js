'use strict';
const el = id => document.getElementById(id);
const notice = (text, error=false) => {el('notice').textContent=text;el('notice').className=error?'error':'';};
async function api(path, payload) {
  const r=await fetch('/ota/v1/admin/'+path,{credentials:'same-origin',cache:'no-store',
    ...(payload===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json','X-Bardbox-OTA':'1'},body:JSON.stringify(payload)})});
  const data=await r.json();if(!r.ok){const error=new Error(typeof data.detail==='string'?data.detail:'Request failed');error.status=r.status;throw error;}return data;
}
const columns=['Device','Last reported version','Assigned release','Update status','Last contact (UTC)','Action'];
function cell(row,text){const c=document.createElement('td');c.setAttribute('data-label',columns[row.children.length]||'');c.textContent=text;row.append(c);return c;}
function activityText(a){
 if(a.action==='release')return `Release ${a.details} uploaded and verified.`;
 let details;try{details=JSON.parse(a.details);}catch{return 'Update activity recorded.';}
 if(!details||typeof details.uid!=='string')return 'Update activity recorded.';
 if(a.action==='assign'&&typeof details.release_id==='string')return `Assigned ${details.release_id} to ${details.uid}.`;
 if(a.action==='stop_delivery')return `Stopped further downloads for ${details.uid}.`;
 return 'Update activity recorded.';
}
function utcTime(seconds){return new Date(seconds*1000).toISOString().replace('T',' ').slice(0,19);}

let refreshGeneration=0;
async function refresh(){
 const generation=++refreshGeneration;
 try {
  const data=await api('overview');if(generation!==refreshGeneration)return;el('devices').replaceChildren();
  for(const d of data.devices){
   const row=document.createElement('tr');cell(row,d.uid);cell(row,d.status?.running_version||d.last_report?.running_version||'—');
   cell(row,d.assignment?.release_id||'—');
   const state=(d.status?.state|| (d.assignment?'assigned':'No update assigned')).replaceAll('_',' ');
   cell(row,state+(d.status?.bytes?' · '+d.status.bytes.toLocaleString()+' bytes':'')+(d.status?.failure&&d.status.failure!=='none'?' · '+d.status.failure:'')+(d.assignment&&!d.assignment.active?' · delivery stopped':'')+(d.stale?' · no recent report':''));
   cell(row,d.last_seen?utcTime(d.last_seen):'—');
   const action=cell(row,'');const select=document.createElement('select');select.setAttribute('aria-label','Release for '+d.uid);
   const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent='Select compatible release';select.append(placeholder);
   for(const r of data.releases.filter(r=>['component','target','layout','config_schema','queue_schema'].every(k=>r[k]===d[k])&&r.size<=d.slot_bytes)){
    const o=document.createElement('option');o.value=r.release_id;o.textContent=r.release_id+' · '+r.version;select.append(o);
   }
   const button=document.createElement('button');button.textContent='Review update';button.disabled=true;
   select.onchange=()=>{button.disabled=!select.value;};
   button.onclick=()=>{if(!select.value)return;const rid=select.value;el('review-text').textContent=`Assign ${rid} to ${d.uid}?`;
    el('review').onclose=async()=>{if(el('review').returnValue!=='confirm')return;button.disabled=true;
     const retryKey='bardbox-ota-'+d.uid+'-'+rid;
     const requestId=sessionStorage.getItem(retryKey)||crypto.randomUUID();sessionStorage.setItem(retryKey,requestId);
     try{await api('assignments',{uid:d.uid,release_id:rid,request_id:requestId});sessionStorage.removeItem(retryKey);await refresh();notice('Update assigned to '+d.uid+'.');}
     catch(e){if(e.status&&e.status<500)sessionStorage.removeItem(retryKey);notice(e.message,true);button.disabled=false;}};
    el('review').returnValue='cancel';el('review').showModal();};
   action.append(select,button);
   if(d.assignment?.active){const stop=document.createElement('button');stop.textContent='Stop delivery';stop.onclick=async()=>{
    stop.disabled=true;try{await api('stop-delivery',{uid:d.uid,generation:d.assignment.generation});await refresh();notice('Further downloads stopped. Devices that already downloaded may still finish.');}
    catch(e){notice(e.message,true);stop.disabled=false;}};action.append(stop);}
   el('devices').append(row);
  }
  if(!data.devices.length){const row=document.createElement('tr');const c=cell(row,'No OTA devices registered yet.');c.colSpan=6;el('devices').append(row);}
  el('audit').replaceChildren();for(const a of data.audit){const li=document.createElement('li');li.textContent=`${utcTime(a.created)} UTC · ${a.actor} · ${activityText(a)}`;el('audit').append(li);}
  notice(data.devices.length+' registered '+(data.devices.length===1?'device.':'devices.'));
 }catch(e){if(generation===refreshGeneration)notice(e.message,true);}
}
el('refresh').onclick=refresh;
el('upload').onsubmit=async e=>{
 e.preventDefault();const button=e.submitter;button.disabled=true;
 try{
  const mf=el('manifest').files[0],image=el('image').files[0];
  if(mf.size>8192||image.size>2097152)throw new Error('File too large for this update service.');
  const envelope=JSON.parse(await mf.text()), bytes=new Uint8Array(await image.arrayBuffer());let binary='';
  for(let i=0;i<bytes.length;i+=8192)binary+=String.fromCharCode(...bytes.subarray(i,i+8192));
  await api('releases',{envelope,image_base64:btoa(binary)});await refresh();notice('Release verified and ready to assign.');
 }catch(e){notice(e.message,true);}finally{button.disabled=false;}
};
refresh();
