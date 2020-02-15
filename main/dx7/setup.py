from distutils.core import setup, Extension
import glob
import os
# the c++ extension module
sources = glob.glob("*.cc")
sources.remove("main.cc")
sources.remove("test_ringbuffer.cc")
sources.remove("test_filter.cc")
sources.remove("test_neon.cc")
os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

extension_mod = Extension("dx7", sources=sources) #["pydx7.cc"])

setup(name = "dx7", ext_modules=[extension_mod])
