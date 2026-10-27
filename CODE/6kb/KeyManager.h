#ifndef KeyManager_h
#define KeyManager_h

#include "Universal.h"
#include "KeyBehaviorManager.h"


//report 这个全局变量在 Universal.h 里声明（和磁轴键盘不同，那边是在这里声明的）
static unsigned long time_Now = 0;   // 本帧时间戳（调试用；逻辑判断请直接读 keys[]）

void reportBufferClear(){
  memset(&report, 0, sizeof(report));
}


void keyOperation_stickyKey(){
  stickyKeyBehavior();
}
//按键映射的执行部分：把"哪几个物理键被按下了"翻译成 HID 报文内容
void keyOperation(){
  for(uint8_t i = 0; i < BUTTONCOUNT; i++){
    if(keyDown(i)){
      //遍历此按键的槽，直到遇到DEVICE为0
      for (uint8_t key_map_addr = 0; key_map_addr < MAX_BINDINGS_PER_KEY; key_map_addr++) {
        uint8_t device = key_map[i][key_map_addr][DEVICE] & 0x0F;
        uint8_t behavior = (key_map[i][key_map_addr][DEVICE] & 0xF0) >> 4;
        uint8_t offset = key_map[i][key_map_addr][KEYOFFSET];
        if (key_map[i][key_map_addr][DEVICE] == NO_DEVICE) {
          break;  //空槽：本按键的映射到此为止
        }
        if(behavior == 0) {
          //behavior为0则为常规按键
          keyCodeWrite(device, offset);
          continue;
        }else{
          //否则，有特殊功能，
          switch (behavior){
            case STICKY:
            {
              if(device == TOGGLE){
                if(keyDownEdge(i)){
                  stickyKeyBehavior_toggle();
                }
              }
              if(device == 0x0E){
                if(keyDownEdge(i)){
                  stickyKeyBehavior_stateSwitch(i);
                }
              }
            break;
            }
            case RAPID_TAB:
            {
              if(device == TOGGLE){
                if(keyDownEdge(i)){
                  rapidTabBehavior_toggle();
                }
              }else{
                rapidTabBehavior(i, device, offset);
              }
              break;
            }
          }
        }
      }
    }
  }
}

/*
  扫描 + 去抖：结果写进 keys[]（每键一个结构体）。
    · 按下立即生效（raw=1 当帧就 down=1）
    · 抬起要连续 release_debounce 毫秒都检测到抬起才确认
  keys[i].pressedAt 记录最近一次按下的时间，抬起去抖/长按/连发都用它。
*/
void updateKeyState(){
  static uint8_t pre_button_state[BUTTONCOUNT] = {0};
  time_Now = getFrameTime();
  for(uint8_t i = 0; i < BUTTONCOUNT; i++){
    uint8_t pressed = (digitalRead(buttonPins[i]) == LOW) ? 1 : 0;
    keys[i].raw = pressed;

    if(pressed){
      keys[i].pressedAt = time_Now;
      keys[i].down = 1;                       //按下立即生效
      if(pre_button_state[i] == 0){ keys[i].down_edge = 1; }else{keys[i].down_edge = 0;}
    } else if((uint32_t)(time_Now - keys[i].pressedAt) >= release_debounce){
      keys[i].down = 0;                       //抬起要等够去抖时间
      keys[i].down_edge = 0;
    }
    pre_button_state[i] = keys[i].down;
  }
}

#endif
