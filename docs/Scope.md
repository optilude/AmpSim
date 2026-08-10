# Guitar effects pedal with NAM and Reverb

The goal of this project is to build a guitar effects pedal that can process a mono guitar signal using a Neural Amp Modeller Architecture 2 (NAM A2) capture, optionally an Impulse Response (IR) of a cabinet (if not built into the NAM capture), and apply a plate reverb to the output, which could be line level and either mono or stereo.

The main processing will happen on an Electro Smith Daisy Seed 3 module. This will be hosted on a 125B-sized PCB created by `bkshepherd`. This has two momentary footswitches, two LEDs, an LCD screen, a rotary encoder, and six potentiometers.

At a basic level, the user experience should be:

- Plug a guitar into the input (mono).
- Select an amp/cab combination using the rotary encoder, shown on the display.
- Use the knobs to adjust amp input gain, output volume, reverb mix, and bass/middle/treble EQ.
- Plug the output into a speaker or mixer.

Amp and cab models (NAM captures and optionally a related cabinet IR, if using an amp-head only NAM capture) are pre-loaded onto the device at compile time. There is no user-interface to load additional items.

In due course, there should be a "settings mode" (e.g. entered by long-pressing one or both foot switches), where parameters can be modified using the rotary encoder, knobs, and switches, and then persisted to the device. This might include whether to send stereo or mono output, and perhaps configure certain reverb or amp/cab parameters.

## Prior art

The hardware unit was designed originally to be a multi-fx pedal platform. The code for this is here: https://github.com/bkshepherd/DaisySeedProjects/tree/main/Software/GuitarPedal. This supports multiple hardware targets, but we just want the 125B build. It should contain the code necessary to control the knobs, encoder, buttons, LED, and screen. Some of the effects may also be useful later as we expand the capabilities of the pedal. There is also an Impulse Response loader.

The MuleBox is a separate project that uses the Daisy Seed on a different PCB platform (in this case, the Cleveland Music Co Hothouse). The code for this is here: https://github.com/optilude/mulebox. This is relevant because it processes a mono guitar signal through a cabinet IR (the provencence of which is the bkshepherd's DaisySeedProjects!) and then a plate reverb on a single "mix" knob (the Dattorro reverb). This project should adopt the default settings and behaviour of that reverb. The MuleBox repository also contains the patterns for loading large files (IRs) into the right region of memory on the Daisy Seed, which the default flashing tool will not do.

Finally, the Flick is a Daisy Seed-based pedal that includes the plate reverb as well as other effects. Its code is here: https://github.com/joulupukki/Flick. The Flick was the basis for the reverb in the MuleBox, but it also contains patterns for handling multiple modes by short, long, and multi-pressing buttons, and other useful abstractions.

## Links

- [Daisy Seed](https://daisy.audio/products/seed3)
- [Daisy Seed documentation](https://docs.daisy.audio/)
- [Neural Amp Modeller](https://www.neuralampmodeler.com)
- [Tone 3000 NAM library](https://www.tone3000.com)
- [bkshepherd 125B hardware](https://github.com/bkshepherd/DaisySeedProjects/blob/main/Hardware/GuitarPedal125b/README.md)
- [bkshepherd multi-fx software](https://github.com/bkshepherd/DaisySeedProjects/blob/main/Software/GuitarPedal/README.md)
- [MuleBox code](https://github.com/optilude/mulebox)
- [Flick code](https://github.com/joulupukki/Flick)