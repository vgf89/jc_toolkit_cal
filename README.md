# BEFORE USING THIS TOOL
***BACKUP YOUR SPI FLASH*** using https://github.com/CTCaer/jc_toolkit. If you ever wish to revert changes made by this calibration tool, you will need to restore from your spi flash backup.

# Table of Contents
- [About](#About)
- [Warning for PC controller usage](#Warning-for-PC-controller-usage-(and-doing-checks-in-Gamepad-Tester))
- [Hall Effect Sticks and Pro Controllers](#Hall-Effect-Sticks-and-Pro-Controllers)
- [Hall Effect Sticks and Joycons](#Hall-Effect-Sticks-and-Joycons)
- [FINAL WARNING](#FINAL-WARNING)


# About

This tool can overwrite the ***FACTORY CALIBRATION*** of Switch Pro Controllers and Joycon controllers. It includes two modes:

1. Guided calibration wizard
    - Lightly wiggle the stick around its center. This sets the X/Y center and deadzone calibration.
    - Spin the sticks around the edges. This sets the min/max for the X/Y axes.
    - Choose whether you want to add some padding to the min/max value (to prevent undershoot i.e. in Gamepad Tester)
2. Raw calibration flasher
    - No deadzone
    - Maximum stick range (your stick will not reach all edges)
    - Useful for centering new Pro Controller sticks -- so that their full range can be detectable -- before using the guided calibration wizard.

Special thanks to https://github.com/CTCaer/jc_toolkit, which is the basis of this tool.

## Warning for PC controller usage (and doing checks in Gamepad Tester)

The center/deadzone settings that are stored in the controller firmware appear to not be read quite correctly by Windows, as if they are applied in the wrong order, so stick behavior near the center may appear strange in PC games and Gamepad Tester. After running this calibration tool, please re-sync your controller to a Switch and test there instead.

On Switch, you may need to wipe the user calibration in `Settings -> Controllers and Sensors -> Calibrate Control Sticks -> Tilt the Stick... -> Back to Default Settings` and then re-sync the controller for the new factory calibration to be properly applied.

If you want to use your controller on a Windows PC, you might not want to use this calibration tool. This is a big reason to ***BACKUP YOUR SPI FLASH FIRST***

## Hall Effect Sticks and Pro Controllers

These sensors are often not well centered from the factory, and the ranges are different from stock Alps sticks.

To fully calibrate your sticks, flash the RAW calibration first, then use Gamepad Tester's circularity test (with the controller's top shell on!) to see if the entirety of your stick range fits inside the circle. If it does not fit, and the results appear rather assymetrical, then you need to adjust your magnets. Making the stick range entirely fit takes a bit of fiddling, just try to get it as close as you can.

Do not worry about where the stick center appears in gamepad tester, all you want is for the range to fit.

Once your range (mostly) fits, re-run the calibration tool and do the full calibration, carefully reading the instructions it provides.

## Hall Effect Sticks and Joycons

Some of these sensors are so badly centered that their raw output is *extremely* lopsided and maxes out far too early in some directions. While this calibration tool can correct the center and deadzone of these sensors, and generally ensures that the output range is set properly, it will not help the accuracy much nor fix the overshooting problems of such badly centered sticks.

## FINAL WARNING

***Again, please BACKUP YOUR SPI FLASH FIRST USING CTCaer's JOYCON TOOLKIT***.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## References:

**Joycon Toolkit** https://gbatemp.net/threads/tool-joy-con-toolkit-v1-0.478560/

**Protocol reverse engineering**: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering

**Protocol and hidapi usage in Linux**: https://github.com/shinyquagsire23/HID-Joy-Con-Whispering

**Some other stuff**: https://github.com/shuffle2/nxpad
