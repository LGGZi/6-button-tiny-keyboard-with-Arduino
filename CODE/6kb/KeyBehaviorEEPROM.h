#ifndef KeyBehaviorEEPROM_h
#define KeyBehaviorEEPROM_h
#include "Universal.h"
#include "EEPROMManager.h"   // 用到 EEPROM / LED_PARAMS_START_POSITION / LEDParams_t

/*
  特殊按键功能（粘滞键 / 连发）在 EEPROM 里的存取。
  ---------------------------------------------------------------------------
  单独放一个文件，是为了让"按键功能"的存储和映射 / 灯效 / 去抖那些存储分开：
  以后加新的 behavior 参数（比如主动调速的步长）只改这里。

  布局（紧跟在灯效参数区之后）：

      偏移 LED_PARAMS_START_POSITION + sizeof(LEDParams_t) = 155
      整块就是一个 RapidKeyArgs_t（AVR 上 1 字节对齐，7 字节）：
        magic / toggle / pulse / interval / variation

  和灯效参数一样：magic 对不上（EEPROM 没写过是 0xFF）或者数值越界就回默认值，
  然后立刻存回去，保证下次上电读到的是有效数据。
*/

//连发参数区：紧跟在灯效参数区之后，整块就是一个 RapidKeyArgs_t
//（sizeof 由编译器布局决定，位置宏用 sizeof，不写死字节数）
#define RAPID_PARAMS_START_POSITION (LED_PARAMS_START_POSITION + sizeof(LEDParams_t))

//EEPROM 总占用自检：改按键数 / 映射数 / 参数区大小时如果超了，这里直接编译报错
static_assert(RAPID_PARAMS_START_POSITION + sizeof(struct RapidKeyArgs_t) <= E2END + 1,
              "EEPROM 不够用了：检查 BUTTONCOUNT / MAX_BINDINGS_PER_KEY / 各参数区大小");
//结构体布局就是 EEPROM 里实际存的字节：AVR 上按 1 字节对齐，这里是 7 字节（无填充）
//改字段记得同步：上位机读取偏移 + README 的布局表
static_assert(sizeof(struct RapidKeyArgs_t) == 7,
              "RapidKeyArgs_t 布局变了（magic/toggle/pulse/interval/variation，AVR 上 7 字节）");

//-------- 连发参数：整块结构体直接存 / 读 --------
bool rapidParamsAreValid(const struct RapidKeyArgs_t *p){
  return p->magic == RAPID_PARAMS_MAGIC
      && p->toggle <= 1
      && p->pulse >= RAPID_PULSE_MIN
      && p->pulse <= RAPID_PULSE_MAX
      && p->interval >= RAPID_INTERVAL_MIN
      && p->interval <= RAPID_INTERVAL_MAX
      && p->variation >= RAPID_VARIATION_MIN
      && p->variation <= RAPID_VARIATION_MAX;
}

void rapidParamsSetDefault(){
  rk_args.magic     = RAPID_PARAMS_MAGIC;
  rk_args.toggle    = RAPID_TOGGLE_DEFAULT;
  rk_args.pulse     = RAPID_PULSE_DEFAULT;
  rk_args.interval  = RAPID_INTERVAL_DEFAULT;
  rk_args.variation = RAPID_VARIATION_DEFAULT;
}

//按上位机发来的值改内存里的参数。
void rapidParamsApply(uint8_t toggle, uint8_t pulse, uint16_t interval, uint16_t variation){
  (void)toggle;
  rk_args.pulse     = (uint8_t)(pulse < RAPID_PULSE_MIN ? RAPID_PULSE_MIN
                              : (pulse > RAPID_PULSE_MAX ? RAPID_PULSE_MAX : pulse));
  rk_args.interval  = (uint16_t)(interval < RAPID_INTERVAL_MIN ? RAPID_INTERVAL_MIN
                              : (interval > RAPID_INTERVAL_MAX ? RAPID_INTERVAL_MAX : interval));
  rk_args.variation = (uint16_t)(variation < RAPID_VARIATION_MIN ? RAPID_VARIATION_MIN
                              : (variation > RAPID_VARIATION_MAX ? RAPID_VARIATION_MAX : variation));
}

//把全局 rk_args 整块写进 EEPROM（调用前先把 magic 补上）
void rapidParamsSaveToEEPROM(){
  rk_args.magic = RAPID_PARAMS_MAGIC;
  rk_args.toggle = 1;
  EEPROM.put(RAPID_PARAMS_START_POSITION, rk_args);
}

//上电读一次：有效就 memcpy 到全局结构体，无效（0xFF / 旧数据 / 越界值）就写默认值并保存
void rapidParamsGetFromEEPROM(){
  struct RapidKeyArgs_t temp;
  EEPROM.get(RAPID_PARAMS_START_POSITION, temp);
  if(rapidParamsAreValid(&temp)){
    memcpy(&rk_args, &temp, sizeof(rk_args));
  }else{
    rapidParamsSetDefault();
    rapidParamsSaveToEEPROM();
  }
}

#endif
