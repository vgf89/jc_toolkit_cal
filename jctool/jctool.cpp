// Copyright (c) 2018 CTCaer. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.


#include <cstdint>
#include <msclr\marshal_cppstd.h>
#include <string>
#include <iostream>
#include <conio.h>
#include <Windows.h>

#include "jctool.h"
#include "hidapi.h"

#pragma comment(lib, "SetupAPI")

bool enable_traffic_dump = false;

hid_device *handle;
hid_device *handle_l;

s16 uint16_to_int16(u16 a) {
    s16 b;
    if (a > 0x7FF) {
        b = -((s16)(0xFFF - a));
        b -= 1;
    }
    else {
        b = (s16)a;
    }
    return b;
}

u16 int16_to_uint16(s16 a) {
    u16 b;
    if (a < 0) {
        b = (u16)(0xFFF + a);
        b += 1;
    }
    else {
        b = (u16)a;
    }
    return b;
}

void decode_stick_params(u16 *decoded_stick_params, u8 *encoded_stick_params) {
    decoded_stick_params[0] = (encoded_stick_params[1] << 8) & 0xF00 | encoded_stick_params[0];
    decoded_stick_params[1] = (encoded_stick_params[2] << 4) | (encoded_stick_params[1] >> 4);
}

void encode_stick_params(u8 *encoded_stick_params, u16 *decoded_stick_params) {
    encoded_stick_params[0] =  decoded_stick_params[0] & 0xFF;
    encoded_stick_params[1] = (decoded_stick_params[0] & 0xF00) >> 8 | (decoded_stick_params[1] & 0xF) << 4;
    encoded_stick_params[2] = (decoded_stick_params[1] & 0xFF0) >> 4;
}


// Credit to Hypersect (Ryan Juckett)
// http://blog.hypersect.com/interpreting-analog-sticks/
void AnalogStickCalc(
    float *pOutX,       // out: resulting stick X value
    float *pOutY,       // out: resulting stick Y value
    u16 x,              // in: initial stick X value
    u16 y,              // in: initial stick Y value
    u16 x_calc[3],      // calc -X, CenterX, +X
    u16 y_calc[3]       // calc -Y, CenterY, +Y
)
{
    float x_f, y_f;
    // Apply Joy-Con center deadzone. 0xAE translates approx to 15%. Pro controller has a 10% deadzone.
    float deadZoneCenter = 0.15f;
    // Add a small ammount of outer deadzone to avoid edge cases or machine variety.
    float deadZoneOuter = 0.10f;

    // convert to float based on calibration and valid ranges per +/-axis
    x = CLAMP(x, x_calc[0], x_calc[2]);
    y = CLAMP(y, y_calc[0], y_calc[2]);
    if (x >= x_calc[1])
        x_f = (float)(x - x_calc[1]) / (float)(x_calc[2] - x_calc[1]);
    else
        x_f = -((float)(x - x_calc[1]) / (float)(x_calc[0] - x_calc[1]));
    if (y >= y_calc[1])
        y_f = (float)(y - y_calc[1]) / (float)(y_calc[2] - y_calc[1]);
    else
        y_f = -((float)(y - y_calc[1]) / (float)(y_calc[0] - y_calc[1]));

    // Interpolate zone between deadzones
    float mag = sqrtf(x_f*x_f + y_f*y_f);
    if (mag > deadZoneCenter) {
        // scale such that output magnitude is in the range [0.0f, 1.0f]
        float legalRange = 1.0f - deadZoneOuter - deadZoneCenter;
        float normalizedMag = min(1.0f, (mag - deadZoneCenter) / legalRange);
        float scale = normalizedMag / mag;
        pOutX[0] = x_f * scale;
        pOutY[0] = y_f * scale;
    }
    else
    {
        // stick is in the inner dead zone
        pOutX[0] = 0.0f;
        pOutY[0] = 0.0f;
    }
}

