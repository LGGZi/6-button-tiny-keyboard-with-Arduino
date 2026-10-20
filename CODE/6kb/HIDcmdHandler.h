#ifndef HIDcmdHandler_h
#define HIDcmdHandler_h

#include <stdint.h>
#include "EEPROMManager.h"
#include "LayoutManager.h"
//六键机械键盘没有霍尔，所以不包含 HallUnitManager.h
// Byte 1, CMD Code
#define CMD_SET_MAP      0x01
#define CMD_SET_PARAM    0x02
#define CMD_CALIBRATE    0x03
#define CMD_QUERY        0x04
#define CMD_ACK          0x05
#define CMD_FIN          0x06
#define CMD_REFRESH      0x07
#define CMD_SET_LED      0x08   // 即时设置灯效：[1]=effect [2]=亮度 [3]=速度 [4..6]=主色 RGB
#define CMD_SET_LED_ACCENT 0x09 // 即时设置副色：[1..3]=副色 RGB
#define CMD_SAVE_LED     0x0A   // 把当前灯效参数写进 EEPROM：[1]=0x5A 确认

// Byte 2, Device, when mapping keys
#define NO_DEVICE 0x00
#define MAP_TYPE_KEYBOARD   0x01
#define MAP_TYPE_MOUSE      0x02
#define MAP_TYPE_GAMEPAD    0x03
#define MAP_TYPE_CONSUMER   0x04
#define MAP_TYPE_CONTROLLER_XAXIS_LEFT 0x05
#define MAP_TYPE_CONTROLLER_YAXIS_LEFT 0x06
#define MAP_TYPE_CONTROLLER_XAXIS_RIGHT 0x07
#define MAP_TYPE_CONTROLLER_YAXIS_RIGHT 0x08
#define MAP_TYPE_MOUSE_X 0x09
#define MAP_TYPE_MOUSE_Y 0x0A
#define MAP_TYPE_CONTROLLER_LEFT_TRIGGER 0x0B
#define MAP_TYPE_CONTROLLER_RIGHT_TRIGGER 0x0C
// Byte 2, When calibrating：CQ_ALL / CQ_SINGLE
#define CQ_ALL     0x00
#define CQ_SINGLE  0x01
// Byte 2, When querying
#define QUERY_MAPPING       0x00
#define QUERY_PARAM         0x01
#define QUERY_CALIB         0x02
#define QUERY_DEVICE_INFO   0xFF
// 查询配列（布局）：[1] = QUERY_LAYOUT，逐键回 0xFE 帧后回 FIN
#define QUERY_LAYOUT        0xFE
// 查询指令布局：[0]=CMD_QUERY [1]=查询内容 [2]=CQ_ALL/CQ_SINGLE [3]=按键号 [7]=序号
//Byte 2, When refresh
#define REFRESH_MAPPING       0x00
#define REFRESH_PARAM         0x01

// Byte 3, 
/*  Key number(mapping, parameter setting, calibrating single, querying single)*/

// Byte 4, Report data offset/parameter value high byte

// Byte 5, Binding's operation when mapping
#define MAP_DO_APPEND   0x00
#define MAP_DO_OVERRIDE 0x01
#define MAP_DO_CLEAR    0x02


// Byte 6, when MAP_DO_OVERRIDE：该按键映射区里的第几个槽（0 开始，< MAXIMUM_BINDABLE_KEY）

uint8_t pre_cmd[8];

/*-------------------------------------------------------------
独立的设备信息报文：结构为 
[0xFF]、 [BUTTONCOUNT]、 [是否为磁轴0xF0 == y|0x0F == n]、 [编码器数量，0就是没有）]、 [设备类型（保留给后续手台主控使用）]、
[布局类型 layoutKind，0=旧设备/0xFF=自定义]、[保留]、[固件版本号]
版本号格式示例 0 1 | 0 1 1 | 0 0 1  ==  1.31
前两位小数点前整数、中间三位小数点后一位、最后三位为小数点后第二位
*/
void HIDDeviceInfoUpload(){
    //编码器数量填 1：硬件上留了编码器（ENC_L_A/B = 2/3），功能目前只是保留
    //设备类型沿用 0x01（保留给后续手台主控使用）
    uint8_t resp[8] = { 0xFF, BUTTONCOUNT, 0x0F, 0x01, 0x01, LAYOUT_KIND, 0, FIRMWARE_VERSION };
    HID().SendReport(4, resp, 8);
}

