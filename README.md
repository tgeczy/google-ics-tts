# Google TTS (Ice Cream Sandwich)

The first Google text-to-speech voices, which shipped only with Android 4.0
"Ice Cream Sandwich" (Google's "Flint" engine), running again on Windows: as
an NVDA synthesizer and, later, a SAPI 5 voice.

Flint is built on the HMM-based speech synthesis system HTS. This project
decodes Google's compact voice format, converts it for the open-source
[hts_engine API](https://hts-engine.sourceforge.net/), and supplies the text
front end Google's engine needs: pronunciations from the CMU Pronouncing
Dictionary and the NRL letter-to-sound rules, with Flint's own label format.

**No Google data is included.** The voices belong to Google. You supply an
Android 4.0 `GoogleTTS.apk`, or an Ice Cream Sandwich gapps package that
contains one, and the voices are converted on your own computer.

Status: work in progress.

## Layout

- `addon/` - the NVDA add-on (driver, front end, NRL rules, notices)
- `native/` - `ics_hts.c` and the vendored hts_engine API; `build.cmd` builds
  x86 and x64 DLLs with the Visual Studio 2022 Build Tools
- `tools/` - the voice-format decoders, the converter and test utilities
- `data/` - the CMU Pronouncing Dictionary

## Building

Needs the Visual Studio 2022 Build Tools and Python 3.

    native\build.cmd              rem builds addon\engine\x64 and x86\ics_hts.dll
    python tools\build_addon.py   rem writes build\googleIcsTts-<version>.nvda-addon

The DLLs link the C runtime statically, so nothing else needs installing, and
they load on Windows 7. The add-on supports NVDA 2023.1 and later, 32- and
64-bit.

## Tests

The C front end and converter are checked against the Python reference in
`tools/reference/`:

    python tools\test_front.py                        rem labels: every CMUdict word, both accents
    python tools\test_convert.py <eng-usa/voice> ...  rem C and Python .htsvoice are byte-identical
    python tools\test_speak.py <us.htsvoice> <gb.htsvoice>  rem audio is identical sample for sample

## License

BSD 3-Clause for this project's code (see `LICENSE`). hts_engine API, the CMU
Pronouncing Dictionary and the public-domain NRL rules keep their own terms;
see `addon/licenses/`.
