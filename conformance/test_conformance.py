"""
pytest front-end for the conformance suite.

This reuses the exact same engine and corpus as run.py, but exposes every
wc/qwc comparison as an individual pytest case for nicer local reporting and
selection (`pytest -k`, `-x`, etc.). The dependency-free run.py is the canonical
CI entrypoint; this is for ergonomics.

    pytest conformance/                      # default (≈60 fuzz cases)
    QWC_CONF_FUZZ=500 pytest conformance/    # deeper
    pytest conformance/ -k "UTF8 and chars"  # slice the matrix

A prebuilt qwc is required (cmake --build <dir> --target qwc) or set $QWC_BIN.
"""

from __future__ import annotations

import atexit
import os
import shutil
import tempfile

import pytest

import corpus
import run as R
import wc_harness as H
from session import build_session


# Built once at collection time. fuzz defaults smaller here than run.py since
# pytest materializes one test object per comparison; bump QWC_CONF_FUZZ for more.
_SESSION = build_session()
_SEED = int(os.environ.get("QWC_CONF_SEED", "1234"))
_FUZZ = int(os.environ.get("QWC_CONF_FUZZ", "60"))

_TMP = tempfile.mkdtemp(prefix="qwc_conf_pytest_")
atexit.register(lambda: shutil.rmtree(_TMP, ignore_errors=True))

_INPUTS = corpus.curated() + corpus.fuzz(_SEED, _FUZZ)
_SINGLE = R._materialize(_INPUTS, _TMP)
_BY_ID = {i.cid: i for i in _SINGLE}
_MULTI = R._multifile_groups(_BY_ID)
_STDIN = [i for i in _SINGLE if i.meta.size <= 256 * 1024]

_SCENARIOS = list(
    R.scenarios(_SESSION, _SINGLE, _MULTI, bpt_stress=True, stdin_inputs=_STDIN)
)


def test_session_has_oracle():
    """Smoke check that the environment is usable before the big matrix."""
    assert os.path.isfile(_SESSION.qwc_bin)
    if _SESSION.utf8_locale is None:
        pytest.skip("no UTF-8 locale available; UTF-8 regime not exercised")


@pytest.mark.parametrize("sc", _SCENARIOS, ids=[sc.label for sc in _SCENARIOS])
def test_wc_conformance(sc):
    _, res = R.run_one(_SESSION, sc)
    # "skip" and "match" are both acceptable; only "fail" is a real problem.
    assert res.ok, res.detail
