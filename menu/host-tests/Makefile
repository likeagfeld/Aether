# menu/host-tests/Makefile -- host GCC tests for menu/src (run from the repo root: sh tools/hosttest.sh)
CC      ?= gcc
GEN     := ../../build/host-tests
SRC     := ../src
# No AddressSanitizer: under this machine's WSL2 kernel (6.6.114) ASan binaries die at start-up in an endless
# "AddressSanitizer:DEADLYSIGNAL" loop (measured while writing this plan: test_artpak and test_tree; the same
# sources pass without ASan; setarch -R is refused in the container). UBSan stays on and aborts on error.
CFLAGS  := -std=gnu99 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined \
           -I$(SRC) -I$(GEN) -I. -I../lib/satiator -I../launcher -I../app
TESTS   := $(basename $(wildcard test_*.c))
# sdwrite.c calls libsatiator, so it links only into the tests that bring a satiator fake
# savegen.c (the save folders' reader rule) calls libsatiator too
SRCS    := $(filter-out $(SRC)/sdwrite.c $(SRC)/savegen.c $(SRC)/impscan.c,$(wildcard $(SRC)/*.c))
SDW     := $(SRC)/sdwrite.c $(SRC)/savegen.c $(SRC)/impscan.c
EXTRA_test_desc := ../launcher/cdparse.c ../launcher/cue2desc.c
# upstream MPL code (Task 17) is compiled as-is: its warnings are reported but not fatal
CFLAGS_test_desc := -Wno-error
# 2026-11-24 (the lid swap): the launcher's descriptors for one disc or a set, through the real cue2desc (warnings as test_desc)
EXTRA_test_ldesc := ../launcher/cdparse.c ../launcher/cue2desc.c ../launcher/ldesc.c
CFLAGS_test_ldesc := -Wno-error
EXTRA_test_card := $(SDW) ../app/sd.c ../app/satfs.c ../app/card.c fake_satiator.c
# boot-animation gaps (2026-11-23): the real card load over a big fake card, on fake_satiator's virtual clock
EXTRA_test_bootgaps := $(SDW) ../app/sd.c ../app/satfs.c ../app/card.c ../app/persist.c fake_satiator.c
# the REAL vendored satiator.c, built with its SAT_HOST_TEST shim (see that
# file's top-of-file comment) so exec_cmd's bounded HIRQ wait is tested directly, not reimplemented.
EXTRA_test_sattimeout := ../lib/satiator/satiator.c
CFLAGS_test_sattimeout := -DSAT_HOST_TEST
EXTRA_test_launch := $(SDW) ../app/sd.c ../app/satfs.c ../app/info.c ../app/launch.c fake_satiator.c
EXTRA_test_artjob := $(SDW) ../app/sd.c ../app/artjob.c fake_satiator.c
# big-card stutter (2026-11-23): path commands per rest frame / per ART.PAK cover
EXTRA_test_navwalks := $(SDW) ../app/sd.c ../app/artjob.c fake_satiator.c
# hardware report on 3c64908: 200 launch-panel open/close cycles, the SD side (art job, IP.BIN read, read-ahead)
EXTRA_test_pgcycle := $(SDW) ../app/sd.c ../app/satfs.c ../app/info.c ../app/artjob.c fake_satiator.c
EXTRA_test_persist := $(SDW) ../app/sd.c ../app/persist.c fake_satiator.c
# one game's CHEATS.PAK record through the real sd.c
EXTRA_test_cheatpak := $(SDW) ../app/sd.c ../app/cheats.c fake_satiator.c
EXTRA_test_sdwrite := $(SDW) fake_satiator.c
# the per-game-save executor over the BRAM and SD fakes
EXTRA_test_pgsrun := $(SDW) ../launcher/pgsrun.c ../launcher/pgsbak.c fake_bram.c fake_satiator.c
# hardware 2026-11-24 (a very long save swap on the big card): the Satiator commands of each launch, against budgets
EXTRA_test_pgscost := $(EXTRA_test_pgsrun)
# the same sweep with absolute names (the swap's fallback when its chdir does not take)
EXTRA_test_pgsrun_abs := $(EXTRA_test_pgsrun)
# the Kai/Rings import and the BRAM backup (pgsbak.c), with the clock's formatter (smpcwait.c)
EXTRA_test_pgsbak := $(SDW) ../app/sd.c ../launcher/pgsrun.c ../launcher/pgsbak.c ../launcher/smpcwait.c fake_bram.c fake_satiator.c
# the save manager (svmgr.c) over the same fakes, with the BRAM backup it runs before a console save goes
EXTRA_test_svmgr := $(SDW) ../launcher/svmgr.c ../launcher/pgsrun.c ../launcher/pgsbak.c ../launcher/smpcwait.c fake_bram.c fake_satiator.c
# the in-menu popups (2026-11-25): Manage saves' model over svmgr, and the Cheats popup's file side
EXTRA_test_psave := $(EXTRA_test_svmgr) ../pop/psave.c
CFLAGS_test_psave := -I../pop
EXTRA_test_pcheat := $(SDW) ../app/sd.c ../app/cheats.c ../pop/pcheat.c ../launcher/chedit.c ../launcher/chkai.c fake_satiator.c
CFLAGS_test_pcheat := -I../pop
EXTRA_test_popgeom := $(EXTRA_test_psave) ../pop/pgeom.c
CFLAGS_test_popgeom := -I../pop
# Settings > About as a popup (2026-11-27): its layout, the credits wrapped and scrolled, and CREDITS.md row by row
EXTRA_test_pabout := ../pop/pabout.c ../pop/pgeom.c
CFLAGS_test_pabout := -I../pop
EXTRA_test_fakefs := $(SDW) ../app/fake/fakesat.c ../app/sd.c ../app/satfs.c ../app/card.c
CFLAGS_test_fakefs := -I../app/fake
# 2026-11-23 hardware report (a 699-name card root hung the library load): the real loader over a large fake card
EXTRA_test_bigcard := $(EXTRA_test_fakefs)
CFLAGS_test_bigcard := -I../app/fake
# the launcher's exception-vector names (pure C; the linked table is checked by sbuild check_vectab)
EXTRA_test_lvec := ../launcher/lvecname.c
# the Aether rename (D9): the tilted orbit's extents, from cos/sin
EXTRA_test_istlgeom := -lm
# INTBACK issue timing (pure C sequencer; the launcher feeds it TVSTAT)
EXTRA_test_smpcwait := ../launcher/smpcwait.c
EXTRA_test_padfall := ../launcher/smpcwait.c
# the in-game reset stub against a fake register bank (IGR_HOST_TEST) and the IP check (pure C)
EXTRA_test_igr := ../launcher/igrstub.c ../launcher/igrcheck.c
EXTRA_test_cwx := ../launcher/cwxplan.c
# Kai's cheat method (the planner, the layout, the arming over simulated RAM)
EXTRA_test_chkai := ../launcher/chkai.c
# the launcher's cheat editor model and the CHEATS.PAK rewrite
EXTRA_test_chedit := ../launcher/chedit.c ../launcher/chkai.c
CFLAGS_test_igr := -DIGR_HOST_TEST
# Astal (2026-11-23): SYS_EXECDMP's guard enters the stub only while it is intact, otherwise resets the console
EXTRA_test_igrguard := ../launcher/igrguard.c ../launcher/igrcheck.c
CFLAGS_test_igrguard := -DIGR_HOST_TEST
# the direct SMPC SSHOFF against a fake register bank (SSHOFF_HOST_TEST) and the shared issue window
EXTRA_test_sshoff := ../launcher/sshoff.c ../launcher/smpcwait.c
CFLAGS_test_sshoff := -DSSHOFF_HOST_TEST
# hardware 2026-11-25 (MK II, step 1 address error): the loading screen's LOADBG.BIN reads, every s_read word-aligned
EXTRA_test_lgread := fake_satiator.c
# hardware 2026-11-27 (Aether 0.1: the eye stalled through the save swap): the launch's chances to pump it, over the real swap
EXTRA_test_lgpace := $(SDW) ../launcher/pgsrun.c ../launcher/pgsbak.c ../launcher/lgread.c fake_bram.c fake_satiator.c
# the console setup file (setup.h): written only when the setup differs, through the real sd.c
EXTRA_test_setup := $(SDW) ../app/sd.c ../app/setupw.c fake_satiator.c
# CWX hardware debug (2026-11-23): cdblk.c's takes against a model of LIBCD's command gate and the CD block (fake_cdc.c)
EXTRA_test_cdtake := ../launcher/cdblk.c fake_cdc.c
CFLAGS_test_cdtake := -DCDBLK_HOST_TEST -DOM_CD_DIAG

.PHONY: run clean
run: $(addprefix $(GEN)/,$(TESTS))
	@set -e; for t in $(TESTS); do $(GEN)/$$t; done; echo "host-tests: $(words $(TESTS)) binaries passed"

$(GEN)/cases.inc: gen_cases.py $(wildcard ../../contract/fixtures/*.json) ../../tools/omsat/lnchhdr.py
	mkdir -p $(GEN)
	python3 gen_cases.py $@

$(GEN)/%: %.c th.h $(SRCS) $(SDW) $(wildcard $(SRC)/*.h) $(GEN)/cases.inc
	$(CC) $(CFLAGS) $(CFLAGS_$*) -o $@ $< $(SRCS) $(EXTRA_$*)

clean:
	rm -rf $(GEN)
