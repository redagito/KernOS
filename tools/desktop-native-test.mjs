// Native QEMU acceptance: PS/2 input, serial evidence and FAT readback only.
import net from 'node:net';
import fs from 'node:fs/promises';
import {openSync, closeSync} from 'node:fs';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'build','native');
const qemu=process.platform==='win32'?'C:/Program Files/qemu/qemu-system-x86_64.exe':'qemu-system-x86_64';
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
class VM {
  constructor(name,disk){this.name=name;this.disk=disk;this.log=path.join(build,name+'.log');this.pending=new Map();this.id=0;this.x=1000;this.y=740;}
  async start(){
    const server=net.createServer();await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
    const port=server.address().port;await new Promise(resolve=>server.close(resolve));
    this.errors=openSync(path.join(build,this.name+'.stderr'),'w');
    this.child=spawn(qemu,['-m','128','-no-reboot','-display','none','-monitor','none',
      '-drive',`format=raw,file=${path.join(build,'kernos.img')},if=ide`,
      '-device','ich9-ahci,id=ahci','-drive',`if=none,id=files,format=raw,file=${this.disk}`,
      '-device','ide-hd,drive=files,bus=ahci.0','-serial','file:'+this.log,
      '-qmp',`tcp:127.0.0.1:${port},server=on,wait=off`],
      {cwd:root,windowsHide:true,stdio:['ignore','ignore',this.errors]});
    this.child.on('error',error=>{this.spawnError=error;});
    await fs.writeFile(path.join(build,this.name+'.pid'),String(this.child.pid));
    for(let i=0;i<100;++i){
      if(this.spawnError)throw this.spawnError;
      try{await this.connect(port);break;}catch(error){if(i===99)throw error;await pause(100);}
    }
    await this.command('qmp_capabilities');
    await this.wait('edit: opened HELLO.C bytes=');
    const log=await this.text();if(log.includes('FAILED'))throw Error('Boot self-check failure\n'+log);
    return this;
  }
  async connect(port){
    this.socket=net.createConnection({host:'127.0.0.1',port});let wire='';
    this.socket.on('data',data=>{
      wire+=data.toString();let split;
      while((split=wire.indexOf('\n'))>=0){
        const line=wire.slice(0,split);wire=wire.slice(split+1);if(!line.trim())continue;
        const msg=JSON.parse(line),p=this.pending.get(msg.id);
        if(p){this.pending.delete(msg.id);clearTimeout(p.timer);msg.error?p.reject(Error(JSON.stringify(msg.error))):p.resolve(msg.return);}
      }
    });
    await new Promise((resolve,reject)=>{this.socket.once('connect',resolve);this.socket.once('error',reject);});
  }
  command(execute,args={}){
    return new Promise((resolve,reject)=>{
      const id=++this.id,timer=setTimeout(()=>{this.pending.delete(id);reject(Error('QMP timeout: '+execute));},8000);
      this.pending.set(id,{resolve,reject,timer});this.socket.write(JSON.stringify({execute,arguments:args,id})+'\n');
    });
  }
  async key(key){await this.command('human-monitor-command',{'command-line':'sendkey '+key+' 10'});await pause(55);}
  async type(value){
    const plain={' ':'spc','\n':'ret','.':'dot',',':'comma','-':'minus','=':'equal',';':'semicolon',"'":'apostrophe','/':'slash','\\':'backslash','[':'bracket_left',']':'bracket_right'};
    const shifted=Object.fromEntries([...'!@#$%^&*()_+{}:"<>?~|'].map((c,i)=>[c,['1','2','3','4','5','6','7','8','9','0','minus','equal','bracket_left','bracket_right','semicolon','apostrophe','comma','dot','slash','grave_accent','backslash'][i]]));
    for(const c of value)await this.key(shifted[c]?'shift-'+shifted[c]:/[A-Z]/.test(c)?'shift-'+c.toLowerCase():plain[c]||c);
  }
  async move(x,y){while(this.x!==x||this.y!==y){const dx=Math.max(-100,Math.min(100,x-this.x)),dy=Math.max(-100,Math.min(100,y-this.y));await this.command('input-send-event',{events:[{type:'rel',data:{axis:'x',value:dx}},{type:'rel',data:{axis:'y',value:dy}}]});this.x+=dx;this.y+=dy;await pause(50);}}
  async button(down){await this.command('input-send-event',{events:[{type:'btn',data:{button:'left',down}}]});await pause(90);}
  async click(x,y){await this.move(x,y);await this.button(true);await this.button(false);}
  async text(){try{return (await fs.readFile(this.log,'utf8')).replaceAll('\r','');}catch{return '';}}
  async wait(needle,after=0){for(let j=0;j<600;++j){const s=await this.text();if(s.slice(after).includes(needle))return s.length;if(s.slice(after).includes('KernOS FAULT:')||this.child.exitCode!==null)throw Error(s.slice(-2000));await pause(100);}throw Error('Missing '+needle+'\n'+(await this.text()).slice(-2000));}
  async close(){
    if(this.child && this.child.exitCode===null){try{await this.command('quit');}catch{this.child.kill();}await new Promise(resolve=>{if(this.child.exitCode!==null)resolve();else this.child.once('exit',resolve);});}
    this.socket?.destroy();if(this.errors!==undefined)closeSync(this.errors);
  }
}
async function diskFile(disk,name){
  const b=await fs.readFile(disk),spc=b[13],res=b.readUInt16LE(14),nf=b[16],fats=b.readUInt32LE(36),data=res+nf*fats;
  const [base,ext='']=name.toUpperCase().split('.'),wanted=base.padEnd(8)+ext.padEnd(3);
  const offset=c=>(data+(c-2)*spc)*512,next=c=>b.readUInt32LE(res*512+c*4)&0xfffffff;
  for(let c=b.readUInt32LE(44),hops=0;c<0xffffff8 && hops<1000;c=next(c),++hops){
    for(let p=offset(c);p<offset(c)+spc*512;p+=32){
      if(b.subarray(p,p+11).toString()!==wanted)continue;
      let size=b.readUInt32LE(p+28),cl=b.readUInt16LE(p+26)+(b.readUInt16LE(p+20)<<16);const chunks=[];
      while(size){if(cl<2||cl>=0xffffff8)throw Error('Broken saved chain');const n=Math.min(size,spc*512);chunks.push(b.subarray(offset(cl),offset(cl)+n));size-=n;cl=next(cl);}
      return Buffer.concat(chunks);
    }
  }
  throw Error('Missing file: '+name);
}
function check(good,label){if(!good)throw Error(label);console.log('PASS: '+label);}
async function main(){
  const disk=path.join(build,'desktop-acceptance.img');await fs.copyFile(path.join(build,'files.img'),disk);
  let vm=new VM('desktop-native',disk);
  const source='#include <stdio.h>\nint twice(int x) { return x * 2; }\nint main(void) { printf("hello %d\\n", twice(21)); printf("wide %lx\\n",0xffffffffffffffffUL); return 7; }\n';
  try{
    await vm.start();
    await vm.key('ctrl-n');await vm.type('abcdef\n');await vm.key('ctrl-a');await vm.key('ctrl-v');await vm.move(48,200);await vm.button(true);await vm.move(72,200);await vm.button(false);await vm.key('ctrl-x');await vm.key('ctrl-v');await vm.key('ctrl-s');await vm.type('UI.C\n');await vm.wait('edit: saved UI.C bytes=7');
    console.log('PASS: mouse selection, cut/paste and Save dialog');
    await vm.click(50,171);await vm.click(60,207);await vm.type('protected\n');await vm.click(699,142);await vm.key('esc');await vm.key('ctrl-s');await vm.type('GUARD.C\n');await vm.wait('edit: saved GUARD.C bytes=10');
    await vm.click(50,171);await vm.click(60,237);await vm.type('UI.C\n');await vm.wait('edit: opened UI.C bytes=7');console.log('PASS: File menus and canceled dirty close preserve text');
    let mark=(await vm.text()).length;
    await vm.move(200,143);await vm.button(true);await vm.move(300,183);await vm.button(false);await vm.wait('desktop: moved window',mark);
    await vm.move(812,585);await vm.button(true);await vm.move(852,625);await vm.button(false);await vm.wait('desktop: resized window',mark);
    await vm.click(808,181);await vm.wait('desktop: maximized window',mark);await vm.click(970,28);await vm.wait('desktop: restored window',mark);
    await vm.click(777,181);await vm.wait('desktop: hidden Notepad',mark);await vm.click(58,42);await vm.key('ctrl-s');await vm.wait('edit: saved UI.C bytes=7',mark);console.log('PASS: drag, resize, maximize, minimize and icon restore');
    await vm.click(252,42);await vm.wait('desktop: focus My files',mark);mark=(await vm.text()).length;await vm.click(360,42);await vm.wait('edit: opened HELLO.C bytes=',mark);
    await vm.key('ctrl-o');await vm.type('UI.C\n');await vm.wait('edit: opened UI.C bytes=7',mark);
    mark=(await vm.text()).length;await vm.key('ctrl-n');await vm.type('different\n');await vm.key('ctrl-s');await vm.type('UI.C\n');await vm.wait('desktop: dialog Replace',mark);await vm.key('esc');await vm.wait('desktop: dialog canceled',mark);await vm.key('ctrl-s');await vm.type('DIFF.C\n');await vm.wait('edit: saved DIFF.C bytes=10',mark);await vm.key('ctrl-q');await vm.wait('edit: closed',mark);
    console.log('PASS: desktop icons, fast filename input and overwrite cancellation');
    await vm.click(570,725);await vm.type('edit DEVTEST.C\n');await vm.wait('edit: opened DEVTEST.C bytes=0');await vm.type(source);await vm.key('ctrl-s');await vm.wait(`edit: saved DEVTEST.C bytes=${source.length}`);
    await vm.click(570,725);mark=(await vm.text()).length;await vm.type('cc DEVTEST.C\n');await vm.wait('ksh: compiler exit=0',mark);await vm.type('run DEVTEST\n');await vm.wait('hello 42',mark);await vm.wait('wide ffffffffffffffff',mark);await vm.wait('exited code=7',mark);console.log('PASS: saved C compiles and runs inside the graphical desktop, including 64-bit formatting');
    await vm.click(470,725);mark=(await vm.text()).length;await vm.type('@\n');await vm.key('ctrl-s');await vm.wait(`edit: saved DEVTEST.C bytes=${source.length+2}`,mark);await vm.click(570,725);mark=(await vm.text()).length;await vm.type('cc DEVTEST.C\n');await vm.wait('ksh: compiler exit=1',mark);await vm.type('run DEVTEST\n');await vm.wait('hello 42',mark);await vm.wait('exited code=7',mark);
    await vm.click(470,725);mark=(await vm.text()).length;await vm.key('backspace');await vm.key('backspace');await vm.key('ctrl-s');await vm.wait(`edit: saved DEVTEST.C bytes=${source.length}`,mark);await vm.click(570,725);mark=(await vm.text()).length;await vm.type('cc DEVTEST.C\n');await vm.wait('ksh: compiler exit=0',mark);console.log('PASS: compiler errors retain the previous executable; correction recompiles');
    const matrix='#include <stdio.h>\n#include <stdarg.h>\nint total(int n, ...) { va_list a; va_start(a, n); int s=0; for(int i=0;i<n;i++) s+=va_arg(a,int); va_end(a); return s; }\nstruct item { int x; };\nint main(void) { struct item a={3}; double x=1.25; double y=0x1.8p1; printf("matrix %d %d %d\\n",a.x,total(3,2,4,6),(int)(x*4+y)); return 0; }\n';
    await vm.type('edit MATRIX.C\n');await vm.wait('edit: opened MATRIX.C bytes=0');await vm.type(matrix);await vm.key('ctrl-s');await vm.wait(`edit: saved MATRIX.C bytes=${matrix.length}`);await vm.click(570,725);mark=(await vm.text()).length;await vm.type('cc MATRIX.C\n');await vm.wait('ksh: compiler exit=0',mark);await vm.type('run MATRIX\n');await vm.wait('matrix 3 12 8',mark);await vm.wait('exited code=0',mark);console.log('PASS: loops, structs, variadic functions and floating literals');
  }finally{await vm.close();}
  check((await diskFile(disk,'UI.C')).toString()==='abcdef\n','mouse editing is byte-exact on disk');
  check((await diskFile(disk,'GUARD.C')).toString()==='protected\n','canceling Close retains the full document');
  check((await diskFile(disk,'DIFF.C')).toString()==='different\n','canceling overwrite retains the new document');
  check((await diskFile(disk,'DEVTEST.C')).toString()===source,'saved source is byte-exact after error correction');
  check((await diskFile(disk,'DEVTEST.ELF')).subarray(0,4).toString('hex')==='7f454c46','guest compiler creates an ELF executable');
  vm=new VM('desktop-native-reboot',disk);
  try{await vm.start();await vm.key('ctrl-q');await vm.wait('edit: closed');await vm.click(570,725);await vm.type('edit DEVTEST.C\n');await vm.wait(`edit: opened DEVTEST.C bytes=${source.length}`);console.log('PASS: reboot and reopen against the same persistent disk');}finally{await vm.close();}
  check((await diskFile(disk,'DEVTEST.C')).toString()===source,'reboot preserves every saved source byte');
}
main().catch(error=>{console.error(error.stack||error);process.exitCode=1;});
