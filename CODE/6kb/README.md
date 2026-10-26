# 六键键盘固件结构说明（6kb）

**中文** | [English](README.en.md)

本文档说明这套固件的**代码结构、数据流和关键约定**，用于排查问题和给后来人做参考。
代码里的中文注释是“为什么这么写”，本文档回答“整体是怎么运转的”。

- 目标芯片：ATmega32u4（Arduino Leonardo 内核，`arduino:avr:leonardo`）
- 按键数量：`BUTTONCOUNT = 6`，**普通机械轴**（数字输入，带上拉，低电平有效）
- 每个键一颗 WS2812B 灯（数据脚 D4，2 行 × 3 列）
- 固件版本：`FIRMWARE_VERSION`（`Universal.h`，当前 `0x46` = 1.06）
- 要求的上位机版本：`HOST_VERSION_REQUIRED`（`Universal.h`，当前 `0x47` = 1.07）
- 上位机协议：Report ID **4**，8 字节指令（与 KeyboardOG 上位机配套）

> 和磁轴版（`rtkb-layout-edited`）的关系：协议、映射格式、EEPROM 映射区布局一致；
> 区别是**没有模拟扫描 / RT / 校准**，多了**每键 RGB 灯**，去抖时间存在 EEPROM 里。

---

## 1. 文件与职责

| 文件 | 职责 | 改它通常因为 |
|---|---|---|
| `6kb.ino` | `setup()` / `loop()`：引脚、灯带、主循环调度 | 加新的每轮任务 |
| `Universal.h` | 全局配置：按键数、映射槽数、按键引脚表、编码器引脚、去抖默认值、固件版本、`report` | 改按键数、改引脚 |
| `KeyManager.h` | 按键扫描 + 去抖 `getKeyState()`、映射翻译 `keyOperation()` | 改去抖手感、改映射语义 |
| `WS2812BManager.h` | 灯效：参数结构体、EEPROM 存取、12 种灯效、50fps 帧调度 | 加灯效、调参数 |
| `EEPROMManager.h` | EEPROM：去抖时间（第 0 字节）+ 映射区 + 灯效参数区、启动载入 | 改存储格式 |
| `HIDcmdHandler.h` | 上位机指令解析、ACK/FIN、查询回传、设备信息帧 | 加指令、改回传 |
| `LayoutManager.h` | 配列表 + 逐键上传（`0xFE` 帧） | 换配列（用 `kle2c.py` 生成） |
| `HID.cpp` / `HID.h` / `SubHID.cpp` / `SubHID.h` | 复合 HID 描述符与报文发送 | 改报文结构 |

---

## 2. 启动顺序（`setup()`）

```c
delay(1000);                                   // 等 USB 枚举稳定
wsInit();                                      // 灯带初始化（FastLED.addLeds + 清屏）
ledParamsGetFromEEPROM();                      // 读灯效参数；无效就写默认值
for (i) pinMode(buttonPins[i], INPUT_PULLUP);  // 6 个按键：上拉输入，按下为 LOW
pinMode(ENC_L_A/B, INPUT_PULLUP);              // 编码器引脚（当前固件未实现解码，见 §9）
debounceGetFromEEPROM();                       // EEPROM[0] → release_debounce
keyCodeInit();                                 // EEPROM 映射区 → key_map[][][]
```

注意 `keyCodeInit()` 只在**上电**执行一次：上位机改完映射后会发 `CMD_REFRESH`，由指令处理再调一次。

---

## 3. 主循环（`loop()`）

```c
reportBufferClear();          // 清报文
report.lyAxis = -128;         // 手柄左摇杆归位（否则每轮清空后会变 0 = 摇杆偏上）
keyOperation(getKeyState());  // 扫描去抖 → 位图 → 填 report
wsTick(buttonbuf);            // 灯效：内部按 fps 限速，每帧只 FastLED.show() 一次
HIDcmdResponseIfChanged();    // 处理上位机指令（去重后回应）
HIDStateUpdate(2);            // 每 2 轮真正发一次 HID 报文
delayMicroseconds(500);       // 主循环节拍
```

