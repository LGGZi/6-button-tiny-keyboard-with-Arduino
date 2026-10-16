/*
  Keyboard.cpp

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

#include "SubHID.h"

#if defined(_USING_HID)

//================================================================================
//================================================================================
//	所有输入的 Sub descriptor

static const uint8_t _hidReportDescriptor[] PROGMEM = {
  /********** Xbox手柄描述 (Report ID 1)**********///=====================================================================
  0x05, 0x01,  // USAGE_PAGE (Generic Desktop)
  0x09, 0x05,  // USAGE (Game Pad)
  0xA1, 0x01,  // COLLECTION (Application)
  0x85, 0x01,  //   REPORT_ID (1)
  // 16个按钮（2字节）
  0x05, 0x09,  //   USAGE_PAGE (Button)
  0x19, 0x01,  //   USAGE_MINIMUM (Button 1)
  0x29, 0x10,  //   Usage Maximum (Button 16)
  0x15, 0x00,  //   LOGICAL_MINIMUM (0)
  0x25, 0x01,  //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,  //   REPORT_SIZE (1)
  0x95, 0x10,  //   Report Count (16)
  0x81, 0x02,  //   INPUT (Data,Var,Abs)
  // 左右摇杆（4字节）
  0x05, 0x01,        //   USAGE_PAGE (Generic Desktop)
  0x09, 0x30,        //   USAGE (X)
  0x09, 0x31,        //   USAGE (Y)
  0x09, 0x33,        //   USAGE (Rx)
  0x09, 0x34,        //   USAGE (Ry)
  0x15, 0x81,        //   LOGICAL_MINIMUM (-127) 
  0x25, 0x7f,        //   LOGICAL_MAXIMUM (127) 
  0x75, 0x08,        //   REPORT_SIZE (8)
  0x95, 0x04,        //   REPORT_COUNT (4)
  0x81, 0x02,        //   INPUT (Data,Var,Abs)
  // 左右扳机（2字节）
  0x05, 0x02,  //   USAGE_PAGE (Simulation Controls)
  0x09, 0xC5,  //   USAGE (Accelerator)
  0x09, 0xC4,  //   USAGE (Brake)
  0x15, 0x00,  //   LOGICAL_MINIMUM (0)
  0x25, 0xFF,  //   LOGICAL_MAXIMUM (255)
  0x75, 0x08,  //   REPORT_SIZE (8)
  0x95, 0x02,  //   REPORT_COUNT (2)
  0x81, 0x02,  //   INPUT (Data,Var,Abs)
  0xc0,                          /* END_COLLECTION */ 

  /********** 键盘描述 (Report ID 2)**********///=====================================================================
  0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
  0x09, 0x06,        // USAGE (Keyboard)
  0xA1, 0x01,        // COLLECTION (Application)
  0x85, 0x02,        // REPORT_ID (2)
  // 普通键（13字节)
  0x05, 0x07,                    // USAGE_PAGE (Keyboard)
  0x19, 0x00,                    // USAGE_MINIMUM (Reserved (no event indicated))
  0x29, 0x67,                    // USAGE_MAXIMUM (Keypad =)
  0x15, 0x00,                    // LOGICAL_MINIMUM (0)
  0x25, 0x01,                    // LOGICAL_MAXIMUM (1)
  0x75, 0x01,                    // REPORT_SIZE (1)
  0x95, 0x68,                    // REPORT_COUNT (104)
  0x81, 0x02,                    // INPUT (Data,Var,Abs)
  // 修饰键（1字节：左右Ctrl/Shift/Alt/GUI）
  0x05, 0x07,                    // USAGE_PAGE (Keyboard)
  0x19, 0xe0,                    // USAGE_MINIMUM (Keyboard LeftControl)
  0x29, 0xe7,                    // USAGE_MAXIMUM (Keyboard Right GUI)
  0x15, 0x00,                    // LOGICAL_MINIMUM (0)
  0x25, 0x01,                    // LOGICAL_MAXIMUM (1)
  0x75, 0x01,                    // REPORT_SIZE (1)
  0x95, 0x08,                    // REPORT_COUNT (8)
  0x81, 0x02,                     // INPUT (Data,Var,Abs)
  0xc0,                          /* END_COLLECTION */ 

 /********** 鼠标描述 (Report ID 3)**********///=====================================================================
  0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
  0x09, 0x02,        // USAGE (Mouse)
  0xA1, 0x01,        // COLLECTION (Application)
  0x85, 0x03,        //   REPORT_ID (3)
  // 按钮（3个按钮）
  0x05, 0x09,        //   USAGE_PAGE (Button)
  0x19, 0x01,        //   USAGE_MINIMUM (Button 1)
  0x29, 0x03,        //   USAGE_MAXIMUM (Button 3)
  0x15, 0x00,        //   LOGICAL_MINIMUM (0)
  0x25, 0x01,        //   LOGICAL_MAXIMUM (1)
  0x95, 0x03,        //   REPORT_COUNT (3)
  0x75, 0x01,        //   REPORT_SIZE (1)
  0x81, 0x02,        //   INPUT (Data,Var,Abs)
  //填充5bit，补齐一字节
  0x95, 0x01,        //   REPORT_COUNT (1)（填充5位）
  0x75, 0x05,        //   REPORT_SIZE (5)
  0x81, 0x03,        //   INPUT (Cnst,Var,Abs)
  // X/Y轴移动和滚轮，各1字节（3字节）
  0x05, 0x01,        //   USAGE_PAGE (Generic Desktop)
  0x09, 0x30,        //   USAGE (X)
  0x09, 0x31,        //   USAGE (Y)
  0x09, 0x38,        //   WHEEL
  0x15, 0x81,        //   LOGICAL_MINIMUM (-127)
  0x25, 0x7F,        //   LOGICAL_MAXIMUM (127)
  0x75, 0x08,        //   REPORT_SIZE (8)
  0x95, 0x03,        //   REPORT_COUNT (3)
  0x81, 0x06,        //   INPUT (Data,Var,Rel)
  0xc0,                          /* END_COLLECTION */ 
 /********** 指令 (Report ID 4)**********///=====================================================================
 // 共8字节
  0x06, 0x00, 0xff,              // USAGE_PAGE (Generic Desktop)
  0x09, 0x01,                    // USAGE (Vendor Usage 1)
  0xa1, 0x01,                    // COLLECTION (Application)
  0x85, 0x04,                    //   REPORT_ID (4)
  0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
  0x26, 0xff, 0x00,              //   LOGICAL_MAXIMUM (255)
  0x75, 0x08,                    //   REPORT_SIZE (8)
  0x95, 0x08,                    //   REPORT_COUNT (8)
  0x09, 0x01,                    //   USAGE (Vendor Usage 1)
  0x81, 0x02,                    //   INPUT (Data,Var,Abs)
  0x09, 0x01,                    //   USAGE (Vendor Usage 1)
  0x91, 0x02,                    //   OUTPUT (Data,Var,Abs)
  0xc0,                           // END_COLLECTION
  /********** 消费类控制 (Report ID 5) **********/
  0x05, 0x0C,                    // USAGE_PAGE (Consumer)
  0x09, 0x01,                    // USAGE (Consumer Control)
  0xA1, 0x01,                    // COLLECTION (Application)
  0x85, 0x05,                    //   REPORT_ID (5)
  0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
  0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,                    //   REPORT_SIZE (1)
  // 定义6个按钮：静音、音量减、音量增、播放/暂停、下一曲、上一曲
  0x09, 0xE2,                    //   USAGE (Mute)
  0x09, 0xEA,                    //   USAGE (Volume Down)
  0x09, 0xE9,                    //   USAGE (Volume Up)
  0x09, 0xCD,                    //   USAGE (Play/Pause)
  0x09, 0xB5,                    //   USAGE (Next Track)
  0x09, 0xB6,                    //   USAGE (Previous Track)
  0x95, 0x06,                    //   REPORT_COUNT (6)
  0x81, 0x02,                    //   INPUT (Data,Var,Abs)
  // 填充剩余的2位，凑齐1字节
  0x95, 0x01,                    //   REPORT_COUNT (1)
  0x75, 0x02,                    //   REPORT_SIZE (2)
  0x81, 0x03,                    //   INPUT (Cnst,Var,Abs)
  0xC0,                          // END_COLLECTION
};

