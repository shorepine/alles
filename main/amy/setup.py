from distutils.core import setup, Extension
import glob
import os
# the c++ extension module
sources = glob.glob("*.c")
os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

extension_mod = Extension("amy", sources=sources) 

setup(name = "amy", ext_modules=[extension_mod])