- 灯用的是 **`buttonbuf`（物理按下状态）**，不是去抖后的 `key_trigger_state`，所以按下瞬间就亮；
- `report` 每轮清零重建，映射是“电平触发”，松手即消失；
- `lyAxis = -128` 是历史行为（原来手柄模式下摇杆中位），删掉会让系统认为左摇杆一直偏上；
- 灯效刷新率和主循环解耦：主循环约 500µs 一轮，`wsTick()` 内部按 `fps` 限速（默认 50fps）。

### 3.1 时间基准（`frame_time`）

`millis()` 每次调用都要短暂关中断、读 `TCNT0` 和溢出计数；同一帧里调多次还可能跨过计时器节拍，
导致同一帧内的判断用了不同基准。所以所有时间统一走 `Universal.h`：

```c
static uint32_t frame_time = 0;
static inline void frameTimeUpdate() { frame_time = millis(); }   // 每帧只在这里读一次
static inline uint32_t getFrameTime() { return frame_time; }      // 其它地方只读缓存
```

- `loop()` 的**第一条语句**就是 `frameTimeUpdate();`
- 按键去抖（`KeyManager.h`）和灯效帧调度（`WS2812BManager.h`）都改用 `getFrameTime()`
- 呼吸效果不再用 `beatsin8()`（FastLED 内部会调 `millis()`），改成自己累加相位的三角波
- 全项目 `millis()` 只出现在 `frameTimeUpdate()` 里

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

数组后面多出来的 `1, MISO, SCK` **没有用到**（留给最多 9 键的板子）；改按键数时按顺序取前 N 个即可，
但别把 `BUTTONCOUNT` 调到 7 以上而不检查这几个引脚是否真的接了按键。

### 4.2 状态变量

```c
volatile uint16_t buttonbuf = 1;      // 物理状态位图（初值是 1，见 §9 的坑）
static uint16_t key_trigger_state;    // 去抖后的状态位图（对外返回这个）
static unsigned long time_Pre[6];     // 每个键最后一次“检测到按下”的时刻
```

### 4.3 去抖算法

```c
读全部引脚 → buttonbuf 位（HIGH=松开清零，LOW=按下置位并记录 time_Pre）
for 每个键:
    如果 buttonbuf 位为 1        → key_trigger_state 置位          （按下立即生效）
    否则如果 now - time_Pre >= release_debounce
                                 → key_trigger_state 清零          （抬起要等够去抖时间）
```

- 按下：**立即**触发，不延迟；抬起：必须连续 `release_debounce` 毫秒都检测到抬起才确认；
- `release_debounce` 默认 1ms，范围 1~20，存在 EEPROM 第 0 字节；
- 位图是**锁存**的（`key_trigger_state` 是静态变量），不是每轮重算——这点和磁轴版一样重要。

---

## 5. 映射与 HID 报文（`KeyManager.h` / `SubHID.h`）

### 5.1 `key_map`

```c
uint8_t key_map[BUTTONCOUNT][MAXIMUM_BINDABLE_KEY][2];   // [按键][槽][device, offset]
```

- 每键最多 `MAXIMUM_BINDABLE_KEY = 12` 条映射，**按顺序**生效；
- 槽的 `device == NO_DEVICE(0)` 表示到此为止。

### 5.2 device / offset 语义

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
偏移 145      灯效参数区：LEDParams_t（10 字节，见 §8）
合计          155 字节（ATmega32u4 有 1024 字节，剩余约 869）
```

- `#define KEYMAP_START_POSITION 1`；灯效区偏移由宏计算，不要写死：
  `#define LED_PARAMS_START_POSITION (KEYMAP_START_POSITION + BUTTONCOUNT * KEYMAP_SIZE_PER_KEY)`；
