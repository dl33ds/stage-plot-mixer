# Hardware Testing Guide

The download contains two programs:

- **Stage Plot Mixer.exe**: the mixer itself: a node editor where you wire inputs through faders, pans and buses to outputs (Test D).
- **spm-diag.exe**: a small diagnostics program (Tests A–C).

`spm-diag` is a small diagnostics program. It checks that your PC and audio interfaces work before the full mixer is built. It lists every audio driver, device and channel, checks the FireWire card, and runs two tests. Everything it shows is saved to a report file for the developers.

It does **not** change any settings on your computer or interface.

---

## 1. Download

1. Go to <https://github.com/dl33ds/stage-plot-mixer/actions> and click the most recent run with a green tick.
2. Scroll to **Artifacts** and download **stage-plot-mixer-windows-x64**.
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

## 6. Test D: The mixer, about 20 minutes (Stage Plot Mixer.exe)

This checks the node editor and that the audio engine runs cleanly on your interface.

**Setup**

1. Close spm-diag and any other audio programs, then double-click **Stage Plot Mixer.exe**.
2. The status bar at the bottom should show **Focusrite USB ASIO (ASIO)**. If it doesn't, click **Audio settings**, choose type **ASIO** and device **Focusrite USB ASIO**.
3. In **Audio settings**, set the sample rate to **48000** and the buffer size to **128**, then close the settings window.
4. You should see a ready-made mix: **Input 1** and **Input 2** each go through a **Fader** and a **Pan** into the **Master Bus**, then the **Master Fader** and **Main Out** (outputs 1–2).

**Sound**

5. Turn your headphones or speakers **down**. Outputs start **muted** (the red **Outputs muted** button, top right). Click it; the sound fades in.
6. Speak or play into the inputs. The meters on each node move, and the wires glow brighter with signal. You hear the inputs on outputs 1–2.
   If you hear yourself twice (slightly delayed), turn the Scarlett's **Direct Monitor** off.
7. Drag the **Level** bar on **Fader 1** and the **Pan** bar on **Pan 1**. Double-click a bar to reset it. Click **Mute**. All of these should be smooth and click-free.

