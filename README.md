# 6kb — 六键矮轴键盘（Arduino Leonardo）

**中文** | [English](README.en.md)

[![Platform](https://img.shields.io/badge/platform-Arduino%20Leonardo%20%2F%20ATmega32u4-00979D?logo=arduino&logoColor=white)](#硬件)
[![Protocol](https://img.shields.io/badge/HID-Report%20ID%204%20%C2%B7%208%20bytes-6f42c1)](#协议速查)
[![Firmware](https://img.shields.io/badge/firmware-1.05-blue)](#固件版本校验)
[![Host](https://img.shields.io/badge/host-KeyboardOG%20%E2%89%A5%201.07-2ea44f)](https://github.com/LGGZi/KeyBoard-OG-key-binding-tool)

一把单手就能盖住的小键盘，最初是给 osu! 这类需要短键程、少按键的场景做的。
主控是 Arduino Leonardo（ATmega32u4），每个键带一颗 WS2812B，所有配置都通过
[KeyboardOG](https://github.com/LGGZi/KeyBoard-OG-key-binding-tool) 上位机改，
不需要装驱动、不需要串口，也不占用键盘的输入功能：

```
上位机 ──(HID Report ID 4，8 字节指令)──> 键盘固件
        <─(ACK / FIN / 查询结果 / 设备信息)─
```

> **默认没有绑定任何键值**：第一次使用请先用上位机写一遍映射。

---

## 功能特性

### 按键扫描与去抖

- 6 个矮轴机械键，2 行 × 3 列（`0 1 2` / `3 4 5`），数字输入带上拉，按下为低电平。
- **按下立即生效**，抬起则要连续 `release_debounce` 毫秒都检测到松开才确认；
  去抖时间存在 EEPROM 第 0 字节，范围 1-20 ms（默认 1）。
- 状态位图是**锁存**的：只有跳变那一轮才改位，按键停在中间不会丢状态。

### 灯效（每键一颗 WS2812B）

- 数据脚 **D4**，12 种灯效分三组，全部整数运算、50fps 以上可调：
  - **单色**：全灭 / 常亮 / 按下即亮 / 按下+余辉 / 打字热度 / 呼吸 / 波浪 / 星光 / 涟漪 / 跑马灯
  - **炫彩**：彩虹流动 / 彩虹呼吸 / 彩虹波浪 / 彩虹星光 / 彩虹跑马
  - **双色**：双色呼吸 / 双色按下 / 双色按下+渐回 / 双色波浪 / 双色跑马 / 双色打字 / 左右渐变 / 双色涟漪
- 可调：**亮度 0-255、速度 1-20、刷新率 10-200 fps、主色 / 副色**；参数整块存在 EEPROM，掉电不丢。
- 涟漪 / 彩虹 / 波浪 / 渐变都按 **2×3 的真实排布**计算方向，不是按编号线性走。
- 灯效参数可以在上位机里实时调，点「保存灯效」才写进 EEPROM。

### 按键映射

- 每个键最多 **12 条映射**（与固件 `MAXIMUM_BINDABLE_KEY` 对应），按顺序生效。
- 四种设备可混用：**键盘 / 鼠标 / 手柄 / 多媒体**；修饰键自动换算到 HID 修饰键字节。
- 映射是“电平触发”：按住持续上报，松开即消失。

### 配列上传

- 固件把每个键的 `x / y / w / h`（0.25u 单位）上报给上位机，上位机按真实配列画图、
  点图选键（`QUERY_LAYOUT = 0xFE`）。
- 配列表由 `kle2c.py` 从 keyboard-layout-editor 生成，长度用 `static_assert` 对齐 `BUTTONCOUNT`。

### 固件版本校验

- 设备信息帧（`0xFF`）里带两个版本，都按 **2 | 3 | 3** 位编码：

  | 字节 | 含义 |
  |---|---|
  | `[6]` | 这套固件**要求的上位机版本**（当前 `0x47` = 1.07） |
  | `[7]` | 固件版本（当前 `0x45` = 1.05） |

- 上位机连上后立即比对：固件要求高于自身版本时，状态栏和日志提示升级，版本标签变红；
  固件侧同时维护“软件版本 ↔ 支持的固件范围”表，判断“固件过老 / 过新”。

---

## 快速开始

### 1. 烧录固件

Arduino IDE 打开 `CODE/6kb/6kb.ino`，板子选 **Arduino Leonardo**，上传。

### 2. 连接上位机

1. 打开 [KeyboardOG](https://github.com/LGGZi/KeyBoard-OG-key-binding-tool)，点「刷新设备」，
   在设备下拉框里选你的键盘（只有真正回应设备信息的设备才会出现）。
2. 点「连接」：自动读取设备信息、全部映射、灯效参数。

### 3. 写映射 / 调灯效

1. 左侧点按键编号 → 右侧页签点想要的键（左键替换、右键追加，再点一次删除）→ 点「写入当前按键映射」。
2. 切到「灯效」页签：选分组与效果、拖亮度 / 速度 / 刷新率、选主色副色，改动**立即生效**；
   满意后点「保存灯效」写进 EEPROM。

---

## 硬件

| 项目 | 说明 |
|---|---|
| 主控 | Arduino Leonardo / ATmega32u4（`arduino:avr:leonardo`，28KB Flash / 2.5KB RAM） |
| 按键 | 6 个矮轴，内部上拉，按下为低电平 |
| 按键引脚 | 键0=D8、键1=D6、键2=D12、键3=D5、键4=D10、键5=D9 |
| 灯带 | 6 × WS2812B，数据脚 **D4** |
| 编码器 | 引脚已在 `Universal.h` 预留（D2/D3），当前固件**未实现**解码 |
| USB | 复合设备：键盘 / 鼠标 / 手柄 / 多媒体（Report ID 1/2/3，指令走 Report ID 4） |

`HardwareAndShell/` 里有打板与外壳资料：

```
HardwareAndShell/
  6kbGerber.zip                Gerber（打板用）
  BOM_6kb_2026-10-13.xlsx      物料清单
  矮轴小键盘_2026-10-13.epro2   EasyEDA 工程
  3DShell_PCB1.zip             3D 外壳
```

---

## 从源码构建

依赖库：**FastLED**（灯效）、**EEPROM**（Arduino 自带）。

Arduino IDE：打开 `CODE/6kb/6kb.ino`，板子选 **Arduino Leonardo**，直接上传。

命令行（arduino-cli）：

```bash
arduino-cli compile --fqbn arduino:avr:leonardo CODE/6kb
arduino-cli upload  --fqbn arduino:avr:leonardo -p COM5 CODE/6kb
```

编译占用（当前版本）：Flash 约 16.2KB / 28KB，RAM 约 640B / 2.5KB。

---

## 目录结构

```
6-button-tiny-keyboard-with-Arduino/
├─ README.md / README.en.md      本文件（中文 / English）
├─ CODE/6kb/
│  ├─ 6kb.ino                    setup / loop
│  ├─ Universal.h                全局配置（按键数、引脚、版本号、report）
│  ├─ KeyManager.h               按键扫描、去抖、映射翻译
│  ├─ WS2812BManager.h           灯效（参数结构体、EEPROM、12 种效果、帧调度）
│  ├─ EEPROMManager.h            EEPROM 布局与读写
│  ├─ HIDcmdHandler.h            上位机指令 / 查询 / 设备信息
│  ├─ LayoutManager.h            配列表 + 逐键上传
│  └─ README.md / README.en.md   固件结构说明（代码级文档）
└─ HardwareAndShell/             Gerber / BOM / 外壳 / EasyEDA
```

---

## 协议速查

指令与回传都走 **Report ID 4**，数据 8 字节；`[0]` 是指令码，`[7]` 是上位机自增序号
（固件会丢弃与上一条完全相同的指令）。

| 指令 | 值 | 说明 |
|---|---|---|
| 写映射 | 0x01 | `[1]=device [2]=键 [3]=offset [4]=APPEND/OVERRIDE/CLEAR [5]=槽` |
| 查询 | 0x04 | `[1]=0 映射 / 0x03 灯效 / 0xFE 配列 / 0xFF 设备信息` |
| ACK / FIN | 0x05 / 0x06 | 回传 |
| 刷新 | 0x07 | 让固件从 EEPROM 重新载入 |
| 设灯效 | 0x08 | `[1]=效果 [2]=亮度 [3]=速度 [4..6]=主色` |
| 设副色 | 0x09 | `[1..3]=副色` |
| 保存灯效 | 0x0A | `[1]=0x5A` 确认 |
| 刷新率 | 0x0B | `[1]=fps（10~200）` |

指令约定：

1. **一条指令回两帧**：先 ACK 再 FIN；只读一帧会把 FIN 留给下一条。
2. **设备信息帧没有 FIN**：查询要用“收到 `0xFF` 帧”作为结束条件、而设备信息不用。
3. **固件丢弃重复指令**：所以每条指令的 `[7]` 必须自增。

完整字节布局、回传格式、EEPROM 映射见
[`CODE/6kb/README.md`](CODE/6kb/README.md)（[English](CODE/6kb/README.en.md)）。

---

## FAQ

**Q：烧录后按键没反应**

- 默认没有绑定任何键，需要先用上位机写映射。
- 确认板子选的是 **Arduino Leonardo**（不是 Micro / Pro Micro），上位机通过检查VID/PID来筛选设备。

**Q：上位机连上就报 3000 ms 超时**

- 设备信息帧没有 FIN，上位机要以“收到 `0xFF` 帧”结束；用配套版本的上位机即可。

**Q：灯效调完，拔插后又变回默认**

- 要点「保存灯效」（`CMD_SAVE_LED`，确认字节 `0x5A`）才会写进 EEPROM。
- 旧固件（灯效结构体从 9 字节变 10 字节之前）会因为确认字节不对而静默忽略保存，烧新固件即可。

**Q：某颗灯亮度不均匀 / 低亮度下闪烁**

- 这是 WS2812B 本身的低亮度表现；固件已经关闭 FastLED 的时域抖动，并统一用 `scale8` 缩放。

**Q：能改成 9 键吗**

- 自己修改代码，修改引脚定义，改变BUTTONCOUNT等宏定义然后自己将getKeyState()修改（扫描按键的函数，去抖也应在内，返回二进制按键按下状态），这样就可以适配不同按键数的键盘。
- 此外还建议去上位机代码中tool文件夹下找将标准KLE json文件转换为我定义的layout数组的小工具，将你的配列转换为数组并替换layout文件内的数组，这样上位机就可以绘制出你的配列的图像。
---

## 相关工程

| 工程 | 说明 |
|---|---|
| [KeyBoard-OG-key-binding-tool](https://github.com/LGGZi/KeyBoard-OG-key-binding-tool) | 上位机（改键 / 调参 / 灯效 / 配列显示） |
| [Simple-n-cheap-…-Hall-Effect-Magnetic-Switch-Keyboard](https://github.com/LGGZi/Simple-n-cheap-A-pretty-nice-one-hand-Hall-Effect-Magnetic-Switch-Keyboard) | 磁轴 14 键版本（RT / 死区 / 校准 / EC11） |

---

## 致谢

- HID 复合描述符基于 Arduino 官方的 `Keyboard` / `HID` 示例改写。
- 感谢在真机上反复验证映射、灯效、保存与版本协议的过程中提供的反馈。