- 空槽的 `device` 不是 1~4（0 / 0xFF / 旧数据）→ 一律当空槽；
- 去抖时间读出来不在 1~20 范围时保持默认值（防止 EEPROM 未初始化时读到 0xFF）。

---

## 7. 上位机协议（`HIDcmdHandler.h`）

### 7.1 指令

| 指令 | 值 | 说明 |
|---|---|---|
| `CMD_SET_MAP` | 0x01 | 写映射：`[1]=device [2]=按键 [3]=offset [4]=APPEND/OVERRIDE/CLEAR [5]=槽` |
| `CMD_SET_PARAM` | 0x02 | **未实现**（六键没有磁轴参数） |
| `CMD_CALIBRATE` | 0x03 | 未实现 |
| `CMD_QUERY` | 0x04 | `[1]=0 映射 / 0x03 灯效 / 0xFE 布局 / 0xFF 设备信息` |
| `CMD_ACK` / `CMD_FIN` | 0x05 / 0x06 | 回传 |
| `CMD_REFRESH` | 0x07 | `[1]=0` 重新载入映射 |
| `CMD_SET_LED` | 0x08 | 即时灯效：`[1]=effect [2]=亮度 [3]=速度 [4..6]=主色 RGB` |
| `CMD_SET_LED_ACCENT` | 0x09 | 即时副色：`[1..3]=副色 RGB` |
| `CMD_SAVE_LED` | 0x0A | 保存灯效：`[1]=0x5A` 确认字节 |
| `CMD_SET_LED_FPS` | 0x0B | 刷新率：`[1]=fps（10~200）` |

### 7.2 回传约定

- **ACK**：`{0x05, 指令码, 序号, 0...}`；
- **FIN**：把收到的 8 字节原样回传、只把 `[0]` 换成 `0x06`；
- 查询结果：`[0]=0x01`（键值）/ `[0]=0x0C`、`0x0D`（灯效）/ `[0]=0xFE`（布局）；
- **设备信息帧没有 FIN**，收到它就是结束标志：

  ```
  { 0xFF, BUTTONCOUNT, 霍尔标志, 编码器数, 设备类型, layoutKind,
    HOST_VERSION_REQUIRED, FIRMWARE_VERSION }
  ```

  - `[2]=0x0F` 表示非磁轴；
  - `[5]=0xFF` 表示会上传布局；
  - **`[6]` = 这套固件要求的上位机版本**（2|3|3 位，当前 `0x47` = 1.07）；
  - `[7]` = 固件版本。
- 固件会丢弃与上一条完全相同的指令，所以上位机每条指令的 `[7]` 序号必须自增。

### 7.3 布局上传

`LayoutManager.h` 里的 `layout_custom[]` 是 PROGMEM 常量表，每项 `{x, y, w, h, flags}`（0.25u）。
收到 `QUERY_LAYOUT(0xFE)` 时逐键发 `{0xFE, index, x, y, w, h, flags, 序号}`，最后 FIN。
表用 `tools/kle2c.py` 从 keyboard-layout-editor 生成，长度由 `static_assert` 对齐 `BUTTONCOUNT`。

---

## 8. 灯光（`WS2812BManager.h`）

灯带接 **D4**，6 颗 WS2812B，按键排布 **2 行 × 3 列**：

```
0 1 2
3 4 5
```

### 8.1 调用方式

```c
// setup()
wsInit();                    // FastLED.addLeds + 清屏 + 关闭 dither
ledParamsGetFromEEPROM();    // 读灯效参数；无效就用默认值并写入 EEPROM

// loop()，放在 keyOperation(getKeyState()) 之后
wsTick(buttonbuf);           // 内部按 ledParams.fps 限速，每帧只 show 一次
```

### 8.2 参数结构体 `LEDParams_t`（10 字节，EEPROM 里就是这 10 个字节）

