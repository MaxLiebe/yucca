# Hardware test: <target>

Firmware version, console model and firmware, controller used, board.

| # | Step | Expected | Result |
| --- | --- | --- | --- |
| 1 | Flash, boot, put the controller in pairing mode | Log shows the controller ready | |
| 2 | Start sync on the console | Console accepts the adapter | |
| 3 | Every button, stick and trigger | Each reaches the console as mapped | |
| 4 | Rumble / LEDs from the console | Controller rumbles / LEDs follow | |
| 5 | Power-cycle controller, then adapter, then console | Reconnects without re-pairing | |
| 6 | Leave idle 10 minutes, then use | Still connected, no stuck inputs | |

File the result with the "Hardware test report" issue template.
