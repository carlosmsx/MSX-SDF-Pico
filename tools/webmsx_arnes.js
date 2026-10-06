window.scr = function(){ const v=WMSX.room.machine.vdp; const r1=v.eval('register[1]'), r2=v.eval('register[2]'); const text=(r1&0x10)!==0; const cols=text?40:32; const nt=(r2&0x0F)<<10; const vram=v.eval('vram'); const lines=[]; for(let y=0;y<24;y++){ let s=''; for(let x=0;x<cols;x++){ const c=vram[nt+y*cols+x]; s+= (c>=32&&c<127)?String.fromCharCode(c):(c==0xFE?'#':'.'); } lines.push(s.replace(/\s+$/,'')); } return lines.join('\n'); };
window.run = n => { for (let i=0;i<n;i++) WMSX.room.mainVideoClockPulse(); };
window.typ = (s, f) => { WMSX.room.keyboard.typeString(s); run(f || (s.length*12+60)); return scr(); };
window.boot = () => { WMSX.room.machine.userPowerOn(); run(500); typ("\r\r\r", 200); };
'ok'
window.start = async (cmd) => { boot(); typ("b:\r",100); return typ(cmd || "vi sample.txt\r", 500); };
