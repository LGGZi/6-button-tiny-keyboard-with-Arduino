/*
  Keyboard.h

  Copyright (c) 2015, Arduino LLC
  Original code (pre-library): Copyright (c) 2011, Peter Barrett

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef HIDCon_h
#define HIDCon_h

#include "HID.h"

#if !defined(_USING_HID)

#warning "USING OLD HID CORE, UPGRADE TO ARDUINO 1.6.8"

#else

extern volatile uint8_t HIDcmd[8];

// 复合设备的报文所对应的数据结构
typedef struct HIDConReport {
    // 键盘部分 (Report ID 2)
  uint8_t keyboardKeys[14];  // 前13字节为键盘按键，从UsageTable的0开始，后跟1字节键盘修饰键

    // 鼠标部分 (Report ID 3)
  uint8_t   mouseButtons; // 低3位表示按钮状态（0b00000111）
  int8_t    mouseX;       // X轴移动（-127 ~ 127）
  int8_t    mouseY;       // Y轴移动（-127 ~ 127）
  int8_t    wheel;        // 滚轮移动（-127 ~ 127）
      // Xbox 手柄部分 (Report ID 1)
  int8_t   lxAxis;       // 左摇杆X轴（-127 ~ 127）
  int8_t   lyAxis;       // 左摇杆Y轴（-127 ~ 127）
  int8_t   rxAxis;       // 右摇杆X轴（-127 ~ 127）
  int8_t   ryAxis;       // 右摇杆Y轴（-127 ~ 127）
  uint8_t   lt;           // 左扳机（0~255）
  uint8_t   rt;           // 右扳机（0~255）
  uint16_t  xboxButtons;  // 支持最多16个按钮（Xbox标准）

  uint8_t mediaButtons; // 低3位：bit0=静音, bit1=音量减, bit2=音量增
} HIDConReport_t;

class HIDCon_
{
public:
  HIDCon_();
  void setState(HIDConReport_t *report);
  void HIDCon_::setConsumerState(uint8_t consumer_data);
  // inline uint16_t readState(){ return HIDCon_led; };
};

extern HIDCon_ HIDCon;

#endif
#endif
