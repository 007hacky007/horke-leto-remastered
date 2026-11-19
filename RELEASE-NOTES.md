A fix for the Windows builder. The game itself is the same as in 1.0.2, and
macOS users don't need this update.

**Windows**
- The builder no longer stops at step *03 Remaster* with
  `invalid UTF-8 byte` when the target folder path contains letters with
  diacritics, for example a Windows user name like *Lukáš*.

If your build failed this way, download the new builder and run it again.

- **Horke-Leto-Builder-macOS.dmg**: open it and follow the picture in the window.
- **Horke-Leto-Builder-Windows.exe**: if SmartScreen warns you, choose
  *More info* > *Run anyway*.

Found a bug? Open an issue and attach the log from *Zobrazit podrobnosti*.