| 字段 | 类型 | 含义 |
|---|---|---|
| `magic` | uint8 | `0x5B`，判断 EEPROM 里有没有有效数据（改结构体布局时换一个值） |
| `effect` | uint8 | 灯效编号 0~22 |
| `brightness` | uint8 | 亮度 0~255 |
| `speed` | uint8 | 动画速度 1~20（1 最慢，20 最快） |
| `fps` | uint8 | 刷新率 10~200（默认 50） |
| `color1[3]` | uint8 | 主色 RGB |
| `color2[3]` | uint8 | 副色 RGB |

全局只有一份 `ledParams`；修改走 `wsSetEffect / wsSetBrightness / wsSetSpeed /
wsSetFps / wsSetColor1RGB / wsSetColor2RGB`（一次只改一个字段）。

**两个不同的常量，别混用**：

| 常量 | 值 | 用途 |
|---|---|---|
| `LED_PARAMS_MAGIC` | `0x5B` | 存进 EEPROM 的 struct 首字节，判断数据是否有效 |
| `LED_SAVE_CONFIRM` | `0x5A` | `CMD_SAVE_LED` 的 `[1]` 确认字节 |

### 8.3 EEPROM 存取

```c
// 读：整块读出来校验，再 memcpy 到全局结构体
EEPROM.get(LED_PARAMS_START_POSITION, temp);
if (ledParamsAreValid(&temp)) memcpy(&ledParams, &temp, sizeof(LEDParams_t));
else { ledParamsSetDefault(); ledParamsSaveToEEPROM(); }

// 写：整块写回（保存前补 magic）
ledParams.magic = LED_PARAMS_MAGIC;
EEPROM.put(LED_PARAMS_START_POSITION, ledParams);
```

### 8.4 灯效（三组）

**单色组（只用主色）**

| 编号 | 宏 | 效果 |
|---|---|---|
| 0 | `LED_EFFECT_OFF` | 全灭 |
| 1 | `LED_EFFECT_SOLID` | 常亮 |
| 2 | `LED_EFFECT_PRESS` | 按下即亮（默认） |
| 3 | `LED_EFFECT_PRESS_FADE` | 按下 + 松开余辉 |
| 4 | `LED_EFFECT_TYPING_HEAT` | 打字热度（主色由暗到亮，按住持续升温） |
| 5 | `LED_EFFECT_BREATH` | 呼吸 |
| 6 | `LED_EFFECT_WAVE` | 波浪 |
| 7 | `LED_EFFECT_SPARKLE` | 星光 |
| 8 | `LED_EFFECT_RIPPLE` | 涟漪 |
| 9 | `LED_EFFECT_COMET_MONO` | 跑马灯 |

**炫彩组（HSV 彩虹，不使用颜色参数）**

| 编号 | 宏 | 效果 |
|---|---|---|
| 10 | `LED_EFFECT_RAINBOW` | 彩虹流动 |
| 11 | `LED_EFFECT_RAINBOW_BREATH` | 彩虹呼吸 |
| 12 | `LED_EFFECT_RAINBOW_WAVE` | 彩虹波浪 |
| 13 | `LED_EFFECT_RAINBOW_SPARKLE` | 彩虹星光 |
| 14 | `LED_EFFECT_COMET_RAINBOW` | 彩虹跑马 |

**双色组（主色 ↔ 副色）**

| 编号 | 宏 | 效果 |
|---|---|---|
| 15 | `LED_EFFECT_DUAL_BREATH` | 双色呼吸（三角波相位驱动，0~255 全范围） |
| 16 | `LED_EFFECT_DUAL_PRESS` | 按下 = 副色，松开 = 主色 |
| 17 | `LED_EFFECT_DUAL_PRESS_FADE` | 按下 = 副色；松开后**该键**的余量衰减回 0，背景恒为主色 |
| 18 | `LED_EFFECT_DUAL_WAVE` | 双色波浪（列错相 85、行错相 30） |
| 19 | `LED_EFFECT_DUAL_COMET` | 双色跑马（头副色 → 尾两格过渡 → 末端纯主色微光） |
| 20 | `LED_EFFECT_DUAL_TYPING` | 双色打字热度：冷副色 → 热主色，**按住持续升温**（与“按下+渐回”不同） |
| 21 | `LED_EFFECT_DUAL_GRADIENT` | 左右渐变（0 / 127 / 255 三段取满） |
| 22 | `LED_EFFECT_DUAL_RIPPLE` | 双色涟漪（延迟与衰减都跟随 speed） |

