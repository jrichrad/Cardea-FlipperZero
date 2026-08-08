# Changelog

## v1.0 — 2026-08-07

First release.

### The idea

A passive-keyless car asks its key a question over 125 kHz, which reaches about
a metre. The key answers over UHF, which reaches hundreds. The cheap relay theft
carries only the question — nobody relays the answer, because the answer gets to
the car by itself. Cardea sits with the car and listens for that answer arriving
when nobody asked for it.

### Detection

- Hops all five worldwide key bands (315, 390, 433.92, 868.35, 915 MHz) and
  camps on whichever one speaks, with a bounded camp so one chatty sensor cannot
  hold the receiver all night.
- Burst extraction with hysteresis, an eight-bin normalised envelope, and
  adaptive decimation so a long burst is described whole rather than truncated.
- Three independent evidence families — UNSOL, RHYTHM, CLONE — scored
  separately.
- RELAY LIKELY requires two families to agree **and** requires RHYTHM
  specifically, because pressing your own key six times is both unsolicited and
  identical.
- Beacon veto: a periodic train running over a minute is treated as scenery,
  which keeps weather stations and tyre sensors from crying wolf.
- Score capped at 92. The engine never claims certainty.
- Sustained carriers reported on their own HELD channel, never folded into the
  score.
- Optional key learning: two agreeing presses build a duration + envelope + band
  template, after which every other remote in the street stops counting.

### Interface

- Animated splash: a door shutting on its hinge, with the key left outside.
- Guard screen with a 30-second burst raster, three fill-graded evidence chips,
  a scored bar with the verdict thresholds marked, plus Detail and Bands pages.
- Full-screen alert with three pages — what happened, why it fired, what to do —
  a repeating alarm, and a cooldown so a dismissed alert cannot immediately
  re-fire off the same evidence.
- Six-panel animated primer explaining how relay theft works and, honestly,
  which half of it a Flipper cannot hear.
- Watch report with the session's worst moment and a plain-language reading.
- Arming delay so the walk to the front door is not counted as evidence.
- Mute silences the speaker and motor but never the LED.
- CSV event log at `/ext/apps_data/cardea/events.csv`.

### Verification

- 18,476 host checks on the engine, including a randomised sweep over 3,000
  generated situations asserting the honesty invariants (score cap, family
  minimum, RHYTHM requirement, armed-only top verdict).
- Builds clean on the ufbt release channel (API 87.1) and the dev channel
  (API 88.2).
- Screen mockups rendered from the app's own layout constants; they caught four
  real collisions before they reached a device.
