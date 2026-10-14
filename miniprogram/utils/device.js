// 已添加的设备（存在本地），以及局域网 HTTP 接口
// 手机和设备在同一个 WiFi 时，wx.request 可以直接访问 http://设备IP/（不校验域名、不要求 https）
const ble = require('./ble');

const KEY = 'device';

function saved() {
  return wx.getStorageSync(KEY) || null;
}

function save(device) {
  wx.setStorageSync(KEY, device);
}

function forget() {
  wx.removeStorageSync(KEY);
}

function request(ip, path, { method = 'GET', data, timeout = 8000, binary = false } = {}) {
  return new Promise((resolve, reject) => {
    wx.request({
      url: `http://${ip}${path}`,
      method,
      data,
      timeout,
      header: data ? { 'content-type': 'application/json' } : {},
      responseType: binary ? 'arraybuffer' : 'text',
      success: (res) => {
        if (res.statusCode === 200) return resolve(res.data);
        const text = typeof res.data === 'string' && res.data ? res.data : `设备返回错误 ${res.statusCode}`;
        reject(new Error(text));
      },
      fail: (e) => reject(new Error(`连不上设备（${e.errMsg || ''}）`)),
    });
  });
}

const api = {
  status: (ip) => request(ip, '/api/status'),
  config: (ip) => request(ip, '/api/config'),
  saveCalendars: (ip, ics) => request(ip, '/api/config', { method: 'POST', data: { ics } }),
  sync: (ip) => request(ip, '/api/sync', { method: 'POST', timeout: 60000 }), // 同步完才回复，要十几秒
  preview: (ip, date) => request(ip, '/api/preview' + (date ? `?date=${date}` : ''), { method: 'POST' }),
  screen: (ip) => request(ip, '/screen.raw', { binary: true, timeout: 15000 }),
};

// 用蓝牙广播找设备（IP 变了、或者想知道它是不是在配网模式）。找不到返回 null
async function locate(device, durationMs = 4000) {
  let found = null;
  await ble.scan((d) => {
    if (d.deviceId !== device.deviceId && d.name !== device.name) return false;
    found = { ...found, ...d };
    return found.online || found.provisioning;
  }, durationMs);
  return found;
}

module.exports = { saved, save, forget, api, locate };
