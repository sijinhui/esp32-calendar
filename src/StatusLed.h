// 板载 RGB 灯（WS2818，GPIO48）状态指示：平时熄灭，只在这几种情况下亮
//   同步中（对时 / 节假日 / 日历）  蓝
//   配网模式（等待手机配置）         橙
//   联网成功（亮 RGB_LED_DONE_MS）   绿
// 同时有多个状态时按上面的顺序显示。
#pragma once

namespace led {

void begin();
void loop();  // 放在主循环里，负责“联网成功”到时熄灭

void setSyncing(bool on);
void setProvisioning(bool on);
void flashConnected();

}  // namespace led
