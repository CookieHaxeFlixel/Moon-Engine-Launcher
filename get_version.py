"""Prints Project::VERSION from project.hpp (used by build.bat for the installer and the update ZIP)."""
import pathlib
import re
import sys

text = (pathlib.Path(__file__).resolve().parent / "project.hpp").read_text(encoding="utf-8")
match = re.search(r'\bVERSION\s*=\s*"([^"]+)"', text)

if not match:
    sys.exit("VERSION not found in project.hpp")

print(match.group(1))
