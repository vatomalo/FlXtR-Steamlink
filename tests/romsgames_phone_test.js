const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync('assets/phone.js','utf8').split('const base=')[0];
const elements={'rom-source':{scrollIntoView(){}},'rom-link':{removeAttribute(){delete this.href;}}};
const context={location:{hash:'#rom=/super-nintendo-rom-demo/'},el:id=>elements[id]};
vm.createContext(context);vm.runInContext(source+';showRomSource()',context);
assert.equal(elements['rom-link'].href,'https://www.romsgames.net/super-nintendo-rom-demo/');
assert.equal(elements['rom-source'].hidden,false);
for(const hash of ['#rom=//evil.invalid/nes-rom-game/','#rom=/nes-rom-../','#rom=javascript:alert(1)','#rom=/nes-rom-%22/','#party']){
 context.location.hash=hash;vm.runInContext('showRomSource()',context);
 assert.equal(elements['rom-source'].hidden,true);assert.equal(elements['rom-link'].href,undefined);
}
console.log('Phone handoff and invalid link checks passed');
