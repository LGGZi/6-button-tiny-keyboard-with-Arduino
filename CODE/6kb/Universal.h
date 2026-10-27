#ifndef Universal_h
#define Universal_h
//在这里开启或关闭串口DEBUG
#define DEBUG 0
#define HOST_VERSION_REQUIRED 0x47   // 设备信息 [6]：这套固件要求的上位机版本（2|3|3 位）
#define FIRMWARE_VERSION 0x49   // 1.11（2|3|3：01 001 001 = 0x49）新增连发参数查询/写入

/*-----------------------------------------------------------------------------
  统一时间基准
  ---------------------------------------------------------------------------
  millis() 不是"读一个变量"：每次调用都要短暂关中断、读 TCNT0 和溢出计数，
  再算成毫秒；同一帧里调用多次还可能跨过计时器节拍，导致同一帧内的时序判断
  用了不同基准。

  这里的做法是：loop() 开头调用一次 frameTimeUpdate() 把时间抓进 frame_time，
  其它所有代码只通过 getFrameTime() 读这个缓存值。
  这样每帧只读一次硬件计时器，帧内的所有判断都基于同一个时间戳。
-----------------------------------------------------------------------------*/
static uint32_t frame_time = 0;

static inline void frameTimeUpdate() {
  frame_time = millis();
}

static inline uint32_t getFrameTime() {
  return frame_time;
}
//编码器灵敏度vv
//键鼠模式灵敏度vv 值为起转脉冲数
#define TT_ACTIVE_COUNT 1
//手柄模式灵敏度vv 值为编码器脉冲数与转动一圈后上报的位置的位移数值的比值
#define ENCODER_PULSE_TIMES 600
//The bigger the more sensitive

// encoder sensitivity = number of positions per rotation (600) / number of positions for HID report (256)
/*
 * connect encoders
 * TURNTABLE to pin 2 and 3
 */
//回报速率
#define REPORT_DELAY 1000
//抬起去抖默认时间(ms)：按键抬起后要连续这么久都检测到抬起才认为真的抬起
//实际用的是 release_debounce，可以改（打算存在 EEPROM 第一个字节里）
#define DEBOUNCE_DEFAULT 1
// Number of microseconds between HID reports
// 2000 = 500hz
//编码器AB相对应引脚
#define ENC_L_A 2
#define ENC_L_B 3
//实现的按键数（作为数组脚标最大值的声名，需严格按照数组大小设置）
#define BUTTONCOUNT 6
//每个按键最多能绑多少条映射
#define MAX_BINDINGS_PER_KEY 12
//每个按键在 EEPROM 里占用的映射空间：每个映射 2 字节(device, offset) * MAX_BINDINGS_PER_KEY
//整个映射区 = BUTTONCOUNT * KEYMAP_SIZE_PER_KEY 字节，紧接在按键参数区
//(BUTTONCOUNT * PSOOSK) 之后，各按键的区域依次紧挨着排列。
//注意：这里不做 EEPROM 越界检查 —— 改按键数 / 最大映射数 / 换芯片时请自行核算总占用
//（ATmega32u4 = 1024 字节，例：参数区 6*12 = 72，映射区 6*12*2 = 144，共 216）
#define KEYMAP_SIZE_PER_KEY (MAX_BINDINGS_PER_KEY * 2)

//映射里的设备类型：就是指令的 Byte2，也是 EEPROM 里存的 device，和磁轴键盘保持一致
#define NO_DEVICE 0x00
#define KEYBOARD 0x01
#define CONTROLLER 0x02
#define MOUSE 0x03
#define MULTIMEDIA 0x04
//下面这些轴 / 扳机类型固件还没实现（keyOperation 里走 default 跳过），上位机也不提供
#define CONTROLLER_XAXIS_LEFT 0x05
#define CONTROLLER_YAXIS_LEFT 0x06
#define CONTROLLER_XAXIS_RIGHT 0x07
#define CONTROLLER_YAXIS_RIGHT 0x08
#define MOUSE_X 0x09
#define MOUSE_Y 0x0A
#define CONTROLLER_LEFT_TRIGGER 0x0B
#define CONTROLLER_RIGHT_TRIGGER 0x0C

#define TOGGLE 0x0F
#define STICKY_KEY_SWITCH 0x0E

#define STICKY 0x01
#define RAPID_TAB 0x02
/*——————————全局变量声名——————————*/
volatile HIDConReport_t report;
volatile uint8_t buttonPins[] = {8,6,12,5,10,9,1,MISO,SCK};

