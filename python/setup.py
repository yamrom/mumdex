#! /usr/bin/env python

import os
import platform

# You may need to modify this for your system setup:
RECENT_GCC_BASE = "/data/software/gcc/7.4.0/rtf"

os.environ["PATH"] = RECENT_GCC_BASE + "/bin:" + os.environ["PATH"]

extra_link = dict()
# add --rpath for library finding on linux
if platform.system() == "Linux":
    extra_link = ['-Xlinker', '--rpath=' + RECENT_GCC_BASE + "/lib64"]
                  
extra_compile_args=['-std=c++11', '-I', 'core', '-I', 'utility']
# disable bogus warnings on Mac
if platform.system() == "Darwin":
    # extra_compile_args.append('-arch x86_64')
    pass
    # 
    # extra_compile_args.append('-Wno-format')
    # extra_compile_args.append('-Wno-missing-braces')

from distutils.core import setup, Extension
setup(ext_modules=[Extension(
            "mumdex._mumdex",
            ["src/python_mumdex.cpp", "utility/files.cpp"],
            extra_compile_args=extra_compile_args,
            extra_link_args=extra_link,
            )],
      data_files=[('bin',
                   ['src/scripts/test_python_mumdex.sh',
                    'src/scripts/bridge_finder.py',
                    'src/scripts/bridge_info.py',
                    'src/scripts/candidate_finder.py',
                    'src/scripts/chromosome_bridges.py',
                    'src/scripts/load_counts.py',
                    'src/scripts/mumdex2txt.py',
                    'src/scripts/show_mums.py',
                    'src/scripts/test_python_mumdex.py',
                    'src/scripts/mapper.py',
                    'src/scripts/sample_bridge_finder.sh',
                    'src/scripts/bridge_tester.py'])],
      requires=['numpy', 'sys', 'os']

)

