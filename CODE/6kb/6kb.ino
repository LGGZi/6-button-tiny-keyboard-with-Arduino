#include "SubHID.h"
#include <FastLED.h>
#include <EEPROM.h>
#include "HIDcmdHandler.h"
#include "KeyManager.h"
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
  wsInit();
  wsSetBaseColor(CRGB::Red);
  wsSetBrightness(128);
  wsSetEffect(LED_EFFECT_PRESS);

  // Setup I/O for pins
  for(int i=0;i<BUTTONCOUNT;i++){
    pinMode(buttonPins[i],INPUT_PULLUP);
  }
  pinMode(ENC_L_A,INPUT_PULLUP);
  pinMode(ENC_L_B,INPUT_PULLUP);
  //把 EEPROM 里的按键映射和去抖时间载入到内存（上电只做这一次，改了要重新插拔才生效）
  debounceGetFromEEPROM();
  keyCodeInit();
}

void loop() {
  //清空报文后按映射表填报文（原来这里是用写死的键值直接填，现在改成走按键映射）
  reportBufferClear();
  //lyAxis 原来是 use_KeyboardMouse_style() 里设一次就不动的，
  //现在每轮都会清报文，所以这里补回来，保证和原来的报文内容一致
  report.lyAxis = -128;
  keyOperation(getKeyState());
  //灯效：内部按 20ms 限速（50fps），每帧只 FastLED.show() 一次
  wsTick();
  //处理上位机下发的指令（改映射 / 查询 / 设备信息），处理完回 ACK 和 FIN
  HIDcmdResponseIfChanged();
  HIDStateUpdate(2);
  delayMicroseconds(500);
}
