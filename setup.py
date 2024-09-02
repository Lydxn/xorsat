from setuptools import Extension, find_packages, setup
from setuptools.command.build_ext import build_ext
import os
import shutil
import subprocess
import sys

class m4ri_build_ext(build_ext):
    def build_extension(self, ext):
        def run(command):
            subprocess.check_call(command, cwd='xorsat/m4ri')

        # run(['autoreconf', '--install'])
        # run(['./configure'])
        # run(['make'])

        super().build_extension(ext)

def check_binary(name, command):
    try:
        subprocess.check_call(command)
    except OSError:
        print(f'Missing build dependency {name!r}, please install it first!',
            file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    # Clean the build folder that setuptools creates before every run, or else it causes
    # weird issues.
    shutil.rmtree('build', ignore_errors=True)

    check_binary('autoreconf', ['autoreconf', '--version'])
    check_binary('make', ['make', '--version'])

    setup(
        name='xorsat',
        author='Lyndon Ho',
        author_email='hlyndon20@gmail.com',
        description='efficient XOR-SAT solver',
        version='0.1.0',
        packages=find_packages(),
        package_data={'xorsat': ['m4ri/.libs/*.so']},
        ext_modules=[
            Extension(
                name='xorsat._xorsat',
                sources=['xorsat/_xorsatmodule.c'],
                extra_compile_args=['-O3', '-march=native'],
                include_dirs=['xorsat/m4ri'],
                libraries=['m4ri'],
                library_dirs=['xorsat/m4ri/.libs'],
                extra_link_args=['-Wl,-rpath=$ORIGIN/m4ri/.libs'],
            ),
        ],
        cmdclass={'build_ext': m4ri_build_ext},
    )