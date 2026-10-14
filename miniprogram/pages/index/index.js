const dev = require('../../utils/device');

// 与网页预览相同的颜色，模拟墨水屏观感
const WHITE = [0xf4, 0xf4, 0xf2];
const BLACK = [0x22, 0x22, 0x22];
const RED = [0xc8, 0x2a, 0x2a];

function signal(rssi) {
  if (rssi > -55) return '信号强';
  if (rssi > -70) return '信号中';
  return '信号弱';
}

function describe(s) {
  const lines = [
    { label: '设备时间', value: s.time || '还没对时' },
    { label: 'WiFi', value: s.wifi ? `${s.ssid}（${signal(s.rssi)}）` : '未连接' },
    { label: '日历订阅', value: s.calendars ? `${s.calendars} 个，近期日程 ${s.events} 条` : '还没有添加' },
  ];
  if (s.calendar_error) lines.push({ label: '同步出错', value: s.calendar_error });
  if (s.preview) lines.push({ label: '正在预览', value: `${s.preview}（10 分钟后自动回到今天）` });
  lines.push({ label: '墨水屏', value: s.epd ? '已连接' : '未启用（画面只在这里预览）' });
  return lines;
}

// /screen.raw：黑色平面 + 红色平面，各 1 bit/像素、行优先、高位在前；红色优先
function paint(canvas, buffer, w, h) {
  const stride = Math.ceil(w / 8);
  const plane = stride * h;
  if (buffer.byteLength < plane * 2) return;
  const black = new Uint8Array(buffer, 0, plane);
  const red = new Uint8Array(buffer, plane, plane);
  const scale = 2; // 按 2 倍像素画，缩放显示时更清晰
  const W = w * scale;
  canvas.width = W;
  canvas.height = h * scale;
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(W, h * scale);
  const px = img.data;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * stride + (x >> 3);
      const bit = 0x80 >> (x & 7);
      const c = red[i] & bit ? RED : black[i] & bit ? BLACK : WHITE;
      for (let dy = 0; dy < scale; dy++) {
        let o = ((y * scale + dy) * W + x * scale) * 4;
        for (let dx = 0; dx < scale; dx++, o += 4) {
          px[o] = c[0];
          px[o + 1] = c[1];
          px[o + 2] = c[2];
          px[o + 3] = 255;
        }
      }
    }
  }
  ctx.putImageData(img, 0, 0);
}

Page({
  data: {
    device: null, // { deviceId, name, ip }
    state: 'idle', // loading | ok | offline
    hint: '',
    status: null,
    lines: [],
    previewDate: '',
    screenHeight: 0,
  },

  onLoad() {
    const { windowWidth } = wx.getWindowInfo ? wx.getWindowInfo() : wx.getSystemInfoSync();
    this.setData({ screenHeight: Math.round((windowWidth - 60) * 0.75) }); // 页面和卡片的左右内边距共 60px
  },

  onShow() {
    const device = dev.saved();
    this.setData({ device, state: 'idle' });
    if (device) this.refresh();
  },

  onPullDownRefresh() {
    this.refresh().finally(() => wx.stopPullDownRefresh());
  },

  async refresh() {
    const device = this.data.device;
    if (!device || this.data.state === 'loading') return;
    this.setData({ state: 'loading' });
    try {
      await this.load(device.ip);
      return;
    } catch (e) {
      console.warn('局域网连接失败', e);
    }
    // 连不上：用蓝牙广播看看设备在不在、IP 有没有变
    let found = null;
    try {
      found = await dev.locate(device);
    } catch (e) {
      console.warn('蓝牙搜索失败', e);
    }
    if (found && found.online && found.ip !== device.ip) {
      device.ip = found.ip;
      dev.save(device);
      this.setData({ device });
      try {
        await this.load(device.ip);
        return;
      } catch (e) {
        console.warn('新 IP 也连不上', e);
      }
    }
    let hint = '连不上设备。请确认手机和设备连的是同一个 WiFi，并允许微信访问“本地网络”。';
    if (found && found.provisioning && !found.online) hint = '设备没有连上 WiFi，正在配网模式。点“重新配网”设置 WiFi。';
    if (!found) hint += '\n蓝牙也没有搜到设备，请确认设备已通电并在附近。';
    this.setData({ state: 'offline', hint });
  },

  async load(ip) {
    const status = await dev.api.status(ip);
    this.setData({ status, state: 'ok', lines: describe(status), previewDate: status.preview || '' });
    await this.drawScreen(ip, status);
  },

  drawScreen(ip, status) {
    return new Promise((resolve) => {
      this.createSelectorQuery()
        .select('#screen')
        .fields({ node: true, size: true })
        .exec(async (res) => {
          const canvas = res && res[0] && res[0].node;
          try {
            if (canvas) paint(canvas, await dev.api.screen(ip), status.width || 400, status.height || 300);
          } catch (e) {
            console.warn('读取屏幕画面失败', e);
          }
          resolve();
        });
    });
  },

  // 执行一个操作，完成后刷新状态和画面
  async run(title, action) {
    const ip = this.data.device.ip;
    wx.showLoading({ title, mask: true });
    try {
      await action(ip);
      await this.load(ip);
    } catch (e) {
      wx.showToast({ title: e.message, icon: 'none', duration: 3000 });
    } finally {
      wx.hideLoading();
    }
  },

  onSync() {
    this.run('同步中…', (ip) => dev.api.sync(ip));
  },

  onPreviewDate(e) {
    this.run('生成预览…', (ip) => dev.api.preview(ip, e.detail.value));
  },

  onToday() {
    this.run('加载中…', (ip) => dev.api.preview(ip));
  },

  goProvision() {
    wx.navigateTo({ url: '/pages/provision/provision' });
  },

  goCalendars() {
    wx.navigateTo({ url: '/pages/calendars/calendars' });
  },

  onForget() {
    wx.showModal({
      title: '移除设备',
      content: '只是从小程序里移除，设备上的设置不受影响。',
      success: (r) => {
        if (!r.confirm) return;
        dev.forget();
        this.setData({ device: null, status: null, state: 'idle' });
      },
    });
  },
});