**Editing** (it's fine to break the mix; **New** starts again)

8. Click a node: its settings appear on the right. Click a wire: you can set its gain there.
9. Press **Tab** (or double-click empty space), type `gain`, press **Enter**. A Gain node appears.
10. Drag from an output dot (right side) to an input dot (left side) to connect. Drag a wire off an input dot to move or remove it. Dropping a wire on empty space offers a node to add, already connected.
11. Move nodes by dragging their title. Drag on empty space to select several. **Delete** removes, **Ctrl+D** duplicates, **Ctrl+Z** undoes.
12. Pan with right-drag, zoom with the mouse wheel, press **F** to fit everything.
13. Click **Save**, give it a name, close the program, and open it again. Your session should come back.

**Stability**

14. Click **Reset counters**, then leave it running for **10 minutes** (you can keep playing into it). Use the PC normally but don't open other audio programs.
15. Click **Copy report**, and paste the text into a new issue (or a Notepad file).

**What we're looking for:** `Verdict: PASS`, meaning 0 late, 0 overloads and 0 driver dropouts over the 10 minutes, and no clicks or dropouts heard. Also tell us:
- whether muting, fader moves and rewiring were click-free,
- the CPU figures shown in the status bar,
- anything that was confusing, looks wrong, or is hard to read (screenshots help).

To try the mixer without an interface, choose the **Simulated** device type in Audio settings. It has 32 inputs carrying quiet test tones.

## 6b. Test E: Recording, about 20 minutes (Stage Plot Mixer.exe)

This checks that takes are recorded cleanly, completely and in sync.

**Setup**

1. Start **Stage Plot Mixer.exe** with the Scarlett on **ASIO, 48000, 128** as in test D. Click **New** for a fresh mix, then **Save** it (for example as `RecordTest`). Takes go in a **Takes** folder next to the session.
2. Press **Tab**, type `recorder`, press **Enter**. In its settings on the right, set **Channels** to **1**, then wire **Input 1**'s output into it. This is a *raw* recorder: with Format and Files on **Auto** it records 24-bit.
3. Add a second Recorder and wire the **Master Fader**'s output into it. This is a *mix* recorder: it records 32-bit float, stereo.
4. Rename them by double-clicking their titles, for example `Vocal raw` and `Mix`. Both should show a red **Armed** button.

**Recording**

5. Next to **Record** (top right) you should see how much recording time the disk has. Click **Record** (or press **Ctrl+R**). It turns into a red **Stop** button, and a timer runs.
6. Speak or play into input 1 for about a minute. Press **M** a couple of times at moments you'll recognise (for example, say "marker" out loud as you press it).
7. While it records, move the fader and pan, and try to change a Recorder's **Channels** (it should be greyed out).
8. Click **Stop**. The button shows *Saving…* briefly.
9. Open the menu next to **Marker** (the down arrow) and choose **Open takes folder**. Open the newest take folder. It should contain `Vocal raw.wav`, `Mix.wav` and `take.json`.
10. Load both WAVs into a DAW or Audacity, lined up at the start. The vocal should line up exactly with the mix. In a DAW that reads BWF markers (Reaper, for example), the markers should be where you pressed M.

**Longer take and pre-roll**

11. In the menu, set **Pre-roll** to 10 seconds. Talk for a few seconds, *then* press Record. The take should start 10 seconds earlier, including what you said.
12. Record for **15 minutes** while using the PC normally. Afterwards, open `take.json` in Notepad: `"dropouts"` and `"errors"` should both be empty (`[]`).

**Crash recovery** (optional)

13. Start recording, wait 20 seconds, then end the program from **Task Manager** (*End task*). Start it again. It should say a take was recovered. That take's files should play, missing only the last couple of seconds.

**What we're looking for:** files that are complete, in sync, and free of clicks, with no dropouts, and pre-roll and markers that work. Please send `take.json` from the 15-minute take along with the **Copy report** text.

## 6c. Test F: Groups, panels and layouts, about 20 minutes (Stage Plot Mixer.exe)

This checks that a "show" setup, with groups, panels and windows on two screens, comes back exactly as it was. A second monitor helps but isn't required.

**Groups**

1. Start with **New**. Drag a box around **Fader 1** and **Pan 1**, then press **Ctrl+G**. They become one **Group** node, wired in the same place. The sound should not change.
2. Double-click the group. You're now inside it: **Group Input** → Fader → Pan → **Group Output**, with breadcrumbs at the top left. Press **Esc** (with nothing selected) to come back out.
3. Select the group and press **Ctrl+Shift+G**. The two nodes come back, still wired. Press **Ctrl+Z** to undo.
4. Right-click the group → **Save as template**, and name it `My strip`. Press **Tab** and type `strip`. Both **Channel Strip** and **My strip** should be listed. Add a **Channel Strip** and wire **Input 1** into it and its output into **Master Bus**.

**Panels**

5. Right-click **Input 1** → **Add face to panel** → **New panel**. A **Panel 1** tab appears. Add the Channel Strip, Master Fader and Main Out the same way (choose *Panel 1*).
6. Click the **Panel 1** tab. Move a fader: the node in the graph should move too, and the meters should move with your signal.
7. Right-click empty space in the panel → **Add face group**. Drag a face by its name into the group. Right-click a face → **Size** → **Large**.
8. Double-click the tab and rename it `Show`.

**Windows and layouts**

9. Right-click the **Show** tab → **Open in its own window**. Put that window on the second monitor (or anywhere) and resize it.
10. Click **Layouts** → **Store current layout as...** and name it `Show`. Move the window somewhere else, then choose **Layouts** → **Show**. It should jump back.
11. Click **Show Lock**. Try to drag a node, delete something, or move a face: none of these should work. Faders and mutes should still work. Turn Show Lock off again.
12. **Save**, quit and start the app again. The panel window should come back on the same monitor, at the same size.
13. Quit, **unplug the second monitor** (or change the display arrangement in Windows settings), and start again. The panel window should appear on the remaining screen with its title bar visible. Plug the monitor back in while the app is running: nothing should end up off-screen.

**What we're looking for:** anything that ends up off-screen, forgets where it was, changes the sound when grouping, or can be changed while Show Lock is on. Please send screenshots of anything odd along with the **Copy report** text.

## 6d. Test G: Effects, about 20 minutes (Stage Plot Mixer.exe)

This checks the new effects and that the Channel Strip sounds right. Use a mic or instrument on **Input 1**, and headphones.

**Each effect**

1. Start with **New**. Press **Tab**, type `filter`, and add a **Filter**. Wire **Input 1** → Filter, and the Filter → **Fader 1** (in place of the direct wire). Speak into the mic, then drag **High-pass freq** up towards 2 kHz: the sound should get thinner smoothly, with no clicks or zipper noise. Click **Bypass** on and off: the sound should switch smoothly, without a click.
2. Swap the Filter for an **EQ** (same wiring). Boost **High** by +12 dB, then **Low**: you should hear more treble, then more bass. Select the EQ and try the frequency and Q sliders in the Inspector.
3. Swap in a **Compressor**. Set **Threshold** to about −30 dB and speak loudly: the yellow bar above the meter shows how much it's turning you down. Raise **Ratio** and the bar should grow.
4. Swap in a **Gate**. With **Threshold** at −50 dB it should go silent between words, and the yellow bar fills when it's closed. If it chops the start of words, lower the threshold.
5. Swap in a **Limiter**. Its header shows a **64 smp** badge. Turn **Input gain** up to +24 dB and shout: the **Main Out** meter should never go past the ceiling (−1 dB) and should never show red clip lights.
6. Add a **Delay** and a **Reverb** the same way. Change the delay **Time** while talking: it should glide, like tape, rather than click. Turn the reverb **Size** up: the tail gets longer.

**Delay compensation**

7. Wire **Input 1** → **Limiter** → **Master Bus**, and also Input 1 → Fader 1 → Pan 1 → Master Bus as usual, so the same signal takes both paths. It should sound like one clean voice, not hollow or "phasey". Click the wire from **Pan 1** into Master Bus: the Inspector says it's delayed by 64 samples to line up. Delete the limiter: the sound should not click.

**Channel Strip**

8. Press **Tab**, add a **Channel Strip**, and wire Input 1 through it to Master Bus. Double-click it: inside are Trim → Filter → EQ → Compressor → Fader → Pan. Adjust a few of them and check they work.

**What we're looking for:** clicks, crackles, zipper noise, anything that gets stuck loud or silent, clip lights after the limiter, the CPU figure at the bottom climbing a lot, and anything that sounds wrong. Please describe what you did and send the **Copy report** text.

## 7. FireWire interfaces (later)

When the M-Audio FireWire 1814 or ProFire Lightbridge are available:
1. Install their drivers, connect them, and power them on before starting spm-diag.
2. Run tests A and B on each one. For the Lightbridge, connect ADAT sources if you can, so all 32 inputs can be checked.
3. The report's **FireWire (IEEE 1394)** section shows the FireWire card and its chipset. Texas Instruments is the best.

## 8. Sending results

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
