// 蓝牙：搜索日历设备、读取广播里的 IP、收发 JSON 命令
// 协议见固件 src/BleControl.h：命令和回复都是一行 JSON（以 \n 结尾），可以分包收发
const utf8 = require('./utf8');

const SERVICE = '9B6D0001-3C1A-4F7E-A1D2-5E8C7B4A2F10';
const RX = '9B6D0002-3C1A-4F7E-A1D2-5E8C7B4A2F10'; // 手机写入
const TX = '9B6D0003-3C1A-4F7E-A1D2-5E8C7B4A2F10'; // 设备通知

const ERRORS = {
  10001: '请打开手机蓝牙，并允许微信使用蓝牙',
  10002: '没有找到设备',
  10003: '连接失败，请靠近设备重试',
  10006: '蓝牙连接已断开',
  10012: '连接超时，请靠近设备重试',
};

function call(api, options) {
  return new Promise((resolve, reject) => api({ ...options, success: resolve, fail: reject }));
}

function bleError(e) {
  if (e instanceof Error) return e;
  return new Error(ERRORS[e && e.errCode] || (e && e.errMsg) || '蓝牙出错');
}

function isAndroid() {
  const info = wx.getDeviceInfo ? wx.getDeviceInfo() : wx.getSystemInfoSync();
  return info.platform === 'android';
}

async function openAdapter() {
  try {
    await call(wx.openBluetoothAdapter);
  } catch (e) {
    throw bleError(e);
  }
}

// 广播里的厂商数据：FF FF 'E' 'P' 版本 标志 IP×4。有的平台会去掉前面两字节公司 ID，所以按 'E' 'P' 找
function parseAdvertisement(buffer) {
  if (!buffer) return null;
  const b = new Uint8Array(buffer);
  for (let i = 0; i + 8 <= b.length; i++) {
    if (b[i] === 0x45 && b[i + 1] === 0x50 && b[i + 2] === 1) {
      const flags = b[i + 3];
      const online = (flags & 2) !== 0;
      return {
        provisioning: (flags & 1) !== 0,
        online,
        ip: online ? Array.from(b.slice(i + 4, i + 8)).join('.') : '',
      };
    }
  }
  return null;
}

let stopCurrentScan = null;

// 搜索附近的日历设备。onFound(device) 会被多次调用（信号强度变化、收到扫描响应时会再报一次）；
// 名称和 IP 在扫描响应里，有的上报里没有这些字段，调用方要把同一设备的多次上报合并起来。
// onFound 返回 true 表示已经找到想要的设备，可以提前结束
async function scan(onFound, durationMs) {
  await openAdapter();
  let finish;
  const done = new Promise((resolve) => (finish = resolve));
  const listener = (res) => {
    for (const d of res.devices) {
      const device = { deviceId: d.deviceId, rssi: d.RSSI, ...parseAdvertisement(d.advertisData) };
      if (d.localName || d.name) device.name = d.localName || d.name;
      if (onFound(device)) finish();
    }
  };
  wx.onBluetoothDeviceFound(listener);
  stopCurrentScan = finish;
  const timer = setTimeout(finish, durationMs);
  try {
    await call(wx.startBluetoothDevicesDiscovery, {
      services: [SERVICE],
      allowDuplicatesKey: true,
      powerLevel: 'high',
    });
    await done;
  } catch (e) {
    const err = bleError(e);
    if (isAndroid()) err.message += '（安卓手机搜索蓝牙需要打开系统定位）';
    throw err;
  } finally {
    clearTimeout(timer);
    stopCurrentScan = null;
    wx.offBluetoothDeviceFound(listener);
    wx.stopBluetoothDevicesDiscovery({});
  }
}

function stopScan() {
  if (stopCurrentScan) stopCurrentScan();
}

// 和一台设备的蓝牙连接
class Link {
  constructor(deviceId) {
    this.deviceId = deviceId;
    this.buf = [];
    this.waiters = [];
    this.onDisconnect = null;
    this.onValue = this.onValue.bind(this);
    this.onState = this.onState.bind(this);
  }

