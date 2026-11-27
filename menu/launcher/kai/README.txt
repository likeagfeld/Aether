Pseudo Saturn Kai's cheat-code bootstrap and in-RAM handler, kept verbatim (GPL-2.0, see LICENSES/GPL-2.0.txt).

From the Pseudo Saturn Kai v6.545 full source tree by cafe-alpha and the Pseudo Saturn team
(https://ppcenter.webou.net/pskai/), archive SatCom_src_20240114.7z,
SHA-256 11d6803d775e873e742f16079d6969ac6ffe0895ef7bf317025be29ad7c89448, folder pseudosat_kai/cheat_codes/.
cheat_bootstrap_ihr.s also carries Israel Jacquez's MIT notice (Yabause).

  cheat_bootstrap_ihr.s   SHA-256 623c030f7db879ad2f093a0d83f5abc2e830ce51ccb2d52a235727c8f4a2657b
  cheat_codes_hdlr_ram.s  SHA-256 da56053d7fa8dab6a6d83c4e649ef4edb77d6add314e6afce765bc3a7af3d4d6

Byte-for-byte copies (git keeps their line endings: .gitattributes -text). menu/launcher/chkai.c lays out Kai's
config around them; tools/sbuild.py check_chkai checks the linked bytes against these sources.