int get_spi_data(u32 offset, const u16 read_len, u8 *test_buf) {
    u8 buf[0x100];
    int res;
    
    do {
        // Prepare command buffer
        memset(buf, 0, sizeof(buf));
        auto hdr = (brcm_hdr *)buf;
        auto pkt = (brcm_cmd_01 *)(hdr + 1);
        
        // Set up header and packet
        hdr->cmd = 0x01;
        hdr->timer = timming_byte & 0xF;
        timming_byte++;
        pkt->subcmd = 0x10;
        pkt->spi_data.offset = offset;
        pkt->spi_data.size = read_len;
        
        // Send command
        hid_write(handle, buf, sizeof(*hdr) + sizeof(*pkt));
        
        // Read response
        res = hid_read_timeout(handle, buf, 0, 200);
    } while (res == 0 || *(u16*)&buf[0xD] != offset || *(u16*)&buf[0xF] != read_len);
    
    // Copy data if we got enough bytes
    if (res >= 0x14 + read_len) {
        memcpy(test_buf, &buf[0x14], read_len);
    }
    
    return 0;
}
int write_spi_data(u32 offset, const u16 write_len, u8* test_buf) {
    u8 buf[49];
    const int MAX_ATTEMPTS = 20;
    const int MAX_RETRIES = 8;
    
    for (int attempts = 0; attempts < MAX_ATTEMPTS; attempts++) {
        // Prepare command buffer
        memset(buf, 0, sizeof(buf));
        auto hdr = (brcm_hdr*)buf;
        auto pkt = (brcm_cmd_01*)(hdr + 1);
        
        // Set up header and packet
        hdr->cmd = 1;
        hdr->timer = timming_byte & 0xF;
        timming_byte++;
        pkt->subcmd = 0x11;
        pkt->spi_data.offset = offset;
        pkt->spi_data.size = write_len;
        
        // Copy data to send
        memcpy(&buf[0x10], test_buf, write_len);
        
        // Send command
        hid_write(handle, buf, sizeof(buf));
        
        // Wait for response
        for (int retries = 0; retries < MAX_RETRIES; retries++) {
            int res = hid_read_timeout(handle, buf, sizeof(buf), 64);
            
            // Check for success
            if (*(u16*)&buf[0xD] == 0x1180)
                return 0;
                
            if (res == 0)
                break;
        }
    }
    
    return 1;  // Failed after maximum attempts
}

int get_device_info(u8* test_buf) {
    int res;
    u8 buf[49];
    int error_reading = 0;
    while (1) {
        memset(buf, 0, sizeof(buf));
        auto hdr = (brcm_hdr*)buf;
        auto pkt = (brcm_cmd_01*)(hdr + 1);
        hdr->cmd = 1;
        hdr->timer = timming_byte & 0xF;
        timming_byte++;
        pkt->subcmd = 0x02;
        res = hid_write(handle, buf, sizeof(buf));
        int retries = 0;
        while (1) {
            res = hid_read_timeout(handle, buf, sizeof(buf), 64);
            if (*(u16*)&buf[0xD] == 0x0282)
                goto check_result;

            retries++;
            if (retries > 8 || res == 0)
                break;
        }
        error_reading++;
        if (error_reading > 20)
            break;
    }
check_result:
    for (int i = 0; i < 0xA; i++) {
        test_buf[i] = buf[0xF + i];
    }

    return 0;
}

int device_connection() {
    if (check_connection_ok) {
        handle_ok = 0;
        // Joy-Con (L)
        if (handle = hid_open(0x57e, 0x2006, nullptr)) {
            handle_ok = 1;
            return handle_ok;
        }
        // Joy-Con (R)
        if (handle = hid_open(0x57e, 0x2007, nullptr)) {
            handle_ok = 2;
            return handle_ok;
        }
        // Pro Controller
        if (handle = hid_open(0x57e, 0x2009, nullptr)) {
            handle_ok = 3;
            return handle_ok;
        }
        // Nothing found
        else {
            return 0;
        }
    }
    return handle_ok;
}

struct DECODED_FACTORY_STICK_CAL {
    u16 xmax;
    u16 ymax;
    u16 xcenter;
    u16 ycenter;
    u16 xmin;
    u16 ymin;
};

