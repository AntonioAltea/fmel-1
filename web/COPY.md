# What the web says

Every sentence on the site is yours. Each row is one thing to convey. Write the phrase in `ES` (and `EN` if you want
it in English too; empty: I leave the row out). Few words: the page shows one thing at a time and lights, ticks and
the live status say the rest.

Principles: a message only when something needs the reader (otherwise nothing: the "dark cockpit"); one action per
step, the next one appears when it can be done; the verb first; the keyboard's name, never "the device".

## Installer: the order of the page

1. Your keyboard (FM-1 / SMK-37, then the model)
2. Before you start (three ticks)
3. Find it → what it runs → Install → progress → Done
4. Folded: back to the factory firmware
5. Folded: if something goes wrong

The "Your browser" box and "What you will see" go away: the browser is only mentioned when it can't install, and the
live status says each step as it happens.

## Installer: the fixed texts

| id | where | what it has to convey | max words | ES | EN |
| --- | --- | --- | --- | --- | --- |
| title | top | install FMEL-1 | 4 | Instalar FMEL-1 ~ | |
| what | under the title | what FMEL-1 is: Mellotron tapes on your keyboard | 12 | Sonidos de Mellotron en FM-1 y SMK-37 | |
| sim | big button | try it first in the browser | 5 | Simulador web | |
| kb | step 1 heading | which keyboard | 3 | Tu teclado ~ | |
| model | under the SMK-37 models | pick the one written on yours: only that model's factory firmware goes back on later | 15 | Modelo de SMK-37 | |
| starry | next to SMK-37 Pro | the Starrykey 37 Play is this one | 4 | Similar a Starrykey 37 Play | |
| before | step 2 heading | before you start | 3 | Antes de empezar | |
| t-cable | tick 1 | a data cable, straight in, no hub | 7 | Conectado directamente por USB | |
| t-alone | tick 2 | close anything that uses MIDI (DAW, M-UPGRADE, other tabs) | 8 | Cierra otras pestañas o programas que pueden estar usando MIDI (DAW, M-UPGRADE, otras pestañas...) | |
| t-stock | tick 3, FM-1 | it runs the official firmware (or FMEL-1); another custom one: go back to the official first | 15 | Firmware oficial o FMEL-1 instalado | |
| t-stock-smk | tick 3, SMK-37 | it runs M-VAVE's V016 (or FMEL-1); older: update with M-UPGRADE; another custom one: back to M-VAVE's first | 18 | Firmware stock o FMEL-1 instalado | |
| t-plug | tick 4 | don't unplug it until it says Done | 8 | No lo desconectes hasta que diga «Hecho» | |
| find | button | find my keyboard | 3 | Encontrar teclado | |
| install | button | install | 2 | Instalar | |
| resume | button | finish the install that was cut off | 4 | Continuar instalación incompleta | |
| copy | button | copy the log | 3 |Copiar log | |
| report | link | tell us on GitHub | 4 | Reportar | |
| stock | folded heading | back to the factory firmware | 5 | Volver a firmware stock | |
| stock-fm1 | its text, FM-1 | choose M-VAVE's FM-1.fwsc (V15) here; only the untouched official file is accepted | 15 | | |
| stock-smk | its text, SMK-37 | with M-UPGRADE and your model's V016 file (links); your calibration and pairing stay | 15 | | |
| credits | foot | Felucca's installer; nothing leaves your computer | 12 | Instalador de Felucca | |

## Installer: what the live status says

Each appears only in its moment.

