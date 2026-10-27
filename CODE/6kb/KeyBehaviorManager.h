#ifndef KeyBehaviorManager_H
#define KeyBehaviorManager_H
#include "Universal.h"

//连发参数存到 EEPROM：具体实现放在 KeyBehaviorEEPROM.h 里（按键功能的存储单独一个文件）。
//这里前置声明是因为包含顺序上它在本文件之后才被 include，
//而 rapidTabBehavior_toggle() 需要调用它。
void rapidParamsSaveToEEPROM();

#define DEVICE 0
#define KEYOFFSET 1

static uint8_t sticky_key_toggle = 1;
static uint8_t sk_trigger_state[BUTTONCOUNT] = {0};
void stickyKeyInit(){
  for(uint8_t i = 0; i < BUTTONCOUNT; i++){
    sk_trigger_state[i] = 0xFF;
    //遍历按键的槽，找出有绑定粘滞键的
    for (uint8_t key_map_addr = 0; key_map_addr < MAX_BINDINGS_PER_KEY; key_map_addr++){
      uint8_t behavior = (key_map[i][key_map_addr][DEVICE] & 0xF0) >> 4;
      if(behavior == STICKY){
        //0xFF标记为没有绑定的，执行置位的时候会略过此按键,0为绑定粘滞键的抬起状态
        sk_trigger_state[i] = 0;
        continue;
      }
    }
  }
}

//置入对应位
void keyCodeWrite(uint8_t device, uint8_t offset) {
  switch (device) {
    case KEYBOARD:
      {
        uint8_t high_bits = (offset >> 4) & 0x0F;
        uint8_t low_bits = offset & 0x0F;
        if (high_bits < 14) {
          report.keyboardKeys[high_bits] |= ((uint8_t)1 << low_bits);
        }
        break;
      }
    case CONTROLLER:
      {
        report.xboxButtons |= (uint8_t)1 << offset;
        break;
      }
    case MOUSE:
      {
        report.mouseButtons |= (uint8_t)1 << offset;
        break;
      }
    case MULTIMEDIA:
      {
        report.mediaButtons |= (uint8_t)1 << offset;
        break;
      }
    default:
      break;  //损坏 / 未知的 device：跳过这一个槽继续处理后面的，
  }
}

void stickyKeyBehavior_toggle(){
  if(sticky_key_toggle == 0){
    sticky_key_toggle = 1;
  }else{
    sticky_key_toggle = 0;
  }
}

void stickyKeyBehavior(){
  if(sticky_key_toggle == 1){
    //根据状态写入对应位
    for(int i = 0;i < BUTTONCOUNT; i++){
      if(sk_trigger_state[i] == 1){
        for (uint8_t key_map_addr = 0; key_map_addr < MAX_BINDINGS_PER_KEY; key_map_addr++) {
          uint8_t behavior = (key_map[i][key_map_addr][DEVICE] & 0xF0) >> 4;
          if(behavior == STICKY){
            uint8_t device = key_map[i][key_map_addr][DEVICE] & 0x0F;
            uint8_t offset = key_map[i][key_map_addr][KEYOFFSET];
            keyCodeWrite(device, offset);
          }
        }
      }
    }
  }
}

//按键下降沿调用的函数，切换对应key的sk_trigger_state状态
void stickyKeyBehavior_stateSwitch(uint8_t key){
  if(sk_trigger_state[key] == 0){
    sk_trigger_state[key] = 1;
  }else if(sk_trigger_state[key] == 1){
    sk_trigger_state[key] = 0;
  }
}
void rapidTabBehavior_toggle(){
  if(rk_args.toggle == 0){
    rk_args.toggle = 1;
  }else{
    rk_args.toggle = 0;
  }
  //开关状态跟着结构体一起存，掉电后保持（一次按键一次写入，EEPROM 寿命足够）
  rapidParamsSaveToEEPROM();
}
void rapidTabBehavior(uint8_t key, uint8_t device, uint8_t offset){
  static uint32_t pre_time[BUTTONCOUNT] = {0};
  if (keyDownEdge(key)) {
    pre_time[key] = 0;   //刚刚按下的时候（按键下降沿）重置计时，立即发送按键
  }
  if(rk_args.toggle == 1){
    if(getFrameTime() - pre_time[key] > rk_args.interval){
      keyCodeWrite(device, offset);
    }
    if(getFrameTime() - pre_time[key] > rk_args.interval + rk_args.pulse){
      pre_time[key] = getFrameTime();
    }
  }

}

#endif
