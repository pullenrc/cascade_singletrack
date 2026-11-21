# Sag Tool V2 firmware · 2.0.10

Open `SagToolV2_XIAO_ESP32C6.ino` in Arduino IDE. Keep `SagToolV2StatusPixel.h` beside it in the matching sketch folder.

1. In Boards Manager, install **esp32 by Espressif Systems**. Select **XIAO_ESP32C6** and the board's USB port.
2. In Library Manager, install **Adafruit SSD1306**, **Adafruit GFX Library**, and **Adafruit NeoPixel**. Accept the dependency prompts for **Adafruit BusIO**.
3. Click **Verify**, then **Upload**. Disconnect LX-LCBST VO+ from XIAO 5V before connecting XIAO USB. The external feed has no Schottky isolation.
4. Open Serial Monitor at **115200 baud**. The startup output reports encoder, OLED, and calibration status. The OLED and the SagTool web page show firmware version and estimated battery charge. The web subtitle reads **Cascade Singletrack**. If the battery sense switch is open during USB power, the OLED shows `--%`.

| Function | XIAO pad | GPIO |
| --- | --- | --- |
| Battery divider midpoint | D0 | 0 |
| ZERO button | D1 | 1 |
| MODE button | D2 | 2 |
| Active buzzer trigger | D3 | 21 |
| I²C SDA | D4 | 22 |
| I²C SCL | D5 | 23 |
| NeoPixel DIN, direct | D10 | 18 |

The 18350 cell uses an LX-LCBST charge/boost module. Verify its adjustable output is set to 5 V before connecting the tool. The two 100 kΩ resistors give a nominal divider gain of 2.0. Compare reported battery voltage with a meter during bench setup. Use the existing calibration workflow for the installed V2 drum. The SagTool access point uses `sagtool123` and serves the interface at `http://192.168.4.1`.

This bench build was ported from the recovered older `sag_tool_v3.ino`. The original XIAO alpha1 sketch was unavailable. Earlier builds were bench-tested on the device. PWM produced an audible but slightly distorted beep during testing. The workspace sketch uses 4 kHz, and the result at that setting is unconfirmed.

## Active buzzer

The buzzer is enabled. Connect D3 to the trigger board's 3.3 V-compatible TRIG/PWM input with a shared signal ground. The trigger board switches power to the active buzzer. The sketch assumes an active-high trigger. The `BUZZER_ACTIVE_HIGH` setting controls the polarity.

This test build uses 4 kHz hardware PWM at 50% duty. Set `BUZZER_USE_PWM` to `false` for the steady trigger, or change `BUZZER_PWM_HZ` to test a different pitch. The trigger remains inactive between beeps.

MODE and the named web mode buttons use a 200 ms burst. ZERO and capture use 70 ms and 140 ms bursts. The main loop checks the elapsed time and turns the output off while encoder sampling continues. Check that the buzzer is silent at idle, then confirm the encoder gap and return to zero with the buzzer connected.

- **Low battery** starts at a reported 5% or below. Three short beeps repeat once a minute. ZERO or MODE acknowledges the alert until the reported charge reaches 8%. An unavailable battery reading pauses the alert.
- **Inactivity** starts after 20 minutes. Two short beeps repeat every five minutes. About 1 mm of reel movement, ZERO, or a mode selection stops the reminder and restarts the timer.

## Long-pull bench check

1. Select **LIVE**, bring the string to a repeatable home position, and press **ZERO**. Watch Serial Monitor at **115200**.
2. Pull the full 738 mm and retract to the same home mark at a controlled pace. Record the extended distance, return-to-zero error, **Encoder gap max**, and **Read errors**. Repeat at your normal speed with the web page open.
3. If the OLED shows **REZERO**, return to the physical home position before pressing **ZERO**. A sample gap over 50 ms or an exactly half-turn angle step invalidates the multi-turn reference. A failed read interrupts the stability window.
4. Repeat in **RIDER** mode. Keep the string moving through the pull, then hold it still to test capture separately.

The OLED and encoder share 400 kHz I²C. The OLED schedules a frame every 100 ms and sends it in chunks of at most 31 bytes per loop. Serial Monitor reports signed turns from zero and calibrated millimeters per turn. The time-gap check flags long interruptions, and tracking still requires the reel to move less than half a turn between successful samples.
