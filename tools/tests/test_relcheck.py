"""the release hygiene check on --app (omsat.relcheck)."""
from omsat.relcheck import check


def _img(*ss):
    return b"\0\0".join(s.encode() for s in ss)


def test_clean_image_passes():
    errs, build = check(_img("Launch canceled", "Aether 0.1 ef2b2b5 2026-11-19"), "kept .text menu.o", "0.1")
    assert errs == [] and build == "Aether 0.1 ef2b2b5 2026-11-19"


def test_developer_text_and_fake_code_fail():
    errs, _ = check(_img("Fake build: would hand off now", "BOOT FRAME", "Aether 0.1 ef2b2b5 2026-11-19"),
                    " .text 0x0600 fake_fakesat.o", "0.1")
    assert len(errs) == 3


def test_build_string_rules():
    assert check(_img("Aether 0.2 ef2b2b5 2026-11-19"), "", "0.1")[0]           # wrong version
    assert check(_img("MENU BUILD: VIBEMENU M3 TASK 16A"), "", "0.1")[0]           # the old fixed tag
    dirty = _img("Aether 0.1 ef2b2b5-dirty 2026-11-19")
    assert check(dirty, "", "0.1")[0] and check(dirty, "", "0.1", allow_dirty=True)[0] == []
