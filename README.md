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

Status: the NVDA add-on is in testing; the SAPI 5 voice is next.

## Using it with NVDA

1. Install `googleIcsTts-<version>.nvda-addon` from the
   [releases](https://github.com/tgeczy/google-ics-tts/releases) and restart NVDA.
2. Open NVDA menu > Tools > **Google TTS (Ice Cream Sandwich) voices**, then either
   - **Download the voices from SourceForge**: reads only GoogleTTS.apk (about
     4 MB) out of the Ice Cream Sandwich gapps package
     `gapps-ics-20120429-signed.zip`, or
   - **Import from a GoogleTTS.apk or gapps file**: any Android 4.0 GoogleTTS.apk,
     or a gapps package containing it.

   Each voice is checked against the known Android 4.0 voice before it is
   converted. Voices are stored in your NVDA configuration folder
   (`googleIcsTts\voices`), so add-on updates keep them.
3. Choose **Google TTS (Ice Cream Sandwich)** in NVDA's Select Synthesizer
   dialog (NVDA+Ctrl+S). Two voices: English US (female) and English GB (male, RP).

Voice settings include NVDA's rate boost and **Shorten pauses**, which trims the
silence between the pieces NVDA speaks.

Works with NVDA 2023.1 and later, 32- and 64-bit, including Windows 7 with NVDA
2023.3. **Check for updates** in the same dialog looks for a newer release on
GitHub, only when you press it.

## Layout

- `src/addon/` - the NVDA add-on: driver, voice importer, update check, notices
- `src/native/` - the shared C engine: hts_engine API (vendored), the text front
  end (`ics_front.c`), the voice converter (`ics_convert.c`) and their API
  (`ics_hts.h`); miniz is vendored under `third_party/`
- `src/sapi/` - the SAPI 5 voice and its settings app
- `tests/` - parity tests against the Python reference in `tests/reference/`
- `tools/` - voice-format decoders, the Python converter, rule generator, build
  and development utilities
- `data/` - the CMU Pronouncing Dictionary

## Building

Needs the Visual Studio 2022 Build Tools and Python 3.

    src\native\build.cmd          rem builds src\addon\engine\x64 and x86\ics_hts.dll
    python tools\build_addon.py   rem writes build\googleIcsTts-<version>.nvda-addon

The DLLs link the C runtime statically, so nothing else needs installing, and
they load on Windows 7. The add-on supports NVDA 2023.1 and later, 32- and
64-bit.

## Tests

The C front end and converter are checked against the Python reference in
`tests/reference/`:

    python tests\test_front.py                        rem labels: every CMUdict word, both accents
    python tests\test_convert.py <eng-usa/voice> ...  rem C and Python .htsvoice are byte-identical
    python tests\test_speak.py <us.htsvoice> <gb.htsvoice>  rem audio is identical sample for sample

## License

BSD 3-Clause for this project's code (see `LICENSE`). hts_engine API, the CMU
Pronouncing Dictionary and the public-domain NRL rules keep their own terms;
see `src/addon/licenses/`.
