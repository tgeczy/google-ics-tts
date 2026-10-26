# Google TTS (Ice Cream Sandwich)

The first Google text-to-speech voices, which shipped only with Android 4.0
"Ice Cream Sandwich" (Google's "Flint" engine), running again on Windows: as
an NVDA synthesizer and a SAPI 5 voice.

Flint is built on the HMM-based speech synthesis system HTS. This project
decodes Google's compact voice format, converts it for the open-source
[hts_engine API](https://hts-engine.sourceforge.net/), and supplies the text
front end Google's engine needs: pronunciations from the CMU Pronouncing
Dictionary and the NRL letter-to-sound rules, with Flint's own label format.

**No Google data is included.** The voices belong to Google. You supply an
Android 4.0 `GoogleTTS.apk`, or an Ice Cream Sandwich gapps package that
contains one, and the voices are converted on your own computer.

Status: early releases (0.x), in testing.

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

## Using it as a SAPI 5 voice

For JAWS, Narrator, Balabolka and any other program that speaks with SAPI 5
voices; no Python or other runtime is needed.

1. Run `googleIcsTts-sapi-<version>.exe` from the
   [releases](https://github.com/tgeczy/google-ics-tts/releases). It installs
   both the 64- and 32-bit voice, so 32-bit programs get it too.
2. Leave **Download the voices from SourceForge now** ticked at the end, or
   open **Google TTS (Ice Cream Sandwich) voice settings** from the Start menu
   later. The settings app can
   - download the voices from SourceForge, or import them from a GoogleTTS.apk
     or gapps file,
   - copy the voices the NVDA add-on already imported,
   - use a voices folder of your own (one holding `ics_en_us.htsvoice` /
     `ics_en_gb.htsvoice`),
   - shorten pauses (on by default), and
   - check for updates.

   Voices are stored in `%ProgramData%\Google ICS TTS\voices`, which every
   account and the Windows sign-in screen can use. The NVDA add-on finds them
   there as well.
3. Pick **Google TTS (Ice Cream Sandwich) English US** or **English GB** in
   your program's voice list.

Rate 0 is the voice's normal speed and +10 about six times as fast; bookmarks
are reported where they fall in the audio. Windows 7 SP1 and later.

## Layout

- `src/addon/` - the NVDA add-on: driver, voice importer, update check, notices
- `src/native/` - the shared C engine: hts_engine API (vendored), the text front
  end (`ics_front.c`), the voice converter (`ics_convert.c`) and their API
  (`ics_hts.h`); miniz is vendored under `third_party/`
- `src/sapi/` - the SAPI 5 voice, its settings app (native importer and update
  check) and the installer (`installer/`, Inno Setup 6)
- `tests/` - parity tests against the Python reference in `tests/reference/`
- `tools/` - voice-format decoders, the Python converter, rule generator, build
  and development utilities
- `data/` - the CMU Pronouncing Dictionary

## Building

Needs Visual Studio or its Build Tools with the C++ workload (found with
vswhere), and Python 3.

    src\native\build.cmd          rem builds src\addon\engine\x64 and x86\ics_hts.dll
    python tools\build_addon.py   rem writes build\googleIcsTts-<version>.nvda-addon
    src\sapi\build.cmd            rem build\sapi: both ics_sapi.dll, IcsTtsSettings.exe, test probes
    src\sapi\build.cmd installer  rem ...and build\googleIcsTts-sapi-<version>.exe (Inno Setup 6)

Both builds take the version from `src\addon\manifest.ini`. To try a SAPI
build without the installer, register both DLLs from an administrator prompt
(System32 regsvr32 for x64, SysWOW64 for x86).

The DLLs link the C runtime statically, so nothing else needs installing, and
they load on Windows 7. The add-on supports NVDA 2023.1 and later, 32- and
64-bit.

## Tests

The C front end and converter are checked against the Python reference in
`tests/reference/`:

    python tests\test_front.py                        rem labels: every CMUdict word, both accents
    python tests\test_convert.py <eng-usa/voice> ...  rem C and Python .htsvoice are byte-identical
    python tests\test_speak.py <us.htsvoice> <gb.htsvoice>  rem audio is identical sample for sample

The registered SAPI voice is driven through ISpVoice, both architectures and
both voices: audio, bookmark order and timing, and how quickly an abort stops:

    python tests\test_sapi.py

The SAPI side's native importer must produce the same voices as the Python
converter, and its update check must pick the right release:

    python tests\test_sapi_import.py <GoogleTTS.apk> [<gapps zip>] [--download]
    python tests\test_sapi_updates.py [--live]

Nothing committed may contain a path from this machine; check before pushing
(`--history` checks every commit too):

    python tools\check_paths.py
    python tests\test_check_paths.py

## License

BSD 3-Clause for this project's code (see `LICENSE`). hts_engine API, the CMU
Pronouncing Dictionary and the public-domain NRL rules keep their own terms;
see `src/addon/licenses/`.
