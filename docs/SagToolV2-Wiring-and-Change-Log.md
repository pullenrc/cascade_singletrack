# Cascade Singletrack · Sag Tool V2

**Reference revision** · XIAO ESP32-C6 hardware, firmware **2.0.10**. The web page subtitle is **Cascade Singletrack**. Use the Arduino sketch ZIP for upload and keep its header beside the .ino.

## Wiring

| XIAO pad | GPIO | Connection |
| --- | ---: | --- |
| D0 | 0 | Battery sense at the midpoint of two 100 kΩ resistors |
| D1 | 1 | ZERO button to ground |
| D2 | 2 | MODE button to ground |
| D3 | 21 | Buzzer module TRIG/PWM input |
| D4 | 22 | Shared I²C SDA to MT6701 and SSD1306 |
| D5 | 23 | Shared I²C SCL to MT6701 and SSD1306 |
| D10 | 18 | NeoPixel DIN, direct 3.3 V data |

**Power and sense.** The 18350 cell connects to B+ / B− on the LX-LCBST. The adjustable VO+ / VO− output supplies the tool. The drawings show a nominal 5 V rail and retain the earlier two-pole switch and ADC filter layout as reference. Confirm the actual switch contacts and filter on your unit. The divider midpoint goes to D0 and reads half the raw cell voltage. Charge through the LX-LCBST USB-C port. Verify boost voltage at VO+ before connecting the XIAO, and disconnect VO+ from XIAO 5V before using XIAO USB for programming because the external feed has no Schottky isolation. [Seeed XIAO ESP32-C6 pin map](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/#pin-map)

**NeoPixel.** D10 / GPIO18 goes directly to DIN, with a shared ground. Verify that replacement pixels accept 3.3 V data at their supply voltage. The code uses GRB at 800 kHz and brightness 25/255. Encoder read faults and lost tracking override the mode color. [Adafruit logic-level guidance](https://learn.adafruit.com/adafruit-neopixel-uberguide/logic-level)

**Buzzer.** D3 drives the trigger input, with the module signal ground tied to XIAO ground. The module's load output powers the buzzer. The drawing uses functional terminal names because the exact module model and buzzer voltage rating are unconfirmed. The current sketch assumes active-high logic and uses 4 kHz hardware PWM at 50% duty. MODE and named web mode selections beep for 200 ms, ZERO for 70 ms, and capture for 140 ms. The loop stops the beep by elapsed time while encoder sampling continues. Set `BUZZER_USE_PWM` to `false` to compare against steady drive.

## Firmware changes

- **2.0.4** kept the I²C bus at 400 kHz and added tracking-gap diagnostics. A gap over 50 ms or an exactly half-turn step invalidates tracking. Return to the physical home mark before pressing ZERO when the display shows REZERO.
- **2.0.5** split each OLED update into transfers of up to 31 data bytes, with an encoder read between transfers. The 738 mm pull test reduced the reported maximum gap from about 35 ms to 8 ms and returned close to zero.
- **2.0.6** enabled timed, nonblocking buzzer output. The reported maximum gap was 12 ms with no observed measurement anomalies.
- **2.0.7** added buzzer feedback to the named web mode buttons and set the mode pulse to 200 ms.
- **2.0.8** added hardware PWM. An audible but slightly distorted beep was reported during PWM testing. The workspace sketch now uses 4 kHz, and the web page reads **Cascade Singletrack**. The result at 4 kHz and the exact as-built power connections are unconfirmed.
- **2.0.9** adds a 500 ms warning at a reported battery level of 5% or below, rearmed at 8%, and a 300 ms reminder after 20 minutes of inactivity. About 1 mm of reel movement, ZERO, or a mode selection restarts the inactivity timer.
- **2.0.10** changes low battery to three short beeps every minute and inactivity to two short beeps every five minutes. ZERO or MODE acknowledges low battery until it rearms at 8%. Movement, ZERO, or mode selection stops and resets the inactivity reminder.

The OLED shows battery percentage beside voltage on the bottom row. The web page shows battery percentage and voltage above the sag reading and firmware version below Wi-Fi Settings. Battery percentage is estimated from terminal voltage.
