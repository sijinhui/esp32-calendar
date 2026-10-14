#include "WebPreview.h"

#include <ArduinoJson.h>
#include <CalendarSubscriptions.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "BmpWriter.h"
#include "Log.h"
#include "Settings.h"
#include "WifiManager.h"
#include "config.h"

namespace web {

namespace {

WebServer server(80);
const Canvas3C* canvas = nullptr;
Hooks hooks;

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>墨水屏日历</title>
<style>
body{margin:0;padding:16px;background:#2a2a2a;color:#ddd;font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;display:flex;flex-direction:column;align-items:center;gap:14px}
.frame{background:#d9d9d6;padding:14px;border-radius:10px;box-shadow:0 8px 28px #000a;max-width:100%;box-sizing:border-box}
img{display:block;width:800px;max-width:100%;image-rendering:pixelated}
.bar{display:flex;gap:8px;flex-wrap:wrap;justify-content:center}
button,input{font:inherit;font-size:14px;padding:6px 12px;border-radius:6px;border:1px solid #555;background:#383838;color:#eee}
button:hover{background:#474747}
pre{margin:0;font-size:12px;color:#999;white-space:pre-wrap;max-width:800px}
</style></head><body>
<div class="frame"><img id="screen" alt="屏幕画面"></div>
<div class="bar">
  <input type="date" id="date"><button onclick="preview()">预览该日期</button>
  <button onclick="post('/api/preview')">回到今天</button>
  <button onclick="post('/api/sync')">立即同步</button>
  <button id="epd" hidden onclick="post('/api/epd')">刷到墨水屏</button>
  <button onclick="location.href='/setup'">设置</button>
</div>
<pre id="status">加载中…</pre>
<script>
const $ = id => document.getElementById(id);
async function refresh() {
  $('screen').src = '/screen.bmp?t=' + Date.now();
  try {
    const s = await (await fetch('/api/status')).json();
    $('epd').hidden = !s.epd;
    $('status').textContent = JSON.stringify(s, null, 2);
  } catch (e) { $('status').textContent = '连接设备失败'; }
}
async function post(url) {
  $('status').textContent = '处理中…（同步日历或刷新墨水屏可能需要十几秒）';
  await fetch(url, {method: 'POST'});
  refresh();
}
function preview() { if ($('date').value) post('/api/preview?date=' + $('date').value); }
refresh();
setInterval(refresh, 60000);
</script></body></html>)HTML";

const char kSetupPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>墨水屏日历 · 设置</title>
<style>
body{margin:0;padding:16px;background:#2a2a2a;color:#ddd;font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif}
main{max-width:640px;margin:0 auto;display:flex;flex-direction:column;gap:14px}
h1{font-size:20px;margin:4px 0}
h2{font-size:16px;margin:0 0 10px}
section{background:#333;border-radius:10px;padding:14px}
.muted{color:#999;font-size:13px}
.head{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px}
ul{list-style:none;margin:0 0 10px;padding:0;max-height:260px;overflow:auto}
li{padding:9px 10px;border-radius:6px;display:flex;justify-content:space-between;gap:8px;cursor:pointer}
li:hover{background:#474747}
input,textarea,button{font:inherit;font-size:15px;border-radius:6px;border:1px solid #555;background:#2a2a2a;color:#eee;box-sizing:border-box}
input,textarea{width:100%;padding:9px 10px;margin-bottom:10px}
textarea{min-height:96px;font-size:13px}
button{padding:7px 16px;background:#383838}
button.primary{background:#b8322f;border-color:#b8322f;color:#fff}
label{display:block;margin-bottom:10px}
label input{width:auto;margin:0 6px 0 0}
.msg{margin-top:10px;font-size:14px;white-space:pre-wrap}
.err{color:#ff7b72}
.ok{color:#7ee787}
a{color:#8ab4f8}
.calendar{padding:20px;border:1px solid #484848;background:linear-gradient(145deg,#373737,#303030);border-radius:14px}
.calendar-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:18px}
.calendar-head h2{font-size:18px;margin:0 0 6px}
.calendar-head p{margin:0;line-height:1.6}
.badge{flex-shrink:0;border:1px solid #555;border-radius:20px;padding:5px 10px;font-size:12px;color:#ccc;font-variant-numeric:tabular-nums}
.badge.err{color:#ff7b72;border-color:#a74c46}
.editor-label{font-size:14px;margin-bottom:8px;font-weight:500}
#ics{display:block;min-height:190px;resize:vertical;margin:0;padding:14px;font:13px/1.85 ui-monospace,SFMono-Regular,Consolas,"PingFang SC","Microsoft YaHei",monospace;background:#242424;border-color:#505050;border-radius:9px;tab-size:2}
#ics::placeholder{color:#888;opacity:1}
#ics:focus{outline:2px solid #dc6b63;outline-offset:2px;border-color:#dc6b63}
#ics[aria-invalid="true"]{border-color:#ff7b72}
.calendar-help{margin:10px 0 14px;font-size:12px;line-height:1.8;color:#b6b6b6}
.calendar-help code{background:#444;padding:1px 5px;border-radius:4px;color:#eee}
.calendar-actions{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
.calendar-actions button{min-height:42px;padding:9px 18px}
.calendar-actions .muted{font-size:12px;line-height:1.6}
button:disabled{opacity:.5;cursor:not-allowed}
button:focus-visible,a:focus-visible,summary:focus-visible{outline:2px solid #dc6b63;outline-offset:3px}
.calendar details{margin-top:16px;border-top:1px solid #484848;padding-top:12px;color:#aaa;font-size:12px}
.calendar summary{cursor:pointer;padding:3px 0}
.calendar pre{white-space:pre-wrap;overflow-wrap:anywhere;line-height:1.8;padding:10px 12px;background:#272727;border-radius:7px;color:#bbb}
.calendar .privacy{margin:12px 0 0;line-height:1.7}
@media(max-width:420px){body{padding:12px}.calendar{padding:16px}#ics{padding:12px;font-size:13px}.calendar-head{gap:8px}.calendar-actions{align-items:flex-start}.calendar-actions button{width:100%}}
</style></head><body><main>
<h1>墨水屏日历 · 设置</h1>
<div id="state" class="muted">读取中…</div>
<section id="wifi" hidden>
  <h2>WiFi</h2>
  <div class="head"><span class="muted">附近的 WiFi（设备只支持 2.4GHz）</span><button onclick="scan(true)">刷新</button></div>
  <ul id="aps"></ul>
  <input id="ssid" placeholder="WiFi 名称" autocomplete="off" autocapitalize="off" autocorrect="off">
  <input id="pass" type="password" placeholder="密码（没有密码就留空）" autocomplete="off">
  <label class="muted"><input type="checkbox" onchange="$('pass').type = this.checked ? 'text' : 'password'">显示密码</label>
  <button class="primary" onclick="connect()">连接</button>
  <div id="wifiMsg" class="msg"></div>
</section>
<section class="calendar" aria-labelledby="calendarTitle">
  <div class="calendar-head">
    <div><h2 id="calendarTitle">日历订阅</h2><p class="muted">添加 Outlook、Google 或 iCloud 的 ICS 订阅链接。</p></div>
    <span id="icsCount" class="badge">0 / 8 个</span>
  </div>
  <label class="editor-label" for="ics">订阅地址与备注</label>
  <textarea id="ics" rows="7" placeholder="# 工作日历&#10;https://…/calendar.ics&#10;&#10;# 家庭日历&#10;webcal://…/calendar.ics # 家庭安排" spellcheck="false" autocomplete="off" autocapitalize="off" autocorrect="off" aria-describedby="icsHelp icsMsg" disabled></textarea>
  <div id="icsHelp" class="calendar-help">每行一个地址，最多 8 个。<code>#</code> 开头的行是注释；行尾备注用 <code>空格 + #</code> 分隔。注释和空行不会同步，保存后仍会保留。</div>
  <div class="calendar-actions">
    <span id="icsEditState" class="muted" role="status">正在读取订阅…</span>
    <button id="icsSave" class="primary" onclick="saveIcs()" disabled>保存并同步</button>
  </div>
  <div id="icsMsg" class="msg" role="status" aria-live="polite"></div>
  <details><summary>查看填写示例</summary><pre># Outlook 工作日历
https://example.com/work.ics # 工作安排

# 暂时停用：在地址前加 #
# https://example.com/family.ics</pre></details>
  <p class="muted privacy">请粘贴 ICS 链接，不是日历网页或 HTML 链接。订阅链接可能包含访问密钥，请勿转发。</p>
</section>
<div id="foot" class="muted" hidden>要更换 WiFi：长按设备上的 BOOT 键 3 秒进入配网模式。<a href="/">查看屏幕画面</a></div>
</main><script>
const $ = id => document.getElementById(id);
const sleep = ms => new Promise(r => setTimeout(r, ms));
let savedIcsText = '';
let icsReady = false;
let icsSaving = false;
let icsSyncMinutes = 30;
// 手机断开设备热点时 fetch 可能一直挂着，加个超时
async function api(url, body) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 5000);
  try {
    const opt = {signal: ctrl.signal};
    if (body) Object.assign(opt, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
    const r = await fetch(url, opt);
    if (!r.ok) throw new Error(await r.text());
    return (r.headers.get('Content-Type') || '').includes('json') ? r.json() : r.text();
  } finally { clearTimeout(timer); }
}
function show(id, text, cls) { $(id).textContent = text; $(id).className = 'msg ' + (cls || ''); }
async function load() {
  const c = await api('/api/config');
  const parts = [];
  if (c.provisioning) parts.push('配网模式（设备热点 ' + c.ap + '）');
  if (c.connected) parts.push('已连接 WiFi「' + c.ssid + '」，IP ' + c.ip);
  else if (c.ssid) parts.push('连不上「' + c.ssid + '」：' + c.error);
  else parts.push('还没有设置 WiFi');
  $('state').textContent = parts.join('，');
  savedIcsText = typeof c.ics_text === 'string' ? c.ics_text : c.ics.join('\n');
  $('ics').value = savedIcsText;
  icsSyncMinutes = c.ics_sync_minutes || 30;
  icsReady = true;
  $('ics').disabled = false;
  updateIcs();
  $('wifi').hidden = !c.provisioning;
  $('foot').hidden = c.provisioning;
  if (c.provisioning) {
    $('ssid').value = c.ssid;
    scan(false);
  }
}
async function scan(refresh) {
  const ul = $('aps');
  ul.innerHTML = '<li class="muted">正在扫描…</li>';
  let r = await api('/api/scan' + (refresh ? '?refresh=1' : ''));
  for (let i = 0; r.scanning && i < 15; i++) {
    await sleep(1000);
    r = await api('/api/scan');
  }
  ul.innerHTML = r.aps.length ? '' : '<li class="muted">没有扫描到 WiFi</li>';
  for (const ap of r.aps) {
    const li = document.createElement('li');
    const name = document.createElement('span');
    const meta = document.createElement('span');
    name.textContent = ap.ssid;  // WiFi 名称来自空中，只当文本处理
    meta.className = 'muted';
    meta.textContent = (ap.secure ? '加密 · ' : '开放 · ') + (ap.rssi > -55 ? '信号强' : ap.rssi > -70 ? '信号中' : '信号弱');
    li.append(name, meta);
    li.onclick = () => { $('ssid').value = ap.ssid; $('pass').value = ''; $('pass').focus(); };
    ul.append(li);
  }
}
async function connect() {
  const ssid = $('ssid').value.trim();
  if (!ssid) return show('wifiMsg', '请选择或输入 WiFi 名称', 'err');
  try {
    await api('/api/wifi', {ssid, password: $('pass').value});
  } catch (e) {
    return show('wifiMsg', e.message, 'err');
  }
  show('wifiMsg', '正在连接「' + ssid + '」…（最多 15 秒）');
  for (let i = 0; i < 30; i++) {
    await sleep(1000);
    let s;
    try {
      s = await api('/api/wifi');
    } catch (e) {
      return show('wifiMsg', '手机和设备热点断开了。一般说明设备已经连上 WiFi、热点随之关闭，请看屏幕确认。', 'ok');
    }
    if (s.state === 'connected') {
      return show('wifiMsg', '连接成功！设备 IP：' + s.ip + '\n设备热点稍后自动关闭。之后在家里的 WiFi 下打开 http://' + s.ip + '/ 就能查看和设置。', 'ok');
    }
    if (s.state === 'failed') return show('wifiMsg', '连接失败：' + s.error, 'err');
  }
  show('wifiMsg', '等待超时，请刷新页面查看状态', 'err');
}
function parseIcs(text) {
  const urls = [];
  let error = '';
  const trim = s => s.replace(/^[ \t\r]+|[ \t\r]+$/g, '');
  text.split('\n').forEach((raw, i) => {
    let line = trim(raw);
    if (!line || line.startsWith('#')) return;
    const comment = /[ \t\r]#/.exec(line);
    if (comment) line = trim(line.slice(0, comment.index));
    urls.push(line);
    if (!/^(https?|webcal):\/\//.test(line) && !error) error = '第 ' + (i + 1) + ' 行：请填写 https://、http:// 或 webcal:// 订阅地址，或在行首加 # 写备注';
  });
  if (new TextEncoder().encode(text).length > 3000) error = '订阅内容太长（含注释最多 3000 字节）';
  else if (urls.length > 8) error = '最多 8 个订阅地址，请删除多余地址或在行首加 # 暂时停用';
  else if (!error && urls.reduce((n, url) => n + new TextEncoder().encode(url).length + 1, 0) > 3000) error = '订阅地址太长';
  return {urls, error};
}
function updateIcs(clearMessage = false) {
  const parsed = parseIcs($('ics').value);
  const dirty = $('ics').value !== savedIcsText;
  $('icsCount').textContent = parsed.urls.length + ' / 8 个';
  $('icsCount').className = 'badge' + (parsed.error ? ' err' : '');
  $('ics').setAttribute('aria-invalid', parsed.error ? 'true' : 'false');
  $('icsSave').disabled = !icsReady || icsSaving || !dirty || !!parsed.error;
  $('icsSave').textContent = icsSaving ? '正在保存…' : '保存并同步';
  $('icsEditState').textContent = icsSaving ? '请稍候，正在保存到设备' : dirty ? '有未保存的修改' : parsed.urls.length ? '已保存 · 每 ' + icsSyncMinutes + ' 分钟自动同步' : '未启用订阅 · 可添加地址或备注';
  if (parsed.error) show('icsMsg', parsed.error, 'err');
  else if (clearMessage) show('icsMsg', '');
}
async function saveIcs() {
  if (!icsReady || icsSaving) return;
  const text = $('ics').value;
  const parsed = parseIcs(text);
  if (parsed.error) return show('icsMsg', parsed.error, 'err');
  icsSaving = true;
  $('ics').disabled = true;
  show('icsMsg', '');
  updateIcs();
  try {
    await api('/api/config', {ics_text: text});
    savedIcsText = text;
    show('icsMsg', parsed.urls.length ? '已保存 ' + parsed.urls.length + ' 个订阅，设备正在同步日历。备注已保留。' : '已保存，当前没有启用的日历订阅。', 'ok');
  } catch (e) {
    show('icsMsg', e.name === 'AbortError' ? '保存请求超时，内容仍保留在输入框中。请稍后重试。' : e.message, 'err');
  } finally {
    icsSaving = false;
    $('ics').disabled = false;
    updateIcs();
  }
}
$('ics').addEventListener('input', () => updateIcs(true));
load().catch(() => {
  $('state').textContent = '连接设备失败，请刷新页面重试';
  $('icsEditState').textContent = '未读取到订阅，暂不能保存';
});
</script></body></html>)HTML";

void sendScreen() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(bmp::fileSize(*canvas));
  server.send(200, "image/bmp", "");
  // 攒成 ~2KB 一块再发，比逐行发快得多
  static uint8_t buf[2048];
  size_t used = 0;
  bmp::write(*canvas, [&](const uint8_t* p, size_t n) {
    if (used + n > sizeof buf) {
      server.sendContent(reinterpret_cast<const char*>(buf), used);
      used = 0;
    }
    memcpy(buf + used, p, n);
    used += n;
  });
  if (used) server.sendContent(reinterpret_cast<const char*>(buf), used);
}

// 原始帧缓冲：黑色平面 + 红色平面，各 1 bit/像素、行优先、高位在前（小程序用 canvas 自己画）
void sendRaw() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(canvas->planeSize() * 2);
  server.send(200, "application/octet-stream", "");
  server.sendContent(reinterpret_cast<const char*>(canvas->blackPlane()), canvas->planeSize());
  server.sendContent(reinterpret_cast<const char*>(canvas->redPlane()), canvas->planeSize());
}

// 是不是从配网热点访问的（手机连着设备热点）
bool viaAp() { return wifi::provisioning() && server.client().localIP() == WiFi.softAPIP(); }

// 强制门户：手机连上热点后检测网络（captive.apple.com 等）会被重定向到设置页
void redirectToSetup() {
  server.sendHeader("Location", String("http://") + wifi::apIp().c_str() + "/setup", true);
  server.send(302, "text/plain", "");
}

void sendText(int code, const std::string& text) { server.send(code, "text/plain; charset=utf-8", text.c_str()); }

void sendJson(const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

bool readJson(JsonDocument& doc) {
  if (deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok) return true;
  sendText(400, "请求格式错误");
  return false;
}

void handleConfigGet() {
  JsonDocument doc;
  doc["ssid"] = settings::wifiSsid();
  doc["connected"] = wifi::connected();
  doc["ip"] = wifi::ip();
  doc["error"] = wifi::configured() ? wifi::lastError() : "";
  doc["provisioning"] = wifi::provisioning();
  doc["ap"] = wifi::apName();
  doc["ics_text"] = settings::icsText();
  doc["ics_sync_minutes"] = ICS_SYNC_MINUTES;
  JsonArray ics = doc["ics"].to<JsonArray>();
  for (const auto& url : settings::icsUrls()) ics.add(url);
  sendJson(doc);
}

void handleConfigPost() {
  JsonDocument doc;
  if (!readJson(doc)) return;
  if (doc["ics_text"].is<const char*>()) {
    const std::string text = doc["ics_text"].as<const char*>();
    const auto parsed = subscriptions::parse(text);
    if (!parsed.error.empty()) return sendText(400, parsed.error);
    settings::setIcsText(text);
    sendText(200, "ok");
    if (hooks.sync) hooks.sync();
    return;
  }
  if (!doc["ics"].is<JsonArray>()) return sendText(400, "缺少 ics 或 ics_text");
  std::vector<std::string> urls;
  for (JsonVariant v : doc["ics"].as<JsonArray>()) {
    const char* url = v.as<const char*>();
    if (url && *url) urls.emplace_back(url);
  }
  const std::string error = settings::checkIcsUrls(urls);
  if (!error.empty()) return sendText(400, error);
  settings::setIcsUrls(urls);
  sendText(200, "ok");
  if (hooks.sync) hooks.sync();  // 同步要十几秒，先回复再同步
}

void handleScan() {
  if (server.hasArg("refresh") || (!wifi::scanning() && wifi::scanResults().empty())) wifi::startScan();
  JsonDocument doc;
  doc["scanning"] = wifi::scanning();
  JsonArray aps = doc["aps"].to<JsonArray>();
  for (const auto& ap : wifi::scanResults()) {
    JsonObject o = aps.add<JsonObject>();
    o["ssid"] = ap.ssid;
    o["rssi"] = ap.rssi;
    o["secure"] = ap.secure;
  }
  sendJson(doc);
}

void handleWifiPost() {
  if (!wifi::provisioning()) return sendText(403, "请先长按设备上的 BOOT 键 3 秒进入配网模式");
  JsonDocument doc;
  if (!readJson(doc)) return;
  wifi::tryConnect(doc["ssid"] | "", doc["password"] | "");
  if (wifi::attempt().state == wifi::AttemptState::kFailed) return sendText(400, wifi::attempt().error);
  sendText(200, "ok");
}

void handleWifiGet() {
  const wifi::Attempt& a = wifi::attempt();
  JsonDocument doc;
  doc["state"] = wifi::stateName(a.state);
  doc["ssid"] = a.ssid;
  doc["error"] = a.error;
  doc["ip"] = wifi::ip();
  sendJson(doc);
}

void handlePreview() {
  int y = 0, m = 0, d = 0;
  if (server.hasArg("date") && sscanf(server.arg("date").c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
    server.send(400, "text/plain", "date 格式应为 YYYY-MM-DD");
    return;
  }
  if (hooks.preview) hooks.preview(y, m, d);
  server.send(200, "text/plain", "ok");
}

void handleAction(const std::function<void()>& action) {
  if (action) action();
  server.send(200, "text/plain", "ok");
}

}  // namespace

void begin(const Canvas3C& c, const Hooks& h) {
  canvas = &c;
  hooks = h;
  server.on("/", HTTP_GET, [] {
    if (viaAp()) return redirectToSetup();
    server.send_P(200, "text/html; charset=utf-8", kPage);
  });
  server.on("/setup", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", kSetupPage); });
  server.on("/screen.bmp", HTTP_GET, sendScreen);
  server.on("/screen.raw", HTTP_GET, sendRaw);
  server.on("/api/status", HTTP_GET, [] {
    server.send(200, "application/json", hooks.statusJson ? hooks.statusJson() : String("{}"));
  });
  server.on("/api/preview", HTTP_POST, handlePreview);
  server.on("/api/sync", HTTP_POST, [] { handleAction(hooks.sync); });
  server.on("/api/epd", HTTP_POST, [] { handleAction(hooks.pushEpd); });
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_GET, handleWifiGet);
  server.on("/api/wifi", HTTP_POST, handleWifiPost);
  server.onNotFound([] {
    if (viaAp()) return redirectToSetup();
    server.send(404, "text/plain", "not found");
  });
  server.begin();

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  LOGF("网页预览：http://%s.local/", HOSTNAME);
}

void loop() { server.handleClient(); }

}  // namespace web