HIDCon_::HIDCon_(void) {
  static HIDSubDescriptor node(_hidReportDescriptor, sizeof(_hidReportDescriptor));
  HID().AppendDescriptor(&node);
}

void HIDCon_::setState(HIDConReport_t *report) {

  // Xbox手柄数据 (Report ID 1)
  uint8_t xbox_data[8]; // 2(按钮) + 4(摇杆) + 2(扳机) = 8字节
  memcpy(xbox_data, &report->xboxButtons, 2);    // 16位按钮
  memcpy(xbox_data + 2, &report->lxAxis, 4);     // 4个8位摇杆轴
  memcpy(xbox_data + 6, &report->lt, 2);        // 2个8位扳机
  HID().SendReport(1, xbox_data, sizeof(xbox_data));

  // 键盘数据 (Report ID 2)
  uint8_t keyboard_data[14] = {
    report->keyboardKeys[0],   // 普通键
    report->keyboardKeys[1],   // 普通键
    report->keyboardKeys[2],   // etc
    report->keyboardKeys[3],
    report->keyboardKeys[4],
    report->keyboardKeys[5],
    report->keyboardKeys[6],
    report->keyboardKeys[7],
    report->keyboardKeys[8],
    report->keyboardKeys[9],
    report->keyboardKeys[10],
    report->keyboardKeys[11],
    report->keyboardKeys[12],
    report->keyboardKeys[13]   // 修饰键（1字节）
  };
  HID().SendReport(2, keyboard_data, sizeof(keyboard_data));

  // 鼠标数据 (Report ID 3)
  uint8_t mouse_data[4] = {
    report->mouseButtons, // 按钮状态（低3位有效）
    report->mouseX,        // X轴
    report->mouseY,        // Y轴
    report->wheel          // 滚轮
  };
  HID().SendReport(3, mouse_data, sizeof(mouse_data));

}
void HIDCon_::setConsumerState(uint8_t consumer_data){
  // 消费类控制 (Report ID 5) 单独逻辑，仅响应变化的按键
  static uint8_t pre_consumer_data = 0;
  consumer_data &= 0x3F;
  if(pre_consumer_data != consumer_data){
    uint8_t diff = pre_consumer_data ^ consumer_data;
    uint8_t payload = consumer_data & diff;
    HID().SendReport(5, &payload, sizeof(payload));
    pre_consumer_data = consumer_data;
  }
}

HIDCon_ HIDCon;

#endif