int write_left_stick_calibration(struct DECODED_FACTORY_STICK_CAL left_cal) {
    u16 data_l[6] = { 0 };
    // Convert left_cal back into data_l array
    data_l[0] = left_cal.xmax - left_cal.xcenter;
    data_l[1] = left_cal.ymax - left_cal.ycenter;
    data_l[2] = left_cal.xcenter;
    data_l[3] = left_cal.ycenter;
    data_l[4] = left_cal.xcenter - left_cal.xmin;
    data_l[5] = left_cal.ycenter - left_cal.ymin;
    // print data_l
    //printf("\nLeft stick calibration data: %04X %04X %04X %04X %04X %04X\n", data_l[0], data_l[1], data_l[2], data_l[3], data_l[4], data_l[5]);
    // Convert data_l array back into left_stick_cal
    u8 left_stick_cal[0x9] = { 0 };
    left_stick_cal[0] = data_l[0] & 0xFF;
    left_stick_cal[1] = ((data_l[0] >> 8) & 0x0F) | ((data_l[1] << 4) & 0xF0);
    left_stick_cal[2] = (data_l[1] >> 4) & 0xFF;
    left_stick_cal[3] = (data_l[2] & 0xFF);
    left_stick_cal[4] = ((data_l[2] >> 8) & 0x0F) | ((data_l[3] << 4) & 0xF0);
    left_stick_cal[5] = (data_l[3] >> 4) & 0xFF;
    left_stick_cal[6] = (data_l[4] & 0xFF);
    left_stick_cal[7] = ((data_l[4] >> 8) & 0x0F) | ((data_l[5] << 4) & 0xF0);
    left_stick_cal[8] = (data_l[5] >> 4) & 0xFF;
    // Print left_stick_cal
    //printf("\nLeft stick calibration data: %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", left_stick_cal[0], left_stick_cal[1], left_stick_cal[2], left_stick_cal[3], left_stick_cal[4], left_stick_cal[5], left_stick_cal[7], left_stick_cal[8]);
    // Write out to SPI
    return write_spi_data(0x603D, sizeof(left_stick_cal), left_stick_cal);
}

int write_right_stick_calibration(struct DECODED_FACTORY_STICK_CAL right_cal) {
    u16 data_r[6] = { 0 };
    // Convert right_cal to data_r
    data_r[0] = right_cal.xcenter;
    data_r[1] = right_cal.ycenter;
    data_r[2] = right_cal.xcenter - right_cal.xmin;
    data_r[3] = right_cal.ycenter - right_cal.ymin;
    data_r[4] = right_cal.xmax - right_cal.xcenter;
    data_r[5] = right_cal.ymax - right_cal.ycenter;
    // Convert data_r array back into right_stick_cal
    u8 right_stick_cal[0x9] = { 0 };
    right_stick_cal[0] = data_r[0] & 0xFF;
    right_stick_cal[1] = ((data_r[0] >> 8) & 0x0F) | ((data_r[1] << 4) & 0xF0);
    right_stick_cal[2] = (data_r[1] >> 4) & 0xFF;
    right_stick_cal[3] = (data_r[2] & 0xFF);
    right_stick_cal[4] = ((data_r[2] >> 8) & 0x0F) | ((data_r[3] << 4) & 0xF0);
    right_stick_cal[5] = (data_r[3] >> 4) & 0xFF;
    right_stick_cal[6] = (data_r[4] & 0xFF);
    right_stick_cal[7] = ((data_r[4] >> 8) & 0x0F) | ((data_r[5] << 4) & 0xF0);
    right_stick_cal[8] = (data_r[5] >> 4) & 0xFF;
    // Write out to SPI
    return write_spi_data(0x6046, sizeof(right_stick_cal), right_stick_cal);
}

bool yesorno() {
    char in = 0;
    while (in != 'y' && in != 'n') {
        in = getch();
    }
    if (in == 'y') {
        return true;
    }
    return false;
}

