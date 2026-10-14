// 板载 RGB 灯（WS2818，GPIO48）状态指示：平时熄灭，只在这几种情况下亮
//   同步中（对时 / 节假日 / 日历）  蓝色慢闪
//   配网模式（等待手机配置）         橙色慢闪
//   联网成功（亮 RGB_LED_DONE_MS）   绿色常亮
// 同时有多个状态时按上面的顺序显示。
// 同步是阻塞的，所以灯由独立的后台任务驱动，主循环卡住时也能闪。
#pragma once

namespace led {

void begin();

void setSyncing(bool on);
void setProvisioning(bool on);
void flashConnected();

}  // namespace led
