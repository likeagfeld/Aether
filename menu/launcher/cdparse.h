/*  Copyright (c) 2015 James Laird-Wah
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/. */

/* Modified 2026-11-17 for Aether (menu/launcher/UPSTREAM.md): the only modification is this
 * notice. Otherwise unmodified from satiator-menu@75bf6e5 disc_format/. */

extern char *cdparse_error_string;
int image2desc(const char *infile, const char *outfile);

int cue2desc(const char *cue_file, const char *desc_file);
void cdparse_set_error(const char *fmt, ...);
