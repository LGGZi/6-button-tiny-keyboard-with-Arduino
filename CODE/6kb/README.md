# 六键键盘固件结构说明（6kb）

本文档说明这套固件的**代码结构、数据流和关键约定**，用于排查问题和给后来人做参考。
代码里的中文注释是“为什么这么写”，本文档回答“整体是怎么运转的”。

- 目标芯片：ATmega32u4（Arduino Leonardo 内核，`arduino:avr:leonardo`）
- 按键数量：`BUTTONCOUNT = 6`，**普通机械轴**（数字输入，带上拉，低电平有效）
- 每个键一颗 WS2812 灯
- 固件版本：`FIRMWARE_VERSION`（`Universal.h`，当前 `0x44` = 1.04）
- 上位机协议：Report ID **4**，8 字节指令（与 KeyboardOG 上位机配套）

> 和磁轴版（`rtkb-layout-edited`）的关系：协议、映射格式、EEPROM 映射区布局完全一致；
> 区别是**没有模拟扫描/RT/校准**，多了**每键 RGB 灯**，去抖时间存在 EEPROM 里。

---

## 1. 文件与职责

| 文件 | 职责 | 改它通常因为 |
|---|---|---|
| `6kb.ino` | `setup()` / `loop()`：引脚、灯带、主循环调度 | 加新的每轮任务、改灯效 |
| `Universal.h` | 全局配置：按键数、映射槽数、按键引脚表、编码器引脚、去抖默认值、固件版本、`report` | 改按键数、改引脚 |
| `KeyManager.h` | 按键扫描 + 去抖 `getKeyState()`、映射翻译 `keyOperation()` | 改去抖手感、改映射语义 |
| `EEPROMManager.h` | EEPROM：去抖时间（第 0 字节）+ 映射区、启动载入 | 改存储格式 |
| `HIDcmdHandler.h` | 上位机指令解析、ACK/FIN、查询回传、设备信息帧 | 加指令、改回传 |
| `LayoutManager.h` | 配列表 + 逐键上传（`0xFE` 帧） | 换配列（用 `kle2c.py` 生成） |
| `HID.cpp` / `HID.h` / `SubHID.cpp` / `SubHID.h` | 复合 HID 描述符与报文发送 | 改报文结构 |

---

## 2. 启动顺序（`setup()`）

```c
delay(1000);                                   // 等 USB 枚举稳定
FastLED.addLeds<WS2812, 4, GRB>(leds, 6);      // 灯带接 D4
for (i) pinMode(buttonPins[i], INPUT_PULLUP);  // 6 个按键：上拉输入，按下为 LOW
pinMode(ENC_L_A/B, INPUT_PULLUP);              // 编码器引脚（当前固件未实现解码，见 §9）
debounceGetFromEEPROM();                       // EEPROM[0] → release_debounce
keyCodeInit();                                 // EEPROM 映射区 → key_map[][][]
```

注意 `keyCodeInit()` 只在**上电**执行一次：上位机改完映射后会发 `CMD_REFRESH`，由指令处理再调一次。

---

## 3. 主循环（`loop()`）

```c
ledbuf++;  if (ledbuf >= 7) { FastLED.show(); ledbuf = 0; }   // 约每 7 轮刷一次灯
reportBufferClear();          // 清报文
report.lyAxis = -128;         // 手柄左摇杆归位（否则每轮清空后会变 0 = 摇杆偏上）
keyOperation(getKeyState());  // 扫描去抖 → 位图 → 填 report
for (i) leds[i] = 按下 ? color_of_each[i] : 全黑;   // 灯跟随“物理按下”状态
HIDcmdResponseIfChanged();    // 处理上位机指令（去重后回应）
HIDStateUpdate(2);            // 每 2 轮真正发一次 HID 报文
delayMicroseconds(500);       // 主循环节拍
```

- 灯用的是 **`buttonbuf`（物理按下状态）**，不是去抖后的 `key_trigger_state`，所以按下瞬间就亮；
- `report` 每轮清零重建，映射是“电平触发”，松手即消失；
- `lyAxis = -128` 是历史行为（原来手柄模式下摇杆中位），删掉会让系统认为左摇杆一直偏上。

---

## 4. 按键扫描与去抖（`KeyManager.h`）

### 4.1 硬件

```c
volatile uint8_t buttonPins[] = {8,6,12,5,10,9, 1,MISO,SCK};   // 只用前 6 个
```

| 按键编号 | 引脚 |
|---|---|
| 0 | D8 |
| 1 | D6 |
| 2 | D12 |
| 3 | D5 |
| 4 | D10 |
| 5 | D9 |

