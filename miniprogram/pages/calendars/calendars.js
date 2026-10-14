// 日历订阅：增删 iCal 地址，保存到设备（局域网 HTTP）
const dev = require('../../utils/device');

// 订阅地址里常带私密令牌，只显示域名和文件名
function label(url) {
  const m = url.match(/^(?:https?|webcal):\/\/([^/?#]+)(.*)$/);
  if (!m) return url;
  const file = m[2].split(/[?#]/)[0].split('/').pop();
  return file ? `${m[1]}/…/${file}` : m[1];
}

Page({
  data: {
    items: [], // { url, label }
    input: '',
    loading: true,
    saving: false,
    dirty: false,
    error: '',
  },

  async onLoad() {
    this.device = dev.saved();
    if (!this.device) return wx.navigateBack();
    try {
      const config = await dev.api.config(this.device.ip);
      this.setUrls(config.ics, false);
    } catch (e) {
      this.setData({ error: `${e.message}。请确认手机和设备连的是同一个 WiFi。` });
    }
    this.setData({ loading: false });
  },

  setUrls(urls, dirty = true) {
    this.setData({ items: urls.map((url) => ({ url, label: label(url) })), dirty });
  },

  onInput(e) {
    this.setData({ input: e.detail.value });
  },

  onAdd() {
    const url = this.data.input.trim();
    if (!url) return;
    if (!/^(https?|webcal):\/\//.test(url)) return this.setData({ error: '订阅地址要以 https:// 或 webcal:// 开头' });
    const urls = this.data.items.map((i) => i.url);
    if (urls.includes(url)) return this.setData({ error: '这个地址已经添加过了' });
    this.setUrls([...urls, url]);
    this.setData({ input: '', error: '' });
  },

  onRemove(e) {
    const urls = this.data.items.map((i) => i.url);
    urls.splice(e.currentTarget.dataset.index, 1);
    this.setUrls(urls);
  },

  async onSave() {
    if (this.data.saving || !this.data.dirty) return;
    this.setData({ saving: true, error: '' });
    try {
      await dev.api.saveCalendars(
        this.device.ip,
        this.data.items.map((i) => i.url),
      );
      this.setData({ dirty: false });
      wx.showToast({ title: '已保存', icon: 'success' });
      setTimeout(() => wx.navigateBack(), 1000);
    } catch (e) {
      this.setData({ error: e.message });
    }
    this.setData({ saving: false });
  },
});
