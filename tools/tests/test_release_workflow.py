# Final review I3: .github/workflows/release.yml must never put build/sgl (SEGA's own closed Saturn SDK) into an
# actions/cache entry -- a cache is restorable by any workflow run in the repo, a materially bigger exposure than a
# file sitting on a machine you control. The job that needs build/sgl must run on a trusted path (a self-hosted
# runner here, not a GitHub-hosted one), and any cache restored on a different OS than it was saved on needs
# enableCrossOsArchive: true or a fail-on-cache-miss restore can fail outright.
import yaml

from pathlib import Path

WORKFLOW = Path(__file__).resolve().parents[2] / ".github" / "workflows" / "release.yml"

GITHUB_HOSTED_PREFIXES = ("ubuntu-", "windows-", "macos-")


def _load():
    return yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))


def _cache_steps(job):
    return [s for s in job.get("steps", []) if str(s.get("uses", "")).startswith("actions/cache/")]


def test_build_sgl_is_never_named_in_any_actions_cache_step():
    wf = _load()
    for job_name, job in wf["jobs"].items():
        for step in _cache_steps(job):
            path = step.get("with", {}).get("path", "")
            assert "build/sgl" not in path, f"{job_name}: {step.get('name')} caches build/sgl"


def test_the_job_that_needs_build_sgl_does_not_run_on_a_github_hosted_runner():
    wf = _load()
    for job_name, job in wf["jobs"].items():
        steps_text = " ".join(str(s) for s in job.get("steps", []))
        if "build/sgl" in steps_text:
            runs_on = job["runs-on"]
            assert not str(runs_on).startswith(GITHUB_HOSTED_PREFIXES), (
                f"{job_name} touches build/sgl but runs on a GitHub-hosted runner ({runs_on})")


def test_every_cache_restore_that_can_cross_operating_systems_sets_enableCrossOsArchive():
    wf = _load()
    for job_name, job in wf["jobs"].items():
        if str(job.get("runs-on", "")).startswith(GITHUB_HOSTED_PREFIXES) is False:
            continue   # this job's own OS is whatever its runner provides -- only GitHub-hosted jobs are checked
        for step in _cache_steps(job):
            if step["uses"].startswith("actions/cache/restore"):
                assert step.get("with", {}).get("enableCrossOsArchive") is True, (
                    f"{job_name}: {step.get('name')} restores a cache without enableCrossOsArchive: true")


def test_the_self_hosted_jobs_checkout_never_wipes_ignored_files():
    # actions/checkout defaults to clean: true, which runs `git clean -ffdx` -- the -x wipes ignored files, so
    # build/sgl (expected to already be on a self-hosted runner's disk, never fetched by this workflow) would be
    # deleted on every run unless this step opts out.
    wf = _load()
    for job_name, job in wf["jobs"].items():
        if str(job.get("runs-on", "")).startswith(GITHUB_HOSTED_PREFIXES):
            continue
        checkouts = [s for s in job.get("steps", []) if str(s.get("uses", "")).startswith("actions/checkout")]
        assert checkouts, f"{job_name} (self-hosted) has no actions/checkout step to check"
        for step in checkouts:
            assert step.get("with", {}).get("clean") is False, (
                f"{job_name}: {step.get('name') or 'actions/checkout'} does not set clean: false -- "
                "it will `git clean -ffdx` build/sgl away on this self-hosted runner")