数组后面多出来的 `1, MISO, SCK` **没有用到**（留着给最多 9 键的板子）；改按键数时按顺序取前 N 个即可，
但别把 `BUTTONCOUNT` 调到 7 以上而不检查这几个引脚是否真的接了按键。

### 4.2 状态变量

```c
volatile uint16_t buttonbuf = 1;      // 物理状态位图（这里初值是 1，见 §9 的坑）
static uint16_t key_trigger_state;    // 去抖后的状态位图（对外返回这个）
static unsigned long time_Pre[6];     // 每个键最后一次“检测到按下”的时刻
```

### 4.3 去抖算法

```c
读全部引脚 → buttonbuf 位（HIGH=松开清零，LOW=按下置位并记录 time_Pre）
for 每个键:
    如果 buttonbuf 位为 1        → key_trigger_state 置位            （按下立即生效）
    否则如果 now - time_Pre >= release_debounce
                                → key_trigger_state 清零            （抬起要等够去抖时间）
```

- 按下：**立即**触发，不延迟；
- 抬起：必须连续 `release_debounce` 毫秒都检测到抬起才确认；
- `release_debounce` 默认 1ms，取值范围 1~20，存在 EEPROM 第 0 字节；
- 位图是**锁存**的（`key_trigger_state` 是全局/静态），不是每轮重算——这点和磁轴版一样重要。

---

## 5. 映射与 HID 报文（`KeyManager.h` / `SubHID.h`）

### 5.1 `key_map`

```c
uint8_t key_map[BUTTONCOUNT][MAXIMUM_BINDABLE_KEY][2];   // [按键][槽][device, offset]
```

- 每键最多 `MAXIMUM_BINDABLE_KEY = 12` 条映射，**按顺序**生效；
- 槽的 `device == NO_DEVICE(0)` 表示到此为止。

### 5.2 device / offset 语义（与磁轴版一致）

| device | 含义 | offset 的解释 |
|---|---|---|
| 1 `KEYBOARD` | 键盘 | 高 4 位 = `keyboardKeys[]` 下标，低 4 位 = 该字节里的 bit |
| 2 `CONTROLLER` | 手柄 | 按钮 bit（`xboxButtons`） |
| 3 `MOUSE` | 鼠标 | 按钮 bit（`mouseButtons`） |
| 4 `MULTIMEDIA` | 多媒体 | bit0 静音 / bit1 音量- / bit2 音量+ |

键盘用“下标 + 位”的压缩方式：`keyboardKeys[]` 共 14 字节，前 13 字节普通键、最后一字节修饰键。
`offset = 0x04` → 第 0 字节 bit4；修饰键是 `13*16 + bit`（`208 + bit`）。

### 5.3 报文

`HIDConReport_t`（`SubHID.h`）是复合设备报文结构，`keyOperation()` 只往上“或”位，
发送在 `SubHID.cpp`（Report ID 1/2/3 分别对应手柄/键盘/鼠标，多媒体另发一条）。

---

## 6. EEPROM 布局（`EEPROMManager.h`）

```
偏移 0        去抖时间（1 字节，1~20，默认 1）
偏移 1        映射区：BUTTONCOUNT * KEYMAP_SIZE_PER_KEY = 6 * 24 = 144 字节
              每键 12 槽 × (device, offset) 2 字节，按键之间依次紧挨
合计          145 字节（ATmega32u4 有 1024 字节，剩余约 879）
```

- `#define KEYMAP_START_POSITION 1`（磁轴版是 `BUTTONCOUNT * PSOOSK`）；
- 空槽的 `device` 不是 1~4（0 / 0xFF / 旧数据）→ 一律当空槽；
- 去抖时间读出来不在 1~20 范围时保持默认值（防止 EEPROM 未初始化时读到 0xFF）。

---

## 7. 上位机协议（`HIDcmdHandler.h`）

### 7.1 指令

| 指令 | 值 | 说明 |
|---|---|---|
| `CMD_SET_MAP` | 0x01 | 写映射：`[1]=device [2]=按键 [3]=offset [4]=APPEND/OVERRIDE/CLEAR [5]=槽` |
| `CMD_SET_PARAM` | 0x02 | **未实现**（六键没有磁轴参数；去抖时间暂不开放给上位机） |
| `CMD_CALIBRATE` | 0x03 | 未实现 |
| `CMD_QUERY` | 0x04 | `[1]=0 映射 / 0xFE 布局 / 0xFF 设备信息` |
| `CMD_ACK` / `CMD_FIN` | 0x05 / 0x06 | 回传 |
| `CMD_REFRESH` | 0x07 | `[1]=0` 重新载入映射 |

### 7.2 回传约定

