// 蓝牙配网：搜索设备 → 连接 → 选 WiFi、输密码 → 设备连上后保存设备信息
const ble = require('../../utils/ble');
const dev = require('../../utils/device');

Page({
  data: {
    step: 'scan', // scan | wifi | done
    error: '',
    // 第 1 步
    scanning: false,
    devices: [],
    connecting: '',
    // 第 2 步
    deviceName: '',
    provisioningMode: true,
    aps: [],
    wifiScanning: false,
    ssid: '',
    password: '',
    showPassword: false,
    joining: false,
    // 第 3 步
    ip: '',
  },

  onLoad() {
    this.startScan();
  },

  onUnload() {
    ble.stopScan();
    if (this.link) this.link.close();
  },

  async startScan() {
    if (this.data.scanning) return;
    this.setData({ scanning: true, devices: [], error: '' });
    const found = {};
    let timer = null;
    const flush = () => {
      timer = null;
      this.setData({ devices: Object.values(found).sort((a, b) => b.rssi - a.rssi) });
    };
    try {
      await ble.scan((d) => {
        found[d.deviceId] = { ...found[d.deviceId], ...d };
        if (!timer) timer = setTimeout(flush, 300); // 广播很频繁，攒一攒再刷新界面
        return false;
      }, 10000);
    } catch (e) {
      this.setData({ error: e.message });
    }
    if (timer) clearTimeout(timer);
    flush();
    this.setData({ scanning: false });
  },

  async onPickDevice(e) {
    if (this.data.connecting) return;
    const d = this.data.devices[e.currentTarget.dataset.index];
    ble.stopScan();
    this.setData({ connecting: d.deviceId, error: '' });
    const link = new ble.Link(d.deviceId);
    try {
      await link.connect();
      const status = await link.status();
      link.onDisconnect = () => this.setData({ error: '蓝牙连接断开了，请返回重新搜索设备' });
      this.link = link;
      const name = d.name || status.ap;
      this.device = { deviceId: d.deviceId, name };
      this.setData({
        step: 'wifi',
        connecting: '',
        deviceName: name,
        provisioningMode: status.provisioning,
        ssid: status.ssid || '',
      });
      this.scanWifi();
    } catch (err) {
      link.close();
      this.setData({ connecting: '', error: err.message });
    }
  },

  async scanWifi() {
    if (this.data.wifiScanning) return;
    this.setData({ wifiScanning: true });
    try {
      this.setData({ aps: await this.link.scanWifi() });
    } catch (e) {
      this.setData({ error: e.message });
    }
    this.setData({ wifiScanning: false });
  },

  onPickAp(e) {
    const ap = this.data.aps[e.currentTarget.dataset.index];
    this.setData({ ssid: ap.ssid, password: '', error: '' });
  },

  onSsid(e) {
    this.setData({ ssid: e.detail.value });
  },

  onPassword(e) {
    this.setData({ password: e.detail.value });
  },

  togglePassword() {
    this.setData({ showPassword: !this.data.showPassword });
  },

  async onJoin() {
    const ssid = this.data.ssid.trim();
    if (!ssid) return this.setData({ error: '请选择或输入 WiFi 名称' });
    if (this.data.joining) return;
    this.setData({ joining: true, error: '' });
    try {
      const r = await this.link.setWifi(ssid, this.data.password);
      dev.save({ ...this.device, ip: r.ip });
      this.setData({ step: 'done', ip: r.ip });
    } catch (e) {
      this.setData({ error: e.message });
    }
    this.setData({ joining: false });
  },

  goCalendars() {
    wx.redirectTo({ url: '/pages/calendars/calendars' });
  },

  onDone() {
    wx.navigateBack();
  },
});