volatile uint16_t ledbuf = 0;
//抬起去抖时间(ms)：单独一个变量，方便以后做成可调的值（见 EEPROMManager.h）
volatile uint8_t release_debounce = DEBOUNCE_DEFAULT;
static uint8_t step = 1;
//sprintf 用的公用缓冲（DEBUG 打印）
#if DEBUG
char buff[80];
#endif
//按键映射表：key_map[按键][第几个映射槽][0=device / 1=offset]
//device == NO_DEVICE 表示这个槽是空的，后面也不用看了
static uint8_t key_map[BUTTONCOUNT][MAX_BINDINGS_PER_KEY][2] = {0};

/*---------------------- 按键运行时状态 ----------------------
  以前是 uint16_t 位图 + 几个并行数组（time_Pre[]、key_trigger_state）：
  最多只能表示 16 个键，而且每加一个功能就要再加一个同长度数组。
  现在统一放进“每键一个结构体”，数组下标就是按键编号，不再有位宽限制：

    raw       本次扫描的原始电平（1 = 按下）
    down      去抖后的按下状态（对外唯一真相，keyDown() 读它）
    pressedAt 最近一次检测到按下的 frame_time（抬起去抖 / 长按 / 连发都用它）

  对外只读用 keyDown(i) / keyRaw(i)，不要直接改 keys[i].down。
--------------------------------------------------------------*/
struct KeyRuntime {
  uint8_t  raw;
  uint8_t  down;
  uint8_t  down_edge;
  uint32_t pressedAt;
};

static KeyRuntime keys[BUTTONCOUNT];

static inline bool keyDown(uint8_t i) { return keys[i].down != 0; }
static inline bool keyRaw(uint8_t i)  { return keys[i].raw != 0; }
static inline bool keyDownEdge(uint8_t i)  { return keys[i].down_edge != 0; }

/*---------------------- 连发参数 ----------------------
  整块结构体直接存进 EEPROM（位置见 EEPROMManager.h 的
  RAPID_PARAMS_START_POSITION，紧挨着灯效参数区之后）。

  magic      数据有效性标记（EEPROM 没写过是 0xFF，读出来对不上就回默认）
  toggle     连发功能总开关：1 = 开，0 = 关（默认开）
  pulse      每次连发按下持续多少 ms（太窄主机会漏读，见 KeyBehaviorManager.h）
  interval   连发间隔 ms
  variation  预留：间隔抖动（还没用）

  注意：AVR 上按 1 字节对齐，sizeof(RapidKeyArgs_t) = 7（magic/toggle/pulse/interval/variation），
  位置宏用 sizeof 算，别写死字节数。
--------------------------------------------------------*/
#define RAPID_PARAMS_MAGIC     0x6B   // EEPROM 里的结构体首字节，判断数据有效
#define RAPID_SAVE_CONFIRM     0x6C   // CMD_SAVE_RAPID 的 [1] 确认字节（别和 magic 混用）
#define RAPID_TOGGLE_DEFAULT   1
#define RAPID_PULSE_DEFAULT    15
#define RAPID_PULSE_MIN        1
#define RAPID_PULSE_MAX        200
#define RAPID_INTERVAL_DEFAULT 100
#define RAPID_INTERVAL_MIN     10
#define RAPID_INTERVAL_MAX     5000
#define RAPID_VARIATION_DEFAULT 10
#define RAPID_VARIATION_MIN     0
#define RAPID_VARIATION_MAX     200

//连发参数在“参数查询/回传”里用的参数码（和磁轴参数共用一套帧格式）
#define RAPID_PARAM_TOGGLE      0x10
#define RAPID_PARAM_PULSE       0x11
#define RAPID_PARAM_INTERVAL    0x12
#define RAPID_PARAM_VARIATION   0x13

struct RapidKeyArgs_t {
  uint8_t  magic;
  uint8_t  toggle;
  uint8_t  pulse;
  uint16_t interval;
  uint16_t variation;
};

struct RapidKeyArgs_t rk_args = {
  .magic     = RAPID_PARAMS_MAGIC,
  .toggle    = RAPID_TOGGLE_DEFAULT,
  .pulse     = RAPID_PULSE_DEFAULT,
  .interval  = RAPID_INTERVAL_DEFAULT,
  .variation = RAPID_VARIATION_DEFAULT,
};
/*——————————通用函数——————————*/
#define HIDStateUpdate(...) _HIDStateUpdate((1, ##__VA_ARGS__))
void _HIDStateUpdate(uint8_t LoopCount){
  if(step < LoopCount){
    step++;
  }else{
    HIDCon.setState(&report);
    HIDCon.setConsumerState(report.mediaButtons);
    step = 1;
  }
}
#endif