# menu/launcher upstream sources

font8x8 (Daniel Hepper, https://github.com/dhepper/font8x8, public domain) and satiator-menu (James Laird-Wah, https://github.com/satiator/satiator-menu, MPL-2.0). License headers are kept in each file; see CREDITS.md. Pseudo Saturn Kai's cheat bootstrap and in-RAM handler (cafe-alpha, GPL-2.0) are kept verbatim in `kai/`; see `kai/README.txt`.

## Local changes

- `cue2desc.c` `cue2desc`: the cue's folder is cut at the **last** `/` (`strrchr`), not the first (`strchr`). Upstream
  only ever passes a cue at most one folder down from the working folder; a multi-disc set's cue two folders down
  (`PhantasM (JP) (Disc 1)/JP FILES/x.cue`) or an absolute cue path resolved its tracks in the wrong folder.
  Tested in `menu/host-tests/test_desc.c`.