//收到指令立刻回的应答，格式与上位机发送的一致（Byte1 换成 CMD_ACK，其余保留）
void HIDSendAck(const uint8_t *cmdBytes) {
    uint8_t ack[8] = { CMD_ACK, cmdBytes[0], cmdBytes[7], 0, 0, 0, 0, 0 };
    HID().SendReport(4, ack, 8);
}
//一条指令处理完毕回的完成标志，格式同样与上位机发送的一致（Byte1 换成 CMD_FIN，其余原样回传），
//这样上位机既能确认收到了，也能确认执行完了
void HIDSendFin(const uint8_t *cmdBytes) {
    uint8_t fin[8];
    for(uint8_t i = 0; i < 8; i++){
        fin[i] = cmdBytes[i];
    }
    fin[0] = CMD_FIN;
    HID().SendReport(4, fin, 8);
}
// ------------------------------------------------------------
// 查询上传：把查到的内容逐条发回去，每条都构造成"和上位机下发时一样的 8 字节"，
// 上位机可以直接复用下发那一套字段位置来解析：
//   键值：[0]=CMD_SET_MAP, [1]=device, [2]=key, [3]=offset, [4]=MAP_DO_OVERRIDE, [5]=slot
// 本板没有磁轴参数，所以不查也不回参数
// 读 EEPROM 的动作在 EEPROMManager 里，这里只负责"读一条发一条"，保证原子性
// ------------------------------------------------------------
void HIDSendMapItem(uint8_t device, uint8_t key, uint8_t offset, uint8_t slot) {
    uint8_t resp[8] = { CMD_SET_MAP, device, key, offset, MAP_DO_OVERRIDE, slot, 0, 0 };
    HID().SendReport(4, resp, 8);
}
void HIDQueryUpload(uint8_t queryWhat, uint8_t queryScope, uint8_t key) {
    if(queryScope == CQ_SINGLE && !(key < BUTTONCOUNT)){
        return;   //单键查询的按键号非法
    }
    if(queryWhat != QUERY_MAPPING){
        return;   
    }
    uint8_t first_key = (queryScope == CQ_SINGLE) ? key : 0;
    uint8_t last_key  = (queryScope == CQ_SINGLE) ? key : (BUTTONCOUNT - 1);

    for(uint8_t k = first_key; k <= last_key; k++){
        for(uint8_t slot = 0; slot < MAXIMUM_BINDABLE_KEY; slot++){
            uint8_t offset = 0;
            uint8_t device = keyMapGetFromEEPROM(k, slot, &offset);
            if(device == NO_DEVICE){
                break;   //空槽：本按键的映射到此为止
            }
            HIDSendMapItem(device, k, offset, slot);
        }
    }
}
/**
 * 解析 8 字节 HID 报告（Report ID 4 的数据部分）
 * @param report 8字节数组指针
 */
