# Switch Controller Calibrator

This is currently a fork of https://github.com/CTCaer/jc_toolkit and turns it into a simple command line tool for calibrating Switch controllers.

BEFORE USING THIS TOOL, ***PLEASE BACKUP YOUR SPI FLASH*** using the original https://github.com/CTCaer/jc_toolkit. If you ever want to revert changes made by this calibration tool, you will need to restore from your spi flash backup.

## Important warning:

The center/deadzone settings that are stored in the controller firmware appear to not be used correctly by Windows, so stick behavior near the center may appear strange in PC games and gamepad tester. After running this calibration tool, please re-sync your controller to a Switch and test there instead. You may need to wipe the user calibration in `Settings -> Controllers and Sensors -> Calibrate Control Sticks -> Tilt the Stick... -> Back to Default Settings` and then re-sync the controller.

If you want to use your controller on a Windows PC, you might not want to use this calibration tool. This is a big reason to ***BACKUP YOUR SPI FLASH FIRST***

## Hall Effect stick users:

These sensors are often not well centered from the factory, and the ranges are very different from stock Alps sticks.

To fully calibrate your sticks, flash the RAW calibration first, then use Gamepad Tester's circularity test (with the controller's top shell on!) to see if the entirety of your stick range fits inside the circle. If it does not fit, and the results appear rather assymetrical, then you need to adjust your magnets. Making the stick range entirely fit takes a bit of fiddling, just try to get it as close as you can.

Do not worry about where the stick center appears in gamepad tester, all you want is for the range to fit.

Once your range (mostly) fits, re-run the calibration tool and do the full calibration, carefully reading the instructions it provides.


## Final Warning:

This application is provided with no warranty and no guarantees.

Again, please ***BACKUP YOUR SPI FLASH FIRST USING CTCaer's JOYCON TOOLKIT***.

## References:

**Official forum** for Joycon Toolkit (use this to backup your SPI Flash): https://gbatemp.net/threads/tool-joy-con-toolkit-v1-0.478560/

**Protocol reverse engineering**: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering

**Protocol and hidapi usage in Linux**: https://github.com/shinyquagsire23/HID-Joy-Con-Whispering

**In windows**: https://github.com/shuffle2/nxpad
