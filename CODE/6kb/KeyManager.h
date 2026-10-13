#ifndef KeyManager_h
#define KeyManager_h

#include "Universal.h"
#include "SubHID.h"
#define HIDStateUpdate(...) _HIDStateUpdate((1, ##__VA_ARGS__))
#define DEVICE 0
#define KEYOFFSET 1
//report 这个全局变量在 Universal.h 里声明（和磁轴键盘不同，那边是在这里声明的）
static unsigned long time_Now = 0;
static unsigned long time_Pre[BUTTONCOUNT] = {0};
static uint16_t key_trigger_state = 0;
volatile uint16_t buttonbuf = 1;

void reportBufferClear(){
  memset(&report, 0, sizeof(report));
}
//按键映射的执行部分：把"哪几个物理键被按下了"翻译成 HID 报文内容
//keyTriggerState 每一位对应一个物理按键（bit0 = 按键0），由各输入方式自己扫描得到
void keyOperation(uint16_t keyTriggerState){
  for(int i = 0; i < BUTTONCOUNT; i++){
    if((keyTriggerState >> i) & 1){
      for(uint8_t key_map_addr = 0; key_map_addr < MAXIMUM_BINDABLE_KEY; key_map_addr++){
        uint8_t device = key_map[i][key_map_addr][DEVICE];
        if(device == NO_DEVICE){
          break;   //空槽：本按键的映射到此为止
        }
        #if DEBUG
          Serial.print("输出手柄");
          Serial.print("  offset=");
          Serial.println(key_map[i][key_map_addr][KEYOFFSET]);
        #endif
        switch(device){
          case KEYBOARD:{
            uint8_t high_bits = (key_map[i][key_map_addr][KEYOFFSET] >> 4) & 0x0F;
            uint8_t low_bits = key_map[i][key_map_addr][KEYOFFSET] & 0x0F;
            if(high_bits < 14){
              report.keyboardKeys[high_bits] |= ((uint8_t)1 << low_bits);
            }
            break;
          }
          case CONTROLLER:{
            report.xboxButtons |= (uint8_t)1 << key_map[i][key_map_addr][KEYOFFSET];
            break;
          }
          case MOUSE:{
            report.mouseButtons |= (uint8_t)1 << key_map[i][key_map_addr][KEYOFFSET];
            break;
          }
          case MULTIMEDIA:{
            report.mediaButtons |= (uint8_t)1 << key_map[i][key_map_addr][KEYOFFSET];
            break;
          }
          default:
            break;   //损坏 / 未知的 device：跳过这一个槽继续处理后面的，
          
        }
      }
    }
  }
}

uint16_t getKeyState(){
  //按键扫描：返回"哪几个键被按下了"的位图（bit0 = 按键0）
  //去抖原理为按下时立刻算按下，抬起则要连续 release_debounce 毫秒都检测到抬起才确认抬起
  //（release_debounce 是单独的变量，默认 1ms，和原来的行为一致）
  time_Now = millis();
  for(int i=0;i<BUTTONCOUNT;i++){
    if(digitalRead(buttonPins[i])==HIGH){
      buttonbuf &= ~((uint16_t)1 << i);

    }else{
      buttonbuf |= (uint16_t)1 << i;
      time_Pre[i] = millis();
    }
  }
  for(int i=0;i<BUTTONCOUNT;i++){
    if((buttonbuf >> i) & 1){
      key_trigger_state |= (uint16_t)1 << i;
    } else if(time_Now - time_Pre[i] >= release_debounce){
      key_trigger_state &= ~((uint16_t)1 << i);
    }
  }
  return key_trigger_state;
}



#endif
