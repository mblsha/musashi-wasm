#!/usr/bin/env node
import path from 'node:path';

function hex(n){return '0x'+(n>>>0).toString(16).padStart(8,'0');}

async function main(){
  const { createSystem } = await import(path.join(process.cwd(),'third_party','musashi-wasm','packages','core','dist','index.js'));
  const rom = new Uint8Array(0x200000);
  rom.set([0x20,0x79,0x00,0x10,0x6e,0x80], 0x416);
  rom.set([0x20,0x39,0x00,0x10,0x6e,0x80], 0x41c);
  const sys = await createSystem({ rom, ramSize: 0x100000 });
  sys.setRegister('sr', 0x2704);
  sys.setRegister('sp', 0x10f300);
  sys.setRegister('a0', 0x100a80);
  sys.setRegister('pc', 0x416);
  const events=[];
  const offR = sys.onMemoryRead((e)=>events.push({kind:'R',...e}));
  const offW = sys.onMemoryWrite((e)=>events.push({kind:'W',...e}));
  console.log('Step @', hex(0x416));
  await sys.step();
  console.log('Step @', hex(0x41c));
  await sys.step();
  offR?.(); offW?.();
  console.log('Events:', events);
}

main().catch(e=>{console.error('repro failed:', e?.stack||e); process.exit(1);});

