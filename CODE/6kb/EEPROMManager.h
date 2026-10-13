#ifndef EEPROMManager_h
#define EEPROMManager_h
#include "Universal.h"
#include <EEPROM.h>

#define DEBOUNCE_EEPROM_POS 0
#define DEBOUNCE_MIN 1
#define DEBOUNCE_MAX 20
//映射区起始位置，紧跟在按键参数区之后
#define KEYMAP_START_POSITION 1

//按 (key, slot) 直接算出映射在 EEPROM 里的位置：
//每个按键占 KEYMAP_SIZE_PER_KEY 字节，按键之间依次紧挨；
//每个映射占 2 字节：(device, offset)
int keyMapSlotToEEPROMIndex(uint8_t key, uint8_t slot){
  return KEYMAP_START_POSITION + (key * KEYMAP_SIZE_PER_KEY) + (slot * 2);
}

//EEPROM 没写过的字节读出来是 0xFF（擦除态）；一个映射槽里的 device 只可能是
//KEYBOARD..MULTIMEDIA(1~4)。所以只要读到的值不在这段范围里 ——
//0（正常空槽）、0xFF（没写过）、或者旧格式的残留数据 —— 一律按"空槽"处理。
bool keyMapSlotIsEmpty(uint8_t device){
  return !(device >= KEYBOARD && device <= MULTIMEDIA);
}

//-------- 供 HIDcmdHandler 的查询功能调用，这里只负责从 EEPROM 取数据 --------
//读取某个按键第 slot 个映射：device 作为返回值（NO_DEVICE 表示空槽），offset 由指针带回
uint8_t keyMapGetFromEEPROM(uint8_t key, uint8_t slot, uint8_t *offset){
  int index = keyMapSlotToEEPROMIndex(key, slot);
  uint8_t device = 0;
  uint8_t off = 0;
  EEPROM.get(index, device);
  EEPROM.get(index + 1, off);
  if(keyMapSlotIsEmpty(device)){
    *offset = 0;
    return NO_DEVICE;   //0xFF / 残留数据都按空槽返回
  }
  *offset = off;
  return device;
}

//把某个按键从 slot 开始（含 slot）往后的所有槽的 device 都设成 NO_DEVICE，
//也就是把"这个按键从第 slot 条以后"的映射全部清掉。
//读映射时遇到空槽就会停，所以调用方只需要按顺序发它有的那几条映射，
//不用再逐条补发空槽（NO_DEVICE 就是 0，用宏写更清楚）。
//注意：只要传进来的 slot < MAXIMUM_BINDABLE_KEY 就不会越界；
//改 BUTTONCOUNT / MAXIMUM_BINDABLE_KEY 时请自行核对映射区总大小（见 Universal.h）
void keyMapClearFromEEPROM(uint8_t key, uint8_t slot){
  if(!(key < BUTTONCOUNT) || !(slot < MAXIMUM_BINDABLE_KEY)){
    return;
  }
  for(uint8_t s = slot; s < MAXIMUM_BINDABLE_KEY; s++){
    EEPROM.write(keyMapSlotToEEPROMIndex(key, s), NO_DEVICE);
  }
}

uint8_t _keyCodeSetToEEPROM(uint8_t key, uint8_t slot, uint8_t device, uint8_t offset) {
  //索引由 (key, slot) 直接算出，不再做 EEPROM 越界检查（见 Universal.h 的说明），
  //只保证 key / slot 合法，否则会写到别的按键的映射区里去
  if(!(key < BUTTONCOUNT) || !(slot < MAXIMUM_BINDABLE_KEY)){
    return 2;
  }
  int index = keyMapSlotToEEPROMIndex(key, slot);
  EEPROM.write(index, device);
  EEPROM.write(index + 1, offset);
  #if DEBUG
    sprintf(buff, "EEPROM操作:key = %d, slot = %d, device = %d, offset = %d", key, slot, device, offset);
    Serial.println(buff);
    sprintf(buff, "Real Index %d", index);
    Serial.println(buff);
  #endif
  return 0;
}

uint8_t keyCodeAddToEEPROM(uint8_t key, uint8_t device, uint8_t offset) {
  if(!(key < BUTTONCOUNT)){
    return 2;
  }
  //从该按键自己的映射区里找第一个空槽追加（0 和 0xFF 都算空槽）
  for(uint8_t slot = 0; slot < MAXIMUM_BINDABLE_KEY; slot++){
    if(keyMapSlotIsEmpty(EEPROM.read(keyMapSlotToEEPROMIndex(key, slot)))){
      _keyCodeSetToEEPROM(key, slot, device, offset);
      #if DEBUG
        sprintf(buff, "映射追加:key = %d, slot = %d", key, slot);
        Serial.println(buff);
      #endif
      return 0;
    }
  }
  #if DEBUG
    sprintf(buff, "映射追加失败:按键 %d 的 %d 个映射槽已满", key, MAXIMUM_BINDABLE_KEY);
    Serial.println(buff);
  #endif
  return 1;
}

