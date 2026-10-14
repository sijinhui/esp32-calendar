// 网页脚本回归测试：直接提取固件内的设置页，无浏览器 / npm 依赖。
// node tools/test_web_subscriptions.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../src/WebPreview.cpp', import.meta.url), 'utf8');
const html = source.split('const char kSetupPage[] PROGMEM = R"HTML(')[1].split(')HTML";')[0];
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    value: '', textContent: '', className: '', disabled: true,
    addEventListener() {}, setAttribute(key, value) { this[key] = value; },
  });
  return elements.get(id);
}
let config = { connected: true, ssid: 'test', ip: '127.0.0.1', provisioning: false,
  ics: ['https://example.com/a.ics'], ics_text: '# 工作\nhttps://example.com/a.ics # 备注', ics_sync_minutes: 45 };
let failSave = false;
const context = vm.createContext({
  document: { getElementById: element }, TextEncoder, AbortController, setTimeout, clearTimeout,
  fetch: async (url, options) => {
    if (options.method === 'POST') {
      if (failSave) throw new Error('模拟连接失败');
      config = { ...config, ...JSON.parse(options.body) };
      return { ok: true, headers: { get: () => 'text/plain' }, text: async () => 'ok' };
    }
    return { ok: true, headers: { get: () => 'application/json' }, json: async () => config };
  },
});
vm.runInContext(script, context);
const run = (code) => vm.runInContext(code, context);
await run('load()');
assert.equal(element('ics').value, config.ics_text);
assert.equal(element('icsSave').disabled, true);
assert.match(element('icsEditState').textContent, /45 分钟/);
const parse = (text) => JSON.parse(JSON.stringify(run(`parseIcs(${JSON.stringify(text)})`)));
assert.deepEqual(parse(' # 注释\r\nhttps://example.com/a.ics#fragment # 备注\n\nwebcal://example.com/b.ics\t# 家庭\n# https://example.com/off.ics').urls,
  ['https://example.com/a.ics#fragment', 'webcal://example.com/b.ics']);
assert.deepEqual(parse('# 注释\n\r\n').urls, []);
assert.match(parse('# 备注\n\nwrong').error, /第 3 行/);
assert.match(parse(Array(9).fill('https://example.com/a.ics').join('\n')).error, /最多 8/);
assert.equal(parse('#' + 'x'.repeat(2999)).error, '');
assert.match(parse('#' + '中'.repeat(1000)).error, /3000 字节/);
element('ics').value = '# 新备注\nhttps://example.com/a.ics # 工作';
run('updateIcs(true)');
assert.equal(element('icsSave').disabled, false);
assert.match(element('icsEditState').textContent, /未保存/);
await run('saveIcs()');
assert.equal(config.ics_text, element('ics').value);
assert.match(element('icsMsg').textContent, /已保存 1 个/);
await run('load()');
assert.equal(element('ics').value, config.ics_text);
failSave = true;
element('ics').value += '\n# 未保存备注';
run('updateIcs(true)');
const unsaved = element('ics').value;
await run('saveIcs()');
assert.equal(element('ics').value, unsaved);
assert.equal(element('icsSave').disabled, false);
assert.match(element('icsMsg').textContent, /模拟连接失败/);
element('ics').value = 'wrong';
run('updateIcs(true)');
assert.equal(element('icsSave').disabled, true);
assert.equal(element('ics')['aria-invalid'], 'true');
failSave = false;
element('ics').value = '# 只保留备注';
run('updateIcs(true)');
await run('saveIcs()');
assert.match(element('icsMsg').textContent, /没有启用/);
delete config.ics_text;
await run('load()');
assert.equal(element('ics').value, 'https://example.com/a.ics');
console.log('网页订阅回归测试通过：注释、URL #、数量/字节限制、保存/刷新、失败保留输入、旧配置兼容。');
