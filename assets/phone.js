'use strict';
function showRomSource(){
  const path=location.hash.startsWith('#rom=')?location.hash.slice(5):'';
  const valid=path.length<=240&&/^\/[a-z0-9-]+-rom-[a-z0-9-]+\/$/.test(path);
  el('rom-source').hidden=!valid;
  if(valid){el('rom-link').href='https://www.romsgames.net'+path;el('rom-source').scrollIntoView();}
  else el('rom-link').removeAttribute('href');
}
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
el('start').onclick=()=>run(async()=>notice(await send('party','start-'+el('tone').value)));
el('pass').onclick=()=>run(async()=>{notice(await send('answer',`${player}:${round}:4`));locked=round;});
async function poll(){
  if(busy)return;
  try{const response=await fetch(new URL('state',base));if(!response.ok)throw Error('Phone session expired. Reopen setup on TV.');const state=await response.json();
    const selected=el('files').value,names=state.files.join('|');
    if(el('files').dataset.names!==names){el('files').replaceChildren(...state.files.map(name=>new Option(name,name)));el('files').dataset.names=names;if(state.files.includes(selected))el('files').value=selected;}
    phase=state.phase;round=state.round;
    const active=phase===1,reveal=phase===2,final=phase===3;
    el('lobby').hidden=active||reveal;el('join').hidden=!!player;
    el('start').textContent=final?'Rematch with fresh questions':'Start the party';
    el('round-label').textContent=active||reveal?`ROUND ${round+1}/${state.rounds} · ${state.kind?'READ THE ROOM':'TRIVIA TWIST'}${round>=6?' · DOUBLE POINTS':''}`:final?'THE RESULTS ARE IN':'THE LOBBY';
    el('timer').hidden=!active;el('timer').value=state.seconds;
    const ranked=[...state.players].sort((a,b)=>b.score-a.score),best=ranked[0]?.score;
    const winners=ranked.filter(p=>p.score===best).map(p=>p.name);
    el('question').textContent=active||reveal?state.question:final?(winners.length>1?'Shared glory: ':'Winner: ')+winners.join(' & '):'Take a seat. The room is judging you already.';
    el('explanation').textContent=reveal?state.explanation:active?`${state.seconds}s · ${state.players.filter(p=>p.answered).length}/${state.players.length} locked in${locked===round?' · Your answer is locked.':''}`:'';
    el('scores').replaceChildren(...ranked.map(p=>{const li=document.createElement('li');li.textContent=`${p.name}: ${p.score}${reveal?' (+'+p.gain+')':''}`;return li;}));
    if(phase===0||phase===3)locked=-1;
    el('pass').hidden=!active;el('pass').disabled=!player||locked===round;
    el('answers').replaceChildren();
    if(active||reveal)state.choices.forEach((choice,index)=>{const b=document.createElement('button');b.textContent=choice+(reveal&&state.kind?` · ${state.votes[index]} votes`:'');b.disabled=!active||!player||locked===round;if(reveal&&(state.winners&(1<<index)))b.classList.add('winner');b.onclick=()=>run(async()=>{notice(await send('answer',`${player}:${round}:${index}`));locked=round;await poll();});el('answers').append(b);});
  }catch(error){notice(error.message);}
}
poll();setInterval(poll,2000);

showRomSource();
window.addEventListener("hashchange",showRomSource);
