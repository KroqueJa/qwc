"""
pytest configuration for the conformance suite.

pytest's default "prepend" import mode already puts this directory on sys.path,
so the sibling modules (corpus, session, wc_harness, run) import cleanly. This
file exists mainly to make that guarantee explicit and to keep the suite runnable
from any working directory.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
