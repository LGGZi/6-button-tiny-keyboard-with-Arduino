#ifndef Universal_h
#define Universal_h
//在这里开启或关闭串口DEBUG
#define DEBUG 0
#define FIRMWARE_VERSION 0x44
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
#define MAXIMUM_BINDABLE_KEY 12
//每个按键在 EEPROM 里占用的映射空间：每个映射 2 字节(device, offset) * MAXIMUM_BINDABLE_KEY
//整个映射区 = BUTTONCOUNT * KEYMAP_SIZE_PER_KEY 字节，紧接在按键参数区
//(BUTTONCOUNT * PSOOSK) 之后，各按键的区域依次紧挨着排列。
//注意：这里不做 EEPROM 越界检查 —— 改按键数 / 最大映射数 / 换芯片时请自行核算总占用
//（ATmega32u4 = 1024 字节，例：参数区 6*12 = 72，映射区 6*12*2 = 144，共 216）
#define KEYMAP_SIZE_PER_KEY (MAXIMUM_BINDABLE_KEY * 2)

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
static uint8_t key_map[BUTTONCOUNT][MAXIMUM_BINDABLE_KEY][2] = {0};
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