std::string get_sn(u32 offset, const u16 read_len) {
    int res;
    int error_reading = 0;
    u8 buf[49];
    std::string test = "";
    while (1) {
        memset(buf, 0, sizeof(buf));
        auto hdr = (brcm_hdr *)buf;
        auto pkt = (brcm_cmd_01 *)(hdr + 1);
        hdr->cmd = 1;
        hdr->timer = timming_byte & 0xF;
        timming_byte++;
        pkt->subcmd = 0x10;
        pkt->spi_data.offset = offset;
        pkt->spi_data.size = read_len;
        res = hid_write(handle, buf, sizeof(buf));

        int retries = 0;
        while (1) {
            res = hid_read_timeout(handle, buf, sizeof(buf), 64);
            if ((*(u16*)&buf[0xD] == 0x1090) && (*(uint32_t*)&buf[0xF] == offset))
                goto check_result;

            retries++;
            if (retries > 8 || res == 0)
                break;
        }
        error_reading++;
        if (error_reading > 20)
            return "Error!";
    }
    check_result:
    if (res >= 0x14 + read_len) {
        for (int i = 0; i < read_len; i++) {
            if (buf[0x14 + i] != 0x000) {
                test += buf[0x14 + i];
            }else
                test += "";
            }
    }
    else {
        return "Error!";
    }
    return test;
}


// crc-8-ccitt / polynomial 0x07 look up table
static uint8_t mcu_crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};
u8 mcu_crc8_calc(u8* buf, u8 size) {
    u8 crc8 = 0x0;

    for (int i = 0; i < size; ++i) {
        crc8 = mcu_crc8_table[(u8)(crc8 ^ buf[i])];
    }
    return crc8;
}
        
