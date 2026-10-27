#include "SubHID.h"
#include <FastLED.h>
#include <EEPROM.h>
#include "HIDcmdHandler.h"
#include "KeyManager.h"
#include "KeyBehaviorEEPROM.h"   // 按键功能（连发参数）的 EEPROM 存读
#include "WS2812BManager.h"      // 灯效：灯带初始化 / 效果切换 / 每帧刷新

//初始化
void setup() {
  #if DEBUG
    Serial.begin(9600);
    while (!Serial) {
      ; // 等待串口连接，仅适用于 USB CDC 板子
    }
    Serial.println("Serial connected");
  #endif
  delay(1000);

  //灯光：初始化灯带 + 默认效果（按下即亮，和原来一致）
  //亮度 / 基础色 / 灯效都可以在运行期通过 wsSetBrightness / wsSetBaseColor / wsSetEffect 改
  wsInit();                   // 灯带硬件初始化（FastLED.addLeds + 清屏）
  ledParamsGetFromEEPROM();   // 读灯效参数；EEPROM 无效就写入默认值（按下即亮 / 红色）

  // Setup I/O for pins
  for(int i=0;i<BUTTONCOUNT;i++){
    pinMode(buttonPins[i],INPUT_PULLUP);
  }
  pinMode(ENC_L_A,INPUT_PULLUP);
  pinMode(ENC_L_B,INPUT_PULLUP);
  //把 EEPROM 里的按键映射和去抖时间载入到内存（上电只做这一次，改了要重新插拔才生效）
  debounceTimeGetFromEEPROM();
  keyCodeInit();
  //连发参数：紧挨着灯效参数区之后，上电读一次
  rapidParamsGetFromEEPROM();
}

void loop() {
  //每帧只在这里读一次 millis()，其它地方都用 getFrameTime()
  frameTimeUpdate();
  //扫描 + 去抖：结果写在 keys[] 里（每键一个结构体）
  updateKeyState();
  //清空报文后按映射表填报文（原来这里是用写死的键值直接填，现在改成走按键映射）
  reportBufferClear();
  //给osu的设置，stable中osu对手柄协议支持有问题，只有在y最大值处才不会向下滑动
  report.lyAxis = -128;
  keyOperation();
  keyOperation_stickyKey();
  //灯效：内部按 20ms 限速（50fps），每帧只 FastLED.show() 一次；按下状态直接读 keys[]
  wsTick();
  //处理上位机下发的指令（改映射 / 查询 / 设备信息），处理完回 ACK 和 FIN
  HIDcmdResponseIfChanged();
  HIDStateUpdate(2);
  delayMicroseconds(500);
}
