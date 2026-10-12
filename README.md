# FMEL-1

This is a personal project to create a few sounds I'm interested in for the M-VAVE FM-1.

This firmware, based on [zvenson's dinghy](https://github.com/zvenson/dinghy), aims to reproduce Mellotron sounds and
color them with some basic FX and lo-fi tape effects. It also lets you mix two different Mellotron sounds together.

## The tapes are not included

FMEL-1 plays real Mellotron M400 tapes: the samples Bernie Kornowicz (Taijiguy) recorded from his M400S. They are his
work, not mine and not under this repository's GPL licence: he shares them free to use, but not in commercial
software. So they are not in this repository.

To build FMEL-1, fetch them first (from his old site, via the Wayback Machine):

    tools/get_tapes.sh        # into tapes/, which git ignores

A build (`mel.fwsc`, or the web version) carries the tapes inside it: keep it for yourself, don't share or sell it.

## Credits

- Drawings by my friend Javi
- [Dinghy](https://github.com/zvenson/dinghy) by zvenson: the firmware FMEL-1 grew out of
- [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments: the FM-1
  platform underneath
- [SLOOP](https://github.com/isod89/sloop-fm1) by isod89: its changes to that platform
- [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) by Leo Kuroshita: the rescue, if an update goes
  wrong
- The Mellotron M400S tapes by Bernie Kornowicz (Taijiguy), from his old site <http://leisureland.us> (see above)
- The browser version follows the emulators of X0X, Felucca and FoMni
- [Terminus](https://terminus-font.sourceforge.net) by Dimitar Zhekov: the screen's font (SIL OFL 1.1)
- [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK): the boot files a build is packaged with
