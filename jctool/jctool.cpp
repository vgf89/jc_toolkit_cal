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

void decode_stick_params(u16 *decoded_stick_params, u8 *encoded_stick_params) { 
    decoded_stick_params[0] = ((encoded_stick_params[1] & 0xF) << 8) | encoded_stick_params[0];
    decoded_stick_params[1] = (encoded_stick_params[2] << 4) | ((encoded_stick_params[1] & 0xF0) >> 4);
}

void encode_stick_params(u8 *encoded_stick_params, u16 *decoded_stick_params) {
    encoded_stick_params[0] =  decoded_stick_params[0] & 0xFF;
    encoded_stick_params[1] = ((decoded_stick_params[0] & 0xF00) >> 8) | ((decoded_stick_params[1] & 0xF) << 4);
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
    // Pack 12 bit (within 16 bit) data_l array back into 8 bit left_stick_cal array
    u8 left_stick_cal[0x9] = { 0 };
    encode_stick_params(&left_stick_cal[0], &data_l[0]);
    encode_stick_params(&left_stick_cal[3], &data_l[2]);
    encode_stick_params(&left_stick_cal[6], &data_l[4]);
    // Print left_stick_cal
    //printf("\nLeft stick calibration data: %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", left_stick_cal[0], left_stick_cal[1], left_stick_cal[2], left_stick_cal[3], left_stick_cal[4], left_stick_cal[5], left_stick_cal[7], left_stick_cal[8]);
    // Write out to SPI
    return write_spi_data(0x603D, sizeof(left_stick_cal), left_stick_cal);
}

int write_right_stick_calibration(struct DECODED_FACTORY_STICK_CAL right_cal) {
    u16 data_r[6] = { 0 };
    // Param order is different for right stick
    data_r[0] = right_cal.xcenter;
    data_r[1] = right_cal.ycenter;
    data_r[2] = right_cal.xcenter - right_cal.xmin;
    data_r[3] = right_cal.ycenter - right_cal.ymin;
    data_r[4] = right_cal.xmax - right_cal.xcenter;
    data_r[5] = right_cal.ymax - right_cal.ycenter;

    u8 right_stick_cal[0x9] = { 0 };
    encode_stick_params(&right_stick_cal[0], &data_r[0]);
    encode_stick_params(&right_stick_cal[3], &data_r[2]);
    encode_stick_params(&right_stick_cal[6], &data_r[4]);
    // Print right_stick_cal
    //printf("\nRight stick calibration data: %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", right_stick_cal[0], right_stick_cal[1], right_stick_cal[2], right_stick_cal[3], right_stick_cal[4], right_stick_cal[5], right_stick_cal[7], right_stick_cal[8]);
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

    u16 range_ratio_l = 0xE14;//max(max_lx, max_ly) - min(min_lx, min_ly); // 0xE14;
    u16 range_ratio_r = 0xE14;//max(max_rx, max_ry) - min(min_rx, min_ry); // 0xE14;

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

    u8 stick_params_left[3];
    u8 stick_params_right[3];
    
    u16 left_params[2] = { left_deadzone, range_ratio_l };
    u16 right_params[2] = { range_ratio_r, right_deadzone}; // These are swapped, exactly like the calibration data itself. Undocumented behavior.
    
    encode_stick_params(&stick_params_left[0], &left_params[0]);
    encode_stick_params(&stick_params_right[0], &right_params[0]);

    // Save stick params
    if (handle_ok == 1) { // Left joycon
        memcpy(&right_cal, &left_cal, sizeof(DECODED_FACTORY_STICK_CAL));
        memcpy(stick_params_right, stick_params_left, sizeof(stick_params_right));
    } else if (handle_ok == 2) { // Right joycon
        memcpy(&left_cal, &right_cal, sizeof(DECODED_FACTORY_STICK_CAL));
        memcpy(stick_params_left, stick_params_right, sizeof(stick_params_left));
    }
    res = write_right_stick_calibration(right_cal);
    res += write_spi_data(0x609B, sizeof(stick_params_right), stick_params_right);
    Sleep(100);
    res += write_left_stick_calibration(left_cal);
    res += write_spi_data(0x6089, sizeof(stick_params_left), stick_params_left);
    Sleep(100);
    if (res != 0) {
        printf("ERROR: Failed to write right stick params!\n");
    } else {
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
    }

    getch();

    return 0;
}
