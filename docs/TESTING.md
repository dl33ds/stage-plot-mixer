# Hardware Testing Guide: spm-diag

`spm-diag` is a small diagnostics program. It checks that your PC and audio interfaces work before the full mixer is built. It lists every audio driver, device and channel, checks the FireWire card, and runs two tests. Everything it shows is saved to a report file for the developers.

It does **not** change any settings on your computer or interface.

---

## 1. Download

1. Go to <https://github.com/dl33ds/stage-plot-mixer/actions> and click the most recent run with a green tick.
2. Scroll to **Artifacts** and download **spm-diag-windows-x64**.
3. Unzip it into a folder (for example `C:\StagePlotMixer\`).

The first time you run it, Windows may show *"Windows protected your PC"*. The program isn't code-signed yet. Click **More info**, then **Run anyway**.

## 2. Before testing

- Install the interface's own driver (for the Scarlett Solo: the Focusrite driver from focusrite.com).
- Close any other audio programs (DAWs, Focusrite Control is fine to leave open).
- Plug the interface in **before** starting spm-diag.

## 3. Test A: Device listing and stability (about 2 minutes)

1. Double-click **spm-diag.exe**. It lists your system and all audio devices.
2. At the menu, type `1` (Stability & input level test) and press Enter.
3. Choose the **ASIO: Focusrite USB ASIO** device. It's listed first and marked `<- recommended`, so pressing Enter picks it.
   Don't choose a **Windows Audio** entry for this test. Those go through the Windows mixer, ignore the buffer size, and are expected to glitch (that's Test C).
4. Press Enter to accept 48000 Hz, the default buffer size and 60 seconds.
5. While it runs, **speak into the mic or play into the inputs**. The `in:` meters should move:
   `.` silent, `-` quiet, `=` medium, `+` loud, `#` very loud, `X` clipping.
6. Repeat the test with buffer sizes **128** and **64**.

**What we're looking for:** `Stability test: PASS`, 0 late callbacks, and each input showing a level when used. If a result isn't PASS, the line above it gives the reason. `CLIPPED` means the input gain is too high; it doesn't cause a FAIL.

## 4. Test B: Round-trip latency (about 1 minute)

This measures the true delay through the interface. You need **one cable** from an output back into an input.

**Scarlett Solo 3rd Gen setup:**
- Cable from the **left RCA line output** on the back → **Input 2** (the ¼" jack) on the front.
  (An RCA-to-¼" cable or an adapter works.)
- **INST** button on input 2: **off**. Input 2 gain: about a quarter turn.
- **Direct Monitor** button: **OFF**. This is important. If it's on, the result is wrong.
- **Turn your headphones and speakers down.** The test plays short bursts of noise.

Then:
1. Run spm-diag, type `2` at the menu, and choose the Focusrite ASIO device.
2. Enter **48000**, buffer **128**, output channel **1**, input channel **2**.
3. Type `y` when asked if you're ready.

**What we're looking for:** five runs with the same (or ±1) sample count, and `Latency test: PASS`. If it says *no clear signal*, turn the input 2 gain up a little and try again.

## 5. Test C: Windows system audio (optional)

Run test A again, but choose a **Windows Audio** device (for example your speakers or built-in microphone). This checks the non-ASIO path.

## 6. FireWire interfaces (later)

When the M-Audio FireWire 1814 or ProFire Lightbridge are available:
1. Install their drivers, connect them, and power them on before starting spm-diag.
2. Run tests A and B on each one. For the Lightbridge, connect ADAT sources if you can, so all 32 inputs can be checked.
3. The report's **FireWire (IEEE 1394)** section shows the FireWire card and its chipset. Texas Instruments is the best.

## 7. Sending results

Reports are saved to **Documents\StagePlotMixer\Diagnostics\** as `spm-diag-<date>-<time>.txt`. The exact path is shown at the end of each run.

To send them, open a new issue at <https://github.com/dl33ds/stage-plot-mixer/issues> and drag the report files into it.

> The repository is **public**, so anyone can read issues. Reports contain device names and hardware IDs but no personal files or passwords. Open the file in Notepad first if you'd like to check what's in it.

## Command-line use (advanced)

```
spm-diag --list
spm-diag --test stability --type ASIO --device "Focusrite USB ASIO" --buffer 64 --seconds 300
spm-diag --test latency --type ASIO --device "Focusrite USB ASIO" --out 1 --in 2
spm-diag --help
```