  async connect() {
    await openAdapter();
    const id = { deviceId: this.deviceId };
    try {
      await call(wx.createBLEConnection, { ...id, timeout: 10000 });
    } catch (e) {
      if (e.errCode !== -1) throw bleError(e); // -1：已经连着
    }
    wx.onBLEConnectionStateChange(this.onState);
    wx.onBLECharacteristicValueChange(this.onValue);
    try {
      if (isAndroid()) await call(wx.setBLEMTU, { ...id, mtu: 185 }).catch(() => {}); // iOS 会自动协商
      await call(wx.getBLEDeviceServices, id);
      await call(wx.getBLEDeviceCharacteristics, { ...id, serviceId: SERVICE });
      await call(wx.notifyBLECharacteristicValueChange, {
        ...id,
        serviceId: SERVICE,
        characteristicId: TX,
        state: true,
      });
    } catch (e) {
      this.close();
      throw bleError(e);
    }
  }

  close() {
    wx.offBLECharacteristicValueChange(this.onValue);
    wx.offBLEConnectionStateChange(this.onState);
    wx.closeBLEConnection({ deviceId: this.deviceId });
    this.failAll(new Error('蓝牙连接已关闭'));
  }

  failAll(err) {
    for (const w of this.waiters) {
      clearTimeout(w.timer);
      w.reject(err);
    }
    this.waiters = [];
  }

  onState(res) {
    if (res.deviceId !== this.deviceId || res.connected) return;
    this.failAll(new Error('蓝牙连接断开了'));
    if (this.onDisconnect) this.onDisconnect();
  }

  onValue(res) {
    if (res.deviceId !== this.deviceId || res.characteristicId.toUpperCase() !== TX) return;
    for (const b of new Uint8Array(res.value)) {
      if (b === 10) {
        const text = utf8.decode(this.buf);
        this.buf = [];
        this.dispatch(text);
      } else {
        this.buf.push(b);
      }
    }
  }

  dispatch(text) {
    let msg;
    try {
      msg = JSON.parse(text);
    } catch (e) {
      return;
    }
    const i = this.waiters.findIndex((w) => w.cmd === msg.cmd && (msg.error || w.accept(msg)));
    if (i < 0) return;
    const [w] = this.waiters.splice(i, 1);
    clearTimeout(w.timer);
    if (msg.error) {
      w.reject(new Error(msg.error));
    } else {
      w.resolve(msg);
    }
  }

  async write(obj) {
    const bytes = utf8.encode(JSON.stringify(obj) + '\n');
    for (let i = 0; i < bytes.length; i += 20) {
      await call(wx.writeBLECharacteristicValue, {
        deviceId: this.deviceId,
        serviceId: SERVICE,
        characteristicId: RX,
        value: bytes.slice(i, i + 20).buffer,
      });
    }
  }

  // 发命令并等回复；accept 用来跳过中间状态（wifi 命令会先回一个 connecting）
  request(obj, { accept = () => true, timeout = 15000 } = {}) {
    return new Promise((resolve, reject) => {
      const w = { cmd: obj.cmd, accept, resolve, reject };
      const drop = () => (this.waiters = this.waiters.filter((x) => x !== w));
      w.timer = setTimeout(() => {
        drop();
        reject(new Error('设备没有响应'));
      }, timeout);
      this.waiters.push(w);
      this.write(obj).catch((e) => {
        clearTimeout(w.timer);
        drop();
        reject(bleError(e));
      });
    });
  }

  status() {
    return this.request({ cmd: 'status' });
  }

  async scanWifi() {
    const msg = await this.request({ cmd: 'scan' }, { timeout: 20000 });
    return msg.aps;
  }

  // 成功时返回 { state: 'connected', ip }；失败时抛出设备给的原因（密码错误、找不到 WiFi…）
  setWifi(ssid, password) {
    return this.request(
      { cmd: 'wifi', ssid, password },
      { accept: (m) => m.state !== 'connecting', timeout: 30000 },
    );
  }
}

module.exports = { scan, stopScan, Link };