void HIDcmdResponse(const uint8_t *report_cmd) {
    //先做一份快照：校准是阻塞操作，期间 USB 中断可能已经把 HIDcmd 改写成新指令，
    //如果一直引用 report_cmd，最后的 FIN 就会回成别的指令
    uint8_t cmd_buf[8];
    for(uint8_t i = 0; i < 8; i++){
        cmd_buf[i] = report_cmd[i];
    }
    HIDSendAck(cmd_buf);
    uint8_t cmd = cmd_buf[0];
    switch (cmd) {
        // ------------------------------------------------------------
        // 命令 1：修改按键映射
        // ------------------------------------------------------------
        case CMD_SET_MAP: {
            uint8_t operation = cmd_buf[4];      //Byte5
            switch (operation){
                case MAP_DO_APPEND:{
                    uint8_t device = cmd_buf[1]; //Byte2
                    uint8_t key = cmd_buf[2];    //Byte3
                    uint8_t offset = cmd_buf[3]; //Byte4
                    keyCodeAddToEEPROM(key, device, offset);
                break;
                }
                case MAP_DO_OVERRIDE:{
                    uint8_t device = cmd_buf[1];         //Byte2
                    uint8_t key = cmd_buf[2];            //Byte3
                    uint8_t offset = cmd_buf[3];         //Byte4
                    uint8_t slot = cmd_buf[5];   //Byte6：本按键的第几个映射槽
                    if(slot < MAXIMUM_BINDABLE_KEY){
                        _keyCodeSetToEEPROM(key, slot, device, offset);
                    }
                break;
                }
                case MAP_DO_CLEAR:{
                    #if DEBUG
                        Serial.print("收到清空指令\n");
                    #endif
                    uint8_t key = cmd_buf[2];           //Byte3
                    uint8_t clear_whom = cmd_buf[5];    //Byte6
                    if(clear_whom == BUTTONCOUNT){
                        keyCodeClearAll();
                    }else if(clear_whom >= 0 && clear_whom < BUTTONCOUNT){
                        keyMapClearInEEPROM(key, clear_whom);
                        //写完后用这个指令，把它后面的槽全部设成空槽
                    }
                    keyCodeInit();
                break;
                }
            }
            HIDSendFin(cmd_buf);
        break;
        }
        // ------------------------------------------------------------
        // 命令 2（修改按键参数）和命令 3（校准）都是磁轴键盘专有的，
        // 六键机械键盘没有霍尔，这两个命令不处理，直接落到 default 回一个 FIN
        // ------------------------------------------------------------

        // ------------------------------------------------------------
        // 命令 4：查询请求
        // [1]=查询内容(QUERY_MAPPING/QUERY_PARAM) [2]=CQ_ALL/CQ_SINGLE [3]=按键号
        // ------------------------------------------------------------
        case CMD_QUERY: {
            uint8_t query_what  = cmd_buf[1];   //Byte2
            uint8_t query_scope = cmd_buf[2];   //Byte3
            uint8_t key        = cmd_buf[3];   //Byte4
            if(query_what == QUERY_DEVICE_INFO){
                HIDDeviceInfoUpload();
            }else if(query_what == QUERY_LAYOUT){
                HIDLayoutUpload(cmd_buf);
                HIDSendFin(cmd_buf);
            }else{
                HIDQueryUpload(query_what, query_scope, key);
                HIDSendFin(cmd_buf);
            }
        break;
        }
        // ------------------------------------------------------------
        // 命令 8 / 9 / 10：灯效参数（即时生效 + 单独保存）
        // ------------------------------------------------------------
        case CMD_SET_LED: {
            wsSetEffect(cmd_buf[1]);                       //Byte2
            wsSetBrightness(cmd_buf[2]);                   //Byte3
            wsSetSpeed(cmd_buf[3]);                        //Byte4
            wsSetColor1RGB(cmd_buf[4], cmd_buf[5], cmd_buf[6]);   //Byte5~7
            HIDSendFin(cmd_buf);
        break;
        }
        case CMD_SET_LED_ACCENT: {
            wsSetColor2RGB(cmd_buf[1], cmd_buf[2], cmd_buf[3]);
            HIDSendFin(cmd_buf);
        break;
        }
        case CMD_SAVE_LED: {
            //多一道 0x5A 确认，避免误发一条就把 EEPROM 写掉
            if(cmd_buf[1] == LED_PARAMS_MAGIC){
                ledParamsSaveToEEPROM();
            }
            HIDSendFin(cmd_buf);
        break;
        }
        case CMD_REFRESH: {
            uint8_t refresh_what = cmd_buf[1];  //Byte2
            if(refresh_what == REFRESH_MAPPING){
                keyCodeInit();
            }
            HIDSendFin(cmd_buf);
        break;
        }
        default:
            // 未知命令码：也回一个 FIN，免得上位机一直等在那
            HIDSendFin(cmd_buf);
        break;
    }
}

void HIDcmdResponseIfChanged(){
    if(memcmp(pre_cmd, HIDcmd, 8) != 0){
        // Serial.print("cmd get\n");
        HIDcmdResponse(HIDcmd);
        memcpy(pre_cmd, HIDcmd, 8);
    }
}
#endif