uint8_t offsetToArrayIndex(uint8_t offset){
  uint8_t high_bits = offset / 8;
  uint8_t low_bits = offset % 8;
  #if DEBUG
    sprintf(buff, "检测到为键盘设备 high_bits = %d , low_bits = %d", high_bits, low_bits);
    Serial.println(buff);
  #endif
  return (high_bits << 4) | low_bits;
}
void keyMapAppend(uint8_t key, uint8_t device, uint8_t offset){
  if(key < BUTTONCOUNT){
    for(uint8_t i = 0; i < MAXIMUM_BINDABLE_KEY; i++){
      if(key_map[key][i][0] == NO_DEVICE){
        if(device == KEYBOARD){
          key_map[key][i][0] = device;
          key_map[key][i][1] = offsetToArrayIndex(offset);
          break;
        }else if(device == CONTROLLER){
          key_map[key][i][0] = device;
          key_map[key][i][1] = offset;
          break;
        }else if(device == MOUSE){
          key_map[key][i][0] = device;
          key_map[key][i][1] = offset;
          break;
        }else if(device == MULTIMEDIA){
          key_map[key][i][0] = device;
          key_map[key][i][1] = offset;
          break;
        }
      }
    }
  }else{
    
  }
  
}

void keyCodeGetFromEEPROM(uint8_t key){
  //一个按键一个按键地读：读到空槽（device 不是 1~4）说明本按键的映射到此为止，
  //或者已经读满 MAXIMUM_BINDABLE_KEY 个也停
  for(uint8_t slot = 0; slot < MAXIMUM_BINDABLE_KEY; slot++){
    int index = keyMapSlotToEEPROMIndex(key, slot);
    uint8_t device = 0;
    uint8_t offset = 0;
    EEPROM.get(index, device);
    EEPROM.get(index + 1, offset);
    if(keyMapSlotIsEmpty(device)){
      break;   //空槽 / 0xFF（没写过）/ 残留数据：本按键的映射到此为止
    }
    #if DEBUG
      sprintf(buff, "获得EEPROM的数据:key = %d, slot = %d, device = %d, offset = %d", key, slot, device, offset);
      Serial.println(buff);
    #endif
    keyMapAppend(key, device, offset);
  }
}
void keyCodeClear(){
  //清每个映射槽的 device
  //device为0后面对应的 offset 不会再被读到
  #if DEBUG
    Serial.print("清空映射\n");
  #endif
  for(int index = KEYMAP_START_POSITION; index < KEYMAP_START_POSITION + BUTTONCOUNT * KEYMAP_SIZE_PER_KEY; index += 2){
    EEPROM.write(index, NO_DEVICE);
  }
}
//读取抬起去抖时间：没写过（0 / 0xFF）或者超出范围就保持默认值
//（EEPROM 操作都放在这个文件里，别的地方只调用）
void debounceGetFromEEPROM(){
  uint8_t value = 0;
  EEPROM.get(DEBOUNCE_EEPROM_POS, value);
  if(value >= DEBOUNCE_MIN && value <= DEBOUNCE_MAX){
    release_debounce = value;
  }
}
void keyCodeInit() {
  memset(key_map, 0, sizeof(key_map));
  for(uint8_t key = 0; key < BUTTONCOUNT; key++){
    keyCodeGetFromEEPROM(key);
  }
}

//六键机械键盘没有磁轴参数，所以没有 keyParameterInit() / keyParameterSet() 这些函数
#if DEBUG
  void showBinding(uint8_t key, uint8_t slot){
    int index = keyMapSlotToEEPROMIndex(key, slot);
    uint8_t device = 0;
    uint8_t offset = 0;
    EEPROM.get(index, device);
    EEPROM.get(index + 1, offset);
      sprintf(buff, "读取EEPROM的数据:key = %d, slot = %d, device = %d, offset = %d", key, slot, device, offset);
      Serial.println(buff);
      sprintf(buff, "Real Index %d", index);
      Serial.println(buff);
  }
  void showAllBindings(){
    for(int key = 0; key < BUTTONCOUNT; key++){
      for(int slot = 0; slot < MAXIMUM_BINDABLE_KEY; slot++){
        showBinding(key, slot);
      }
    }
  }
#endif
#endif