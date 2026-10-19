#include "SubHID.h"
#include <FastLED.h>
#include <EEPROM.h>
#include "HIDcmdHandler.h"
#include "KeyManager.h"

CRGB leds[BUTTONCOUNT];
CRGB color_of_each[BUTTONCOUNT];
uint8_t mode = 0;
static uint8_t hue=0;
//初始化
void setup() {
  #if DEBUG
    Serial.begin(9600);
    while (!Serial) {
      ;
    }
    Serial.println("Serial connected");
  #endif
  delay(1000);
  FastLED.addLeds<WS2812,4,GRB>(leds, sizeof(leds));
  FastLED.setBrightness(128);

  for(int color_init = 0; color_init < BUTTONCOUNT; color_init++){
    color_of_each[color_init] = CRGB::Red;
  }
  // Setup I/O for pins
  for(int i=0;i<BUTTONCOUNT;i++){
    pinMode(buttonPins[i],INPUT_PULLUP);
  }
  pinMode(ENC_L_A,INPUT_PULLUP);
  pinMode(ENC_L_B,INPUT_PULLUP);
  //把 EEPROM 里的按键映射和去抖时间载入到内存（上电只做这一次，改了要重新插拔才生效）
  debounceGetFromEEPROM();
  keyCodeInit();
  #if DEBUG
    Serial.begin(9600);
    while (!Serial) {
      ; // 等待串口连接，仅适用于 USB CDC 板子
    }
    Serial.println("Serial connected");
  #endif
}

void loop() {
  ledbuf++;
  if(ledbuf>=7){  
    FastLED.show();
    ledbuf=0;
  }
  //清空报文后按映射表填报文（原来这里是用写死的键值直接填，现在改成走按键映射）
  reportBufferClear();
  //lyAxis 原来是 use_KeyboardMouse_style() 里设一次就不动的，
  //现在每轮都会清报文，所以这里补回来，保证和原来的报文内容一致
  report.lyAxis = -128;
  keyOperation(getKeyState());
  //灯光部分保持原样：按物理按键的按下状态点亮
  for(int led_write = 0; led_write < BUTTONCOUNT; led_write++){
    if((buttonbuf >> led_write) & 1){
      leds[led_write] = color_of_each[led_write];
    }else{
      leds[led_write].r = 0;
      leds[led_write].g = 0;
      leds[led_write].b = 0;
    }
  }
  //处理上位机下发的指令（改映射 / 查询 / 设备信息），处理完回 ACK 和 FIN
  HIDcmdResponseIfChanged();
  HIDStateUpdate(2);
  delayMicroseconds(500);
}
