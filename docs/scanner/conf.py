source_suffix = '.rst'
root_doc = 'index'
project = 'deai'
copyright = '2022, Yuxuan Shui'
author = 'Yuxuan Shui'

add_module_names = False

import sys, os, subprocess

from pathlib import Path
sys.path.append(str(Path(__file__).resolve().parent / "sphinx-exts"))
extensions = ['luadomain']

subprocess.run(["meson", "setup", "../../build", "../.."])
os.symlink("build/compile_commands.json", "../../compile_commands.json")
subprocess.run(["cargo", "run", "../..", "."])