双色组的实现约定（避免闪烁/突变）：

- **背景颜色只由 `color1` 决定**：按下类效果用每键独立的 `wsGlow[]` 记录“副色余量”；
- **不用帧缓冲回溯**：颜色都由 `color1/color2` 和状态量直接算，不会“从中间色再混一次”；
- **呼吸/波浪用自己累加的相位**，端点连续；
- **涟漪**：单次触发，延迟 `80-2×speed` ms/格，衰减 `1800-50×speed` ms；
  双色涟漪不会把按住的键刷成主色（否则松手瞬间会从主色跳到副色）。
- **彗星（跑马）**：头的位置按毫秒连续推进，亮度按“0/1/2/3 格”的距离曲线插值，
  所以不会整格跳动；旧写法是每帧清空重画 + 整格移动，头会在格与格之间"闪现"。
- **两种热度**都用**每帧小步**累加（约 0.3s 到满），不是按一下直接跳到满值；
  单色热度用主色的明暗，不再用 `HeatColor()`（那个是黑→红→黄→白，和设置的颜色无关）。

涟漪、彩虹、波浪、渐变都用 2×3 的物理排布（`wsKeyCol / wsKeyRow / wsKeyDistance`），
扩散方向和色相按真实位置走。

### 8.5 灯效指令与查询

| 指令 | 值 | 字节布局 |
|---|---|---|
| `CMD_SET_LED` | 0x08 | `[1]=effect [2]=亮度 [3]=速度 [4..6]=主色 RGB [7]=序号` |
| `CMD_SET_LED_ACCENT` | 0x09 | `[1..3]=副色 RGB [7]=序号` |
| `CMD_SAVE_LED` | 0x0A | `[1]=0x5A`（`LED_SAVE_CONFIRM`）`[7]=序号` |
| `CMD_SET_LED_FPS` | 0x0B | `[1]=fps（10~200）[7]=序号` |

08 / 09 / 0B **立即生效但不保存**，0A 才把当前 `ledParams` 写进 EEPROM。

**查询灯效**：`CMD_QUERY` 的 `[1] = QUERY_LED (0x03)`，固件回两帧后回 FIN：

| 帧 | 值 | 字节布局 |
|---|---|---|
| 基本参数 | `0x0C` | `[1]=effect [2]=亮度 [3]=速度 [4]=fps` |
| 颜色 | `0x0D` | `[1..3]=主色 RGB [4..6]=副色 RGB` |

两帧的 `[7]` 都是查询指令的序号；10 个参数字节一帧装不下（报文只有 8 字节），所以拆成两帧。

### 8.6 本地模拟器（`tools/led_sim.py`）

改灯效前建议先用它跑一遍，不用烧录：

```bash
python tools/led_sim.py --effect 18 --frames 60 --fps 50 --speed 8 \
                        --color1 FE0107 --color2 00E6FF --out out
python tools/led_sim.py --list          # 列出效果编号
```

输出：

| 文件 | 内容 |
|---|---|
| `out/frames.png` | 胶片图（每行一帧、6 颗灯），肉眼看渐变 |
| `out/timeline.csv` | 每帧每颗灯的 RGB，方便贴进表格分析 |
| `out/report.txt` | 连续性统计：相邻帧最大通道变化、跳变位置与前后颜色 |

判定规则：`<= 40` 算平滑；涟漪 / 彗星这类有“波前”的效果允许波前扫过时的一帧跳变；
其余效果报 `JUMP` 就是真的有跳色。模拟器复刻的是固件里的整数数学
（`wsMix8` / `wsScale` / 时间步进 / 各效果更新公式），改公式时要同步改它。

