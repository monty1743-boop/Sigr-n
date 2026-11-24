# Sigrún

A pocket glass for a hard minute. Sigrún is a valkyrie; her name means victory rune. This one is a smaller kind of victory: the minute she stays.

It runs on a Waveshare ESP32-S3-Touch-AMOLED-1.75C. The round panel is always one breath. Picking it up is the practice. A short press of PWR, or BOOT, is three cyclic sighs, and then the breath is back. There is no menu in her hand.

Lists, a task, feeling words, and a soft noise are decisions, so they belong on the charger, one card at a time. That catalog is not in this first firmware. What is here is the picture, the sigh, and the rules for what may edit the picture. Those rules are tested on the host.

## The picture

True black in the corners, so those AMOLED pixels are off. The ring is drawn in floating point, given a short trail, and dithered down to the panel's RGB565 so the bloom does not band into stripes. A task set at home puts the breath inside a moon; darkness eats the moon with a wide dusk, not a clock hand.

![The everyday breath](docs/proof/tide.png)

![The top of a sigh](docs/proof/sigh.png)

![The long exhale](docs/proof/exhale.png)

![The moon, half gone](docs/proof/moon.png)

The minute of motion is [docs/proof/sigrun.mp4](docs/proof/sigrun.mp4). It is the same framebuffer the panel gets: the breath, three sighs, the moon, a walking pace, a sway, a still hand, and a warmer exhale.

## Host

The preview draws that framebuffer on this computer. No board required.

```bash
cd preview
make test
./preview still 4500 out/tide.ppm
./preview reel | ffmpeg -y -f rawvideo -pix_fmt rgb24 -s 466x466 -framerate 30 -i - \
  -c:v libx264 -pix_fmt yuv420p -crf 17 ../docs/proof/sigrun.mp4
```

`make test` checks the rules: a rescue is the sigh, a second rescue inside a minute is the grounding script, walking does not cancel a sigh, a traced circle cannot go faster than about eight breaths a minute, and a finger held down waits at the bottom of the exhale.

## On the board

ESP-IDF 5.5 or newer. From `firmware/`:

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

The panel backlight stays low (35). PWR and BOOT, short press, start the sighs. A long press of PWR still belongs to the power chip. Hold BOOT only while the chip is resetting if you mean to enter the ROM downloader.

The QMI8658, the microphones, and the touch path are in the plan and in the host rules. This firmware does not read them yet. The glass on the desk breathes, and it sighs.

## What it is not

It does not treat ADHD. It does not replace care. An episode that feels medical needs a person.
