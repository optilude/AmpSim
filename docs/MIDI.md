# MIDI Control

AmpSim accepts MIDI over its hardware MIDI IN and forwards the incoming byte
stream to MIDI OUT. This is designed for commercial loop switchers that send a
Program Change and several Control Change messages when a preset switch is
pressed.

## Receive Channel

Long-press the rotary encoder, select **MIDI Ch**, and choose channel 1-16 or
Omni. Channel 1 is the factory default. Omni accepts channel voice messages on
all channels.

The channel filter affects AmpSim control only. MIDI OUT forwards every input
byte regardless of the configured channel.

## Program Change

Program Change values 0-127 select capture indices 0-127 and enable the model
engine after the selected capture loads successfully. Values beyond the models
compiled into the current firmware are ignored locally and still forwarded.

MIDI uses zero-based program values, but many switchers label them 1-128. On
those devices, displayed program 1 selects AmpSim capture 0, displayed program
2 selects capture 1, and so on.

## Control Change

| CC | Parameter | Value mapping |
| --- | --- | --- |
| 16 | Input Gain | 0 = -20 dB, 64 = 0 dB, 127 = +20 dB |
| 17 | Output Volume | 0 = -20 dB, 64 = 0 dB, 127 = +20 dB |
| 18 | Reverb Mix | 0 = 0%, 64 = 50%, 127 = 100% |
| 19 | Bass | 0 = -12 dB, 64 = 0 dB, 127 = +12 dB |
| 20 | Mid | 0 = -12 dB, 64 = 0 dB, 127 = +12 dB |
| 21 | Treble | 0 = -12 dB, 64 = 0 dB, 127 = +12 dB |
| 23 | Reverb Enable | 0-63 = off, 64-127 = on |

CC22 is intentionally unassigned. Program Change does not alter the reverb
enable state; send CC23 when a switcher preset must set that state explicitly.

## Physical Knobs

After MIDI changes a parameter, that value remains active while its physical
knob stays still. Moving the knob beyond the normal control deadband immediately
returns the parameter to the knob's absolute position. This can create a value
jump and is intentional.

The six CC parameter values are not saved. After power-up, parameters follow
the physical knobs. The selected model, model/reverb enable states, and MIDI
receive channel are persisted.

## MIDI Thru

MIDI OUT is an always-on software thru. Forwarding happens before message
parsing, preserving channels, running status, realtime bytes, channel-mode
messages, and SysEx. Local channel filtering and unsupported Program/Control
messages do not remove them from the forwarded stream.

## Loop-Switcher Preset

Configure one stomp-switch preset to send, in this order:

1. A Program Change for the desired capture.
2. CC16-21 values for gain, volume, reverb mix, and EQ.
3. CC23 with 0 for reverb off or 127 for reverb on.

All messages must use the pedal's configured receive channel unless it is set
to Omni.