### 8.7 性能与时序

- 每帧只 `FastLED.show()` 一次（6 颗灯约 200µs），关掉了 FastLED 的时域抖动；
- 全部整数运算：`sin8` / `HeatColor` / `scale8` / `blend` 都是查表或汇编实现，没有浮点；
- 唯一的查找表是跑马灯拖尾的 3 字节 PROGMEM 表（`wsCometTrail`）；
- 动画**按时间**推进，不按帧：

  ```c
  wsDt = 本次帧间隔（限幅 8~60ms）
  步进 = (1 + speed/2) * wsDt / 20      // speed 只决定“每秒走多远”
  ```

  所以调刷新率只改变平滑度，不改变动画快慢；低于 ~16fps 时动画会变慢但不会跳帧。
  
- **亮度缩放要用 `scale8()`，不要用 `scale8_video()`**：后者最低留 1，永远关不灭
  （这就是之前“余辉/涟漪关不掉”的原因）；也不要拿 `CRGB::nscale8_video(x)` 当亮度系数，
  它的参数是**减暗量**。

---

## 9. 已知的坑与注意点

1. **`buttonbuf` 初值是 1**
   ```c
   volatile uint16_t buttonbuf = 1;   // bit0 = 1
   ```
   上电第一次扫描之前，按键 0 看起来是“按下的”，会亮一下灯、也可能触发一次映射。

2. **`buttonPins[]` 有 9 项，只用前 6 项**
   后面三项是 `1, MISO, SCK`；其中 D1 是串口 TX，别在 `BUTTONCOUNT > 6` 时直接用。

3. **编码器还没实现**
   `Universal.h` 定义了 `ENC_L_A/B`（D2/D3）和 `TT_ACTIVE_COUNT`、`ENCODER_PULSE_TIMES`，
   但 `loop` 里没有解码调用——旋钮目前**不工作**。要启用得把磁轴版的 `EncoderManager.h` 移植过来。

4. **`LED_PARAMS_MAGIC`（0x5B）和 `LED_SAVE_CONFIRM`（0x5A）不是一回事**
   曾经把保存指令的确认字节写成 `LED_PARAMS_MAGIC`，结果上位机发 0x5A 永远对不上，
   保存被静默忽略。两者含义不同，必须分开。

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
| 灯完全不亮 | `wsInit()` 是否调用、D4 接线、`ledParams.brightness` 是否为 0 |
| 余辉/涟漪关不灭 | 亮度缩放是不是用了 `scale8_video`（最低留 1），应改 `scale8` |
| 灯效保存后拔插丢失 | `CMD_SAVE_LED` 的 `[1]` 是否等于 `LED_SAVE_CONFIRM`（0x5A） |
| 上位机连上就报 3000ms 超时 | 设备信息帧没有 FIN，上位机要按“收到 0xFF 帧”结束 |
| 布局显示不出来 | 设备信息 `[5]` 是否为 `0xFF`、布局表长度是否 6 |
| 改完映射要重新插拔 | `keyCodeInit()` 只在 setup 和 `CMD_REFRESH` 时执行 |

---

## 11. 一页速查

```
setup: 灯带(wsInit→ledParamsGetFromEEPROM) → 引脚(上拉) → debounceGetFromEEPROM → keyCodeInit
loop : 清报文+lyAxis → getKeyState(扫描去抖) → keyOperation → wsTick(灯效) → 指令 → 发送 → 500us

按键: digitalRead(上拉) → buttonbuf（物理）→ key_trigger_state（去抖锁存）
      → keyOperation() 查 key_map → report → HIDStateUpdate(2) 每两轮发一次

EEPROM: [0] 去抖时间（1~20）  [1..144] 映射  [145..154] 灯效参数（LEDParams_t）
指令  : Report ID 4，8 字节；ACK=0x05、FIN=0x06、查询结果 0x01/0x0C/0x0D/0xFE、设备信息 0xFF
```
