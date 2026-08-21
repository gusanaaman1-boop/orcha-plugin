ORCHA — Windows install
Rhythm Loop Generator · by Gussa Naaman · v0.17.0 · x64

------------------------------------------------------------------------------
INSTALL - this is the whole thing
------------------------------------------------------------------------------

  1. EXTRACT this ZIP to a folder without spaces or Hebrew in the path.
     C:\Audio\ORCHA is good.
     Do not run the .bat from inside the ZIP viewer: Windows copies it out on
     its own and then it has nothing to build.

  2. Close Cubase.
     Windows will not replace a plugin a running host has open.

  3. RIGHT-CLICK  INSTALL-ORCHA-BUILD.bat  then "Run as administrator".

  That one file does everything: checks what is installed, builds, installs,
  verifies. If FOUR COLOR was ever built on this machine, JUCE is already at
  %USERPROFILE%\JUCE and NOTHING is downloaded - the whole run is a few
  minutes of compiling and one copy.

  It installs:

    ORCHA.vst3  ->  C:\Program Files\Common Files\VST3
    ORCHA.exe   ->  C:\Program Files\Naaman\ORCHA          (standalone)

  4. Start Cubase, then Studio menu, then VST Plug-in Manager, then Update.
     ORCHA is an INSTRUMENT, not an effect: add it on an INSTRUMENT TRACK.
     It appears under Naaman, category Instrument / Drum.

------------------------------------------------------------------------------
IF SOMETHING GOES WRONG
------------------------------------------------------------------------------

The installer checks every step and stops with a reason rather than claiming
success, and writes everything to Orcha-install-log.txt next to itself.
If it stops - send me that log file, or a screenshot of the window.

To start clean:  right-click UNINSTALL-ORCHA.bat, "Run as administrator".
That clears every plausible location and both install shapes, plus ORCHA's
loop cache, and says what it found in each.

------------------------------------------------------------------------------
WHAT IT IS
------------------------------------------------------------------------------

Drop up to three samples. Pick the moment and the rhythm family. One click -
twelve different loop options, synced to the project tempo. Play them, keep
the good ones, drag any card straight into an audio track as a WAV.

  DROP / BREAK / BUILD / GROOVE       what part of the track this is for
  EDM / MELODIC TECHNO / PSYTRANCE /  which rhythmic language it speaks
  ARABIC / MEDITERRANEAN / AFRO /
  CINEMATIC / HYBRID
  ENERGY / DENSITY / RANDOMNESS       the three macros
  1 / 2 / 4 BARS                      loop length

  GENERATE MORE keeps your favorited cards and replaces the rest.
  The heart keeps a card. The circular arrow re-rolls just that card.

------------------------------------------------------------------------------
WORTH CHECKING ON THIS BUILD
------------------------------------------------------------------------------

  * Drag a card into Cubase: the WAV must land exactly on the grid at the
    project tempo, and loop cleanly.
  * Change the project tempo: ORCHA re-renders its options within a second
    or two; drag again and the new WAV matches the new tempo.
  * Save the project, close Cubase, reopen: samples, options and favorites
    all return.
  * Press play in Cubase while previewing a card: the loop locks to the
    project position.

------------------------------------------------------------------------------
KNOWN
------------------------------------------------------------------------------

Development build, unsigned. First Windows build of this plug-in - the mac
build passed 1179 automated checks; what Windows adds on top is exactly what
this run is for. MediaBay drag-in works when Cubase exposes a real file path;
the card's click-to-load is the guaranteed fallback.