- **ACK**：`{0x05, 指令码, 序号, 0...}`；
- **FIN**：把收到的 8 字节原样回传、只把 `[0]` 换成 `0x06`；
- 查询结果：`[0]=0x01`（键值）/ `[0]=0xFE`（布局）；
- **设备信息帧没有 FIN**：`{0xFF, 6, 0x0F, 编码器数, 设备类型, layoutKind, 保留, 固件版本}`
  （`[2]=0x0F` 表示非磁轴；`[5]=0xFF` 表示会上传布局）；
- 固件会丢弃与上一条完全相同的指令，所以上位机每条指令的 `[7]` 序号必须自增。

### 7.3 布局上传

`LayoutManager.h` 里的 `layout_custom[]` 是 PROGMEM 常量表，每项 `{x, y, w, h, flags}`（0.25u）。
收到 `QUERY_LAYOUT(0xFE)` 时逐键发 `{0xFE, index, x, y, w, h, flags, 序号}`，最后 FIN。
表用 `tools/kle2c.py` 从 keyboard-layout-editor 生成，长度由 `static_assert` 对齐 `BUTTONCOUNT`。

---

## 8. 灯光（WS2812）

- 灯带接 **D4**，6 颗，`FastLED.addLeds<WS2812, 4, GRB>`；
- 每颗灯的颜色在 `color_of_each[]` 里（默认全红），要改灯效改这里；
- `loop` 里每 7 轮 `FastLED.show()` 一次，避免刷新太频繁拖慢扫描；
- 灯由 **`buttonbuf`（物理按下）** 驱动：按下亮、松开灭。

---

## 9. 已知的坑与注意点

1. **`buttonbuf` 初值是 1**
   ```c
   volatile uint16_t buttonbuf = 1;   // bit0 = 1
   ```
   上电第一次扫描之前，按键 0 看起来是“按下的”，会亮一下灯、也可能触发一次映射。
   改成 `= 0` 更干净（但要确认没人依赖这个初值）。

2. **`buttonPins[]` 有 9 项，只用前 6 项**
   后面三项是 `1, MISO, SCK`；其中 D1 是串口 TX，千万别在 `BUTTONCOUNT > 6` 时直接用。

3. **编码器还没实现**
   `Universal.h` 定义了 `ENC_L_A/B`（D2/D3）和 `TT_ACTIVE_COUNT`、`ENCODER_PULSE_TIMES`，
   但 `loop` 里没有解码/上报调用——即旋钮目前**不工作**。要启用得把磁轴版的 `EncoderManager.h` 移植过来。

4. **没有 `CMD_SET_PARAM` / 校准**
   六键没有磁轴参数，去抖时间只能改 EEPROM 第 0 字节；上位机发 `SET_PARAM` 会落到 `default` 分支回一个 FIN。

5. **`lyAxis = -128` 不能删**
   每轮 `reportBufferClear()` 会把手柄摇杆清零（= 偏上），所以要手动补回中位。

6. **映射是“电平触发”**
   `report` 每轮重建，按住不放才会持续上报；松开即消失，不需要额外的“释放”指令。

---

## 10. 常见现象 → 可能原因

| 现象 | 先看哪里 |
|---|---|
| 某个键一直按下 | 该键引脚被短路到地 / `release_debounce` 太大 / 引脚表写错 |
| 按下没反应 | `key_map` 是否为空（EEPROM 没写过）、引脚是否 `INPUT_PULLUP` |
| 松手后还粘一下 | `release_debounce` 偏大（默认 1ms，可 1~20） |
| 灯不亮 | `FastLED.addLeds` 的引脚/灯数、`color_of_each` 是否全黑 |
| 上位机连上就报 3000ms 超时 | 设备信息帧没有 FIN，上位机要按“收到 0xFF 帧”结束 |
| 布局显示不出来 | 设备信息 `[5]` 是否为 `0xFF`、布局表长度是否 6 |
| 改完映射要重新插拔 | `keyCodeInit()` 只在 setup 和 `CMD_REFRESH` 时执行 |

---

## 11. 一页速查

```
setup: 引脚(上拉) → 灯带 → debounceGetFromEEPROM → keyCodeInit
loop : 灯刷新 → 清报文+lyAxis → getKeyState(扫描去抖) → keyOperation → 灯跟随 → 指令 → 发送 → 500us

按键: digitalRead(上拉) → buttonbuf（物理）→ key_trigger_state（去抖锁存）
      → keyOperation() 查 key_map → report → HIDStateUpdate(2) 每两轮发一次

EEPROM: [0] 去抖时间（1~20）  [1..144] 映射（6 键 × 12 槽 × 2 字节）
指令  : Report ID 4，8 字节；ACK=0x05、FIN=0x06、查询结果 0x01/0xFE、设备信息 0xFF
```