[STAThread]
int Main(array<String^>^ args) {
    
    BOOL chk = AllocConsole();
    if (chk) {
        freopen("CONOUT$", "w", stdout);
    }
    
    bool raw_calibration = false;
    check_connection_ok = true;
    while (!device_connection()) {
        printf(
            "\nThe device is not paired or the device was disconnected!\n\n"\
            "To pair:\n  1. Press and hold the sync button until the leds are on\n"\
            "  2. Pair the Bluetooth controller in Windows\n\nTo connect again:\n"\
            "  1. Press a button on the controller\n  (If this doesn\'t work, re-pair.)\n\n"\
            "To re-pair:\n  1. Go to 'Settings -> Devices' or Devices and Printers'\n"\
            "  2. Remove the controller\n  3. Follow the pair instructions"\
            "CTCaer's Joy-Con Toolkit - Connection Error!\n\n"\
            "Press any key to Retry, or close the window to quit."
        );
        getch();
        printf("\n\n");
    }
    // Enable debugging
    if (args->Length > 0) {
        if (args[0] == "-f")
            check_connection_ok = false;   // Don't check connection after the 1st successful one
    }

    timming_byte = 0x0;

    std::cout << "Your controller is connected!" << std::endl;
    unsigned char device_info[10];
    memset(device_info, 0, sizeof(device_info));
    std::cout << "Getting device info..." << std::endl;
    get_device_info(device_info);
    std::cout << "Device info retrieved" << std::endl;

    std::string devinfo1 = msclr::interop::marshal_as< std::string >(String::Format("{0:X}.{1:X2}", device_info[0], device_info[1]));
    std::string devinfo2 = msclr::interop::marshal_as< std::string >(String::Format("{0:X2}:{1:X2}:{2:X2}:{3:X2}:{4:X2}:{5:X2}",
        device_info[4], device_info[5], device_info[6], device_info[7], device_info[8], device_info[9]));

    if (handle_ok == 1) {
        std::cout << "Joy-Con (L)" << std::endl;
    }
    else if (handle_ok == 2) {
        std::cout << "Joy-Con (R)" << std::endl;
    }
    else if (handle_ok == 3) {
        std::cout << "Pro Controller" << std::endl;
    }
    else {
        std::cout << "Something went wrong getting device info." << std::endl;
        std::cout << "Press any key to quit." << std::endl;
        getch();
        return 0;

    }

    std::cout << "Firmware:" << devinfo1 << std::endl;
    std::cout << "MAC Address:" << devinfo2 << std::endl;


    printf("\nWarning: Some parameters in factory stick calibration appear to be used incorrectly on Windows.\nSpecifically, the stick deadzone seems to be applied *before* the center calibration, resulting in oddities near the deadzone.\nThis phenomenon does not appear on a real Nintendo Switch, so please test centering/deadzone there after running this calibration.\n\n");
    printf("Would you like to calibrate your controller sticks? y/n: \n");
    if (yesorno() == false) {
        printf("Quitting. No changes have been made.\nPress any key to quit\n");
        getch();
        return 0;
    }

    printf("\nPress 1 to enter the calibration routine, \nor press 2 to flash a max-range 'raw' calibration (useful when physically aligning Hall Effect magnets).\n");
    printf("\nIf you just installed new Hall Effect(HE) sticks, it is highly suggested to first flash the max-range calibration.\n");
    char input = getch();
    if (input == '1') {
        printf("Beginning calibration routine...\n");
    }
    else if (input == '2') {
        printf("Flasing max-range calibration...\n");
        raw_calibration = true;
    }
    else {
        printf("Quitting. No changes have been made.\nPress any key to quit\n");
        getch();
        return 0;
    }



    u8 left_stick_cal[0x9];
    u8 right_stick_cal[0x9];
    struct DECODED_FACTORY_STICK_CAL left_cal = { 0 };
    struct DECODED_FACTORY_STICK_CAL right_cal = { 0 };
    int res;
    u8 buf_reply[0x170];
    u32 min_lx, max_lx, min_ly, max_ly, min_rx, max_rx, min_ry, max_ry;
    u16 left_deadzone, right_deadzone;



    if (raw_calibration) {
        printf("Flashing raw calibration.");
        min_lx = min_ly = min_rx = min_ry = 0;
        max_lx = max_ly = max_rx = max_ry = 0xfff;
        left_cal.xmin = left_cal.ymin = right_cal.xmin = right_cal.ymin = 0;
        left_cal.xmax = left_cal.ymax = right_cal.xmax = right_cal.ymax = 0xfff;
        left_cal.xcenter = left_cal.ycenter = right_cal.xcenter = right_cal.ycenter = 0x7ff;
        left_deadzone = right_deadzone = 0;
        goto write_cal_label;
    }

    

    printf("Release the sticks, then press any key.\n");
    getch();
    // Enable nxpad standard input report
    u8 buf_cmd[49];
    memset(buf_cmd, 0, sizeof(buf_cmd));
    auto hdr = (brcm_hdr *)buf_cmd;
    auto pkt = (brcm_cmd_01 *)(hdr + 1);
    hdr->cmd = 0x01;
    hdr->timer = timming_byte & 0xF;
    timming_byte++;
    pkt->subcmd = 0x03;
    pkt->subcmd_arg.arg1 = 0x30;
    res = hid_write(handle, buf_cmd, sizeof(buf_cmd));
    res = hid_read_timeout(handle, buf_cmd, 0, 120);
    
    // Collect samples to find average center and ideal deadzone.
    printf("\n\nCalibrating joystick Center and Deadzone.\n\nGently wiggle the sticks around the center within the area where the stick spring is slack.\nWhen you are finished, press Y on the controller or Left Dpad button.\n\n");

    min_lx = min_ly = min_rx = min_ry = 0xFFF;
    max_lx = max_ly = max_rx = max_ry = 0x000;
    u32 lx_center, ly_center, rx_center, ry_center;
    int i = 0;
    while (buf_reply[3] != 0x01 && buf_reply[5] != 0x08) {
        res = hid_read_timeout(handle, buf_reply, sizeof(buf_reply), 200);
        if (res > 12) {
            u16 lx = buf_reply[6] | (u16)((buf_reply[7] & 0xF) << 8);
            u16 ly = (buf_reply[7] >> 4) | (buf_reply[8] << 4);
            u16 rx = buf_reply[9] | (u16)((buf_reply[10] & 0xF) << 8);
            u16 ry = (buf_reply[10] >> 4) | (buf_reply[11] << 4);
            min_lx = lx < min_lx ? lx : min_lx;
            max_lx = lx > max_lx ? lx : max_lx;
            min_ly = ly < min_ly ? ly : min_ly;
            max_ly = ly > max_ly ? ly : max_ly;
            
            min_rx = rx < min_rx ? rx : min_rx;
            max_rx = rx > max_rx ? rx : max_rx;
            min_ry = ry < min_ry ? ry : min_ry;
            max_ry = ry > max_ry ? ry : max_ry;


            printf("\rReading: L(%03X, %03X) R(%03X, %03X)   Min/Max L(%03X, %03X, %03X, %03X) R(%03X, %03X, %03Xm %03X)", lx, ly, rx, ry, min_lx, min_ly, max_lx, max_ly, min_rx, min_ry, max_rx, max_ry);
        }
    }
    
    const u16 EXTRA_INNER_DEADZONE = 0x00;
    lx_center = (min_lx + max_lx) / 2;
    ly_center = (min_ly + max_ly) / 2;
    rx_center = (min_rx + max_rx) / 2;
    ry_center = (min_ry + max_ry) / 2;
    left_deadzone = ((max_lx - min_lx) / 2) + EXTRA_INNER_DEADZONE;
    right_deadzone = ((max_rx - min_rx) / 2) + EXTRA_INNER_DEADZONE;
    
    left_cal.xcenter = lx_center;
    left_cal.ycenter = ly_center;
    right_cal.xcenter = rx_center;
    right_cal.ycenter = ry_center;

    printf("\nCenters and deadzones found!\n");

    printf("\nCalibrating stick range.\n\n");
    printf("Slowly spin each stick gently around the outer rim 3 times.\nWhen you're finished, press the controller's A button or Right Dpad button.\n\n");
    // Collect stick samples, track min/max for each axis. Maybe Pull final result in ~1% of total possible range.
    min_lx = min_ly = min_rx = min_ry = 0xFFF;
    max_lx = max_ly = max_rx = max_ry = 0x000;
    while (buf_reply[3] != 0x08 && buf_reply[5] != 0x04) {
        res = hid_read_timeout(handle, buf_reply, sizeof(buf_reply), 200);
        if (res > 12) {
            u16 lx = buf_reply[6] | (u16)((buf_reply[7] & 0xF) << 8);
            u16 ly = (buf_reply[7] >> 4) | (buf_reply[8] << 4);
            u16 rx = buf_reply[9] | (u16)((buf_reply[10] & 0xF) << 8);
            u16 ry = (buf_reply[10] >> 4) | (buf_reply[11] << 4);
            min_lx = lx < min_lx ? lx : min_lx;
            max_lx = lx > max_lx ? lx : max_lx;
            min_ly = ly < min_ly ? ly : min_ly;
            max_ly = ly > max_ly ? ly : max_ly;
            
            min_rx = rx < min_rx ? rx : min_rx;
            max_rx = rx > max_rx ? rx : max_rx;
            min_ry = ry < min_ry ? ry : min_ry;
            max_ry = ry > max_ry ? ry : max_ry;


            printf("\rReading: L(%03X, %03X) R(%03X, %03X)   Min/Max L(%03X, %03X, %03X, %03X) R(%03X, %03X, %03Xm %03X)", lx, ly, rx, ry, min_lx, min_ly, max_lx, max_ly, min_rx, min_ry, max_rx, max_ry);
        }

    }

    printf("\r");


write_cal_label:
    u16 OUTER_PADDING = 0;

    if (!raw_calibration) {
        printf("\n\nWould you like to add a small outer deadzone? This will help your stick to not\nundershoot the circularity test's outer circle but will slightly increase overall circularity error.\n");
        printf("\ny/n: ");
        if (yesorno() == true) {
            OUTER_PADDING = 0x050;
            printf("\nOuter deadzone addded\n");
        }
    }
    left_cal.xmin  = min(0xFFF, min_lx + OUTER_PADDING);
    left_cal.ymin  = min(0xFFF, min_ly + OUTER_PADDING);
    right_cal.xmin = min(0xFFF, min_rx + OUTER_PADDING);
    right_cal.ymin = min(0xFFF, min_ry + OUTER_PADDING);

    left_cal.xmax  = max(0, max_lx - OUTER_PADDING);
    left_cal.ymax  = max(0, max_ly - OUTER_PADDING);
    right_cal.xmax = max(0, max_rx - OUTER_PADDING);
    right_cal.ymax = max(0, max_ry - OUTER_PADDING);

    u16 range_ratio_l = max(max_lx, max_ly) - min(min_lx, min_ly);
    u16 range_ratio_r = max(max_rx, max_ry) - min(min_rx, min_ry);

    printf("\n\nNew calibration values:\n\n");
    if (handle_ok == 1 || handle_ok == 3) {
        printf("Left Stick\nMin/Max: X(%03X, %03X) Y(%03X, %03X)\nRatio: %03X, Center (x,y): (%03lX, %03lX), Deadzone: %03X\n\n",
            left_cal.xmin, left_cal.xmax, left_cal.ymin, left_cal.ymax,
            range_ratio_l, left_cal.xcenter, left_cal.ycenter, left_deadzone);
    }
    if (handle_ok == 2 || handle_ok == 3) {
        printf("Right Stick\nMin/Max: X(%03X, %03X) Y(%03X, %03X)\nRatio: %03X, Center (x,y): (%03lX, %03lX), Deadzone: %03X\n\n",
            right_cal.xmin, right_cal.xmax, right_cal.ymin, right_cal.ymax,
            range_ratio_r, right_cal.xcenter, right_cal.ycenter, right_deadzone);
    }


    printf("\nWould you like to write this calibration to the controller? y/n:\n");
    if (yesorno() == false) {
        printf("No changes have been made. Press any key to quit.\n");
        getch();
        return 0;
    }




    printf("\nWriting calibration to controller...\n");
    // Left joycon uses left-stick cal address, Right joycon uses right-stick cal address!
    if (handle_ok == 3 || handle_ok == 1) {
        res = write_left_stick_calibration(left_cal);
        if (res != 0) {
            printf("Failed to write left stick calibration.\n");
        }
    }
    Sleep(100);
    if (handle_ok == 3 || handle_ok == 2) { // Only Pro controllers and right joycons have a right stick
        res = write_right_stick_calibration(right_cal);
        if (res != 0) {
            printf("Failed to write right stick calibration.\n");
        }
    }
    Sleep(100);

    u8 stick_params_left[3];
    u8 stick_params_right[3];
    
    stick_params_left[0] = left_deadzone & 0xFF;
    stick_params_right[0] = right_deadzone & 0xFF;
    
    stick_params_left[1] = (left_deadzone & 0xF00) >> 8 | ((range_ratio_l & 0xF) << 4);
    stick_params_left[2] = (range_ratio_l & 0xFF0) >> 4;

    stick_params_right[1] = (right_deadzone & 0xF00) >> 8 | ((range_ratio_r & 0xF) << 4);
    stick_params_right[2] = (range_ratio_r & 0xFF0) >> 4;

    // Save left stick params
    if (handle_ok == 3 || handle_ok == 1) {
        res = write_spi_data(0x6089, sizeof(stick_params_left), stick_params_left);
        if (res != 0) {
            printf("Failed to write left stick params.\n");
        }
    }
    // Save right stick params
    if (handle_ok == 3) {
        res = write_spi_data(0x609B, sizeof(stick_params_right), stick_params_right);
        if (res != 0) {
            printf("Failed to write right stick params.\n");
        }
    } else if (handle_ok == 2) {
        res = write_spi_data(0x6089, sizeof(stick_params_right), stick_params_right);
        // IDK why the right joycon uses left stick param address but right stick calibration address *shrug*
        if (res != 0) {
            printf("Failed to write right stick params.\n");
        }
    }

    Sleep(100);
    printf("COMPLETE!\n");
    if (raw_calibration) {
        printf("A max-range 'raw' calibration has been flashed. Please disconnect and reconnect your controller.\n");
        printf("Then go to Gampad Tester and run a circularity test with the top controller shell on.\n");
        printf("If the whole output range does not fit inside the circularity test circle, you need to adjust your magnet positions.\n");
        printf("Once as much of the stick output fits inside the circle as possible, re-run this tool with option 1 to run stick calibration\n\n");
    }
    else {
        printf("Calibration finished!\nNote: On PC, stick inputs may appear strange near the deadzone.\nPlease test center/deadzone on a Nintendo Switch.\n\n");
    }
    printf("Press any key to quit.\n");

    getch();

    return 0;
}
