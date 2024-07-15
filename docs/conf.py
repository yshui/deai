source_suffix = '.rst'
root_doc = 'index'
project = 'deai'
copyright = '2022, Yuxuan Shui'
author = 'Yuxuan Shui'

add_module_names = False
html_theme = 'sphinx_rtd_theme'

import sys, os, subprocess

from pathlib import Path
sys.path.append(str(Path(__file__).resolve().parent / "sphinx-exts"))
extensions = ['luadomain']

os.environ['RUST_LOG'] = 'info'

if not os.path.isdir('../build'):
    subprocess.run(["meson", "setup", "../build", ".."])
    subprocess.run(["ninja", "-C", "../build", "libextra_doc_target.a"])

try:
    os.symlink("build/compile_commands.json", "../compile_commands.json")
except:
    pass
subprocess.run(["cargo", "run", "../..", "../generated"], cwd="scanner")
