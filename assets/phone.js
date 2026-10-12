'use strict';
const base=new URL('.',location.href),el=id=>document.getElementById(id);
let busy=false,round=-1,phase=0,locked=-1,player=sessionStorage.getItem(location.pathname)||'';
const notice=text=>{el('notice').textContent=text;};
async function send(field,value){
  const response=await fetch(base,{method:'POST',body:new URLSearchParams({[field]:value})});
  const text=await response.text();if(!response.ok)throw Error(text);return text;
}
async function run(task){try{await task();}catch(error){notice(error.message||'Connection lost. Reopen phone setup on TV.');}}
document.querySelectorAll('[data-control]').forEach(button=>button.onclick=()=>run(async()=>notice(await send('control',button.dataset.control))));
el('upload').onclick=()=>run(async()=>{
  const file=el('file').files[0];if(!file)throw Error('Choose a file first.');
  if(file.size>512*1024*1024)throw Error('Maximum file size is 512 MB.');
  const name=file.name.replace(/[^A-Za-z0-9._-]/g,'_');if(name.length>100)throw Error('Please shorten the filename to 100 characters.');
  busy=true;el('upload').disabled=true;notice('Uploading '+name+' — keep this page open');
  try{const response=await fetch(new URL('upload/'+encodeURIComponent(name),base),{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:file});
    const text=await response.text();if(!response.ok)throw Error(text);notice(text);
  }finally{busy=false;el('upload').disabled=false;}
});
el('play').onclick=()=>run(async()=>notice(await send('play',el('files').value)));
el('subtitle').onclick=()=>run(async()=>notice(await send('subtitle',el('files').value)));
el('clear-subtitle').onclick=()=>run(async()=>notice(await send('subtitle','')));
el('cast').onsubmit=event=>{event.preventDefault();run(async()=>notice(await send('url',el('url').value)));};
el('join').onsubmit=event=>{event.preventDefault();run(async()=>{if(player){notice('Already joined on this phone.');return;}player=await send('join',el('name').value);sessionStorage.setItem(location.pathname,player);notice('Joined! Wait for everyone, then start.');});};
el('start').onclick=()=>run(async()=>notice(await send('party','start')));
async function poll(){
  if(busy)return;
  try{const response=await fetch(new URL('state',base));if(!response.ok)throw Error('Phone session expired. Reopen setup on TV.');const state=await response.json();
    const selected=el('files').value,names=state.files.join('|');
    if(el('files').dataset.names!==names){el('files').replaceChildren(...state.files.map(name=>new Option(name,name)));el('files').dataset.names=names;if(state.files.includes(selected))el('files').value=selected;}
    phase=state.phase;round=state.round;
    el('question').textContent=phase===1?`Question ${round+1}/5 · ${state.seconds}s: ${state.question}`:phase===2?`Answer: ${state.choices[state.correct]}`:phase===3?'Final scores — play again?':'Join, then start when everyone is ready.';
    el('scores').textContent=state.players.map(p=>`${p.name}: ${p.score}`).join(' · ');
    if(phase===0||phase===3)locked=-1;
    el('answers').replaceChildren();
    if(phase===1)state.choices.forEach((choice,index)=>{const b=document.createElement('button');b.textContent=choice;b.disabled=!player||locked===round;b.onclick=()=>run(async()=>{notice(await send('answer',`${player}:${round}:${index}`));locked=round;});el('answers').append(b);});
  }catch(error){notice(error.message);}
}
poll();setInterval(poll,2000);