| id | when | what it has to convey | max words | ES | EN |
| --- | --- | --- | --- | --- | --- |
| no-browser | the browser has no Web MIDI | open this page in Chrome or Edge on a computer | 10 | Usa Chome o Edge en un ordenador | |
| denied | MIDI was refused | allow MIDI for this page (the lock by the address) and reload | 12 | Permite MIDE en esta página | |
| pkg-ok | the package checked | ready: FMEL-1 version, model | 5 |Comprobación correcta | |
| pkg-bad | the package fails its check | the file is damaged: reload; nothing will be written | 10 | Fichero corrupto | |
| pkg-none | no package for this model yet | not available yet for this model | 7 | Aún no hay versión para este modelo ~ | |
| looking | searching | looking… | 2 | Buscando… ~ | |
| not-found | nothing answers | not found: data cable? unplug, plug in, again | 10 | No se encuentra | |
| wrong-board | an FM-1 when SMK-37 is chosen (or the reverse) | this is an FM-1 / SMK-37: choose it above | 8 | | |
| official | runs the factory firmware | factory firmware (version): ready | 5 | Firmware oficial ({v}): listo ~ | |
| old-stock | SMK-37 older than V016 | update to V016 with M-UPGRADE first | 8 | | |
| is-mel | runs FMEL-1 | FMEL-1 (version): it can be updated | 6 | FMEL-1 ({v}): listo para actualizar ~ | |
| rescue | in the USB rescue | in the rescue: ready | 4 | En rescate: listo ~ | |
| custom | runs another custom firmware | another custom firmware: back to the factory one first | 10 | | |
| cut-off | in FMEL's update mode | an install was cut off: finish it | 7 | Instalación a medias: termínala ~ | |
| other-loader | in another firmware's update mode | another firmware's update: switch it off and on | 9 | | |
| ask | confirm before writing | install FMEL-1 for the (model)? don't unplug it | 9 | | |
| verify | the keyboard checks the package | checking… | 2 | Comprobando… ~ | |
| loader | it restarts into update mode | update mode… don't unplug it | 5 | Modo actualización… no lo desconectes ~ | |
| writing | writing, with % | writing N %… don't unplug it | 5 | Escribiendo {n} %… no lo desconectes ~ | |
| reboot | restarting | restarting… | 1 | Reiniciando… ~ | |
| done | done | done: you can unplug it | 5 | Hecho: ya puedes desconectarlo ~ | |
| lost | the cable came out | cut off: plug it in, find it, finish | 8 | | |
| no-return | it did not come back | switch it off and on, then find it again | 9 | | |
| why-* | under a disabled button | what is still missing (tick step 2 / find it / the file) | 6 | Falta: {lo que falte} ~ | |

## Installer: if something goes wrong (folded, one line each)

From what people hit on GitHub with the SMK-37's custom firmwares (Reddit could not be read from here).

| id | the problem | what it has to convey | max words | ES | EN |
| --- | --- | --- | --- | --- | --- |
| h-win | Windows: the browser freezes on «update mode» | close the browser (Task Manager), open the page, find it, install: it goes on | 14 | | |
| h-winrt | Windows, again | chrome://flags/#use-winrt-midi-api → Enabled | 3 | chrome://flags/#use-winrt-midi-api → Enabled ~ | |
| h-dark | dark or restarting after an install | hold OCT− and OCT+ 5 s (or OCT− while switching on): the rescue; install again | 15 | | |
| h-stuck | stuck in another firmware's update mode | switch it off and on | 5 | Apágalo y vuélvelo a encender ~ | |
| h-mupgrade | M-UPGRADE: «does not conform to the device» | the file must be your model's own | 8 | | |
| h-keys-c | SMK-37: every key a C, wheels dead | another firmware broke the key map; genkigenki's Repair (link); FMEL-1 never writes it | 14 | | |
| h-wheels | SMK-37: wheels dead after a repair | calibrate the wheels in M-VAVE's settings | 7 | Calibra las ruedas en los ajustes de M-VAVE ~ | |
| h-black | black screen, a USB disk «WL82 UBOOT1.00» | not lost: ask before touching anything (link) | 8 | | |
| h-daw | the DAW hears nothing after installing | restart the computer (the browser held the MIDI driver) | 9 | | |

## Simulators

| id | where | what it has to convey | max words | ES | EN |
| --- | --- | --- | --- | --- | --- |
| s-what | under the title | FMEL-1 in the browser, the same code as the keyboard | 10 | | |
| s-on | the start button | switch on | 2 | Encender ~ | |
| s-drawn | SMK-37 only | the panel is drawn to be clear, not to scale | 9 | | |
| s-hint | under the panel | click or the computer keyboard; drag knobs, faders, wheels | 10 | | |
| s-midi | button | connect MIDI | 2 | Conectar MIDI ~ | |
| s-reset | button | factory settings | 2 | Ajustes de fábrica ~ | |
| s-vel | SMK-37 slider | velocity | 1 | Velocidad ~ | |
| s-install | top link | install on the FM-1 / SMK-37 | 5 | Instalar en el {teclado} ~ | |
| s-manual | folded | manual (the controls table; its rows are names, not sentences) | 1 | Manual ~ | |
| s-tapes | foot | Bernie Kornowicz's M400S tapes, free, never sold; drawings by Javi; built on Dinghy and Felucca | 18 | | |
| s-licence | foot | GPL-3.0, source, report a bug, not M-VAVE's | 8 | | |
