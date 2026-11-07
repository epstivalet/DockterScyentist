/* Test the actual magnet preparation function embedded in RetroVerse's UI.
 * No external URLs, peer connections, or torrent downloads are involved. */
'use strict';
const fs=require('node:fs');
const vm=require('node:vm');
const assert=require('node:assert/strict');
const html=fs.readFileSync(__dirname+'/index.html','utf8');
const a=html.indexOf('function prepareMagnet(');
const b=html.indexOf('async function addMagnet()',a);
assert(a>=0&&b>a,'Could not locate magnet preparation logic');
const ctx=vm.createContext({URLSearchParams,encodeURIComponent,Error});
vm.runInContext(html.slice(a,b)+'\nthis.prepare=prepareMagnet;',ctx);
const prepare=ctx.prepare;
const hash='0123456789abcdef0123456789abcdef01234567';
const exact='magnet:?xt=urn:btih:'+hash;
const short=prepare(exact);
assert.equal(new URLSearchParams(short.magnet.slice(8)).get('xt'),'urn:btih:'+hash);
assert.equal(short.skipped,0);
const trackers=Array.from({length:90},(_,i)=>'udp://tracker'+i+'.example.org:6969/announce?code='+i+'&key=abc');
const long=exact+'&dn='+encodeURIComponent('Test Homebrew Torrent')+
 trackers.map(tr=>'&tr='+encodeURIComponent(tr)).join('')+'&so=0-2';
assert(long.length>1800,'Synthetic link must exceed the previous limit');
const compact=prepare(long);
assert(compact.magnet.length<=950,'Stored magnet must fit the backend buffer');
assert(encodeURIComponent(compact.magnet).length<=1850,'Form encoding must fit the API input buffer');
assert(compact.skipped>0,'Must report omitted tracker parameters');
assert(compact.kept<compact.trackers,'Must report retained vs total trackers');
const parsed=new URLSearchParams(compact.magnet.slice(8));
assert.equal(parsed.get('xt'),'urn:btih:'+hash);
assert.equal(parsed.get('so'),'0-2');
assert(parsed.getAll('tr').every(t=>trackers.includes(t)),'A tracker URL was modified or truncated');
assert.throws(()=>prepare('https://example.com/not-magnet'));
assert.throws(()=>prepare('magnet:?dn=missing-hash'));
assert.throws(()=>prepare('magnet:?xt=urn:btih:invalid'));
const btmh=prepare('magnet:?xt=urn:btmh:1220'+'a'.repeat(64));
assert.equal(new URLSearchParams(btmh.magnet.slice(8)).get('xt'),'urn:btmh:1220'+'a'.repeat(64));
console.log('PASS: short, oversized, tracker-integrity, file-selection, invalid input, and btmh magnets');
