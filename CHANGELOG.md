# Changelog

## 0.1.1

- Use the shared Comp Suite / Suite power button: the same dark disc and green
  power glyph as the rest of the plugins.
- Quick second clicks on the power button (and the other chips) are no longer
  swallowed by the double-click detector, so the button toggles reliably.
- Added DSP and UI regression tests for power-off bypass and the power click.

## 0.1.0

- Initial release.
- Eight-band zero-latency channel EQ: low cut, low shelf, four bells, high
  shelf, high cut.
- Selectable cut slopes: 6, 12, 18, 24, or 30 dB per octave, with wheel
  editing on the cut icons, nodes, and bottom-strip columns.
- Per-band dynamics with threshold, ratio, attack, release, range, and
  up/down mode.
- PRE/POST FFT analyzer, Q-Couple, stereo/mono/L/R processing, output gain,
  A/B compare, and six factory presets.
- LV2 build with the Harrison/Ardour inline display.
- CLAP build with the REAPER `cockos.reaper_embedui` interactive embedded view.
- Cairo/X11 editor shared by both builds.
