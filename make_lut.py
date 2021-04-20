"""make_lut: generate a lookup table include file."""

from functools import partial

import numpy as np
tab_size = 1024

# Function maps a table index in the range 0..1 into waveform output in range -1..1.
#function = np.sin
#basename = "sinLUT"


# 64 equal-amplitude cosines make a band-limited impulse.
def cosines(num_cosines, args):
  num_points = len(args)
  vals = np.zeros(num_points)
  for i in range(num_cosines):
    vals += np.cos((i + 1) * args)
  return vals / float(num_cosines)

#basename = "impulse64"
#function = partial(cosines, 64)

#basename = "impulse10"
#function = partial(cosines, 10)
#function.__name__ = "impulse10"

num_harmonics = 32
basename = "impulse{:d}".format(num_harmonics)
function = partial(cosines, num_harmonics)
function.__name__ = basename

filename = "{:s}_{:d}.h".format(basename, tab_size) 

cpp_base = basename.upper()
cpp_flag = "__" + cpp_base + "_H"
size_sym = cpp_base + "_SIZE"
mask_sym = cpp_base + "_MASK"


def preamble(f):
  """Write the stuff at the top of the file."""
  f.write("// {:s} - lookup table\n".format(filename))
  f.write("// tab_size = {:d}\n".format(tab_size))
  f.write("// function = {:s}\n".format(function.__name__))
  f.write("\n")
  f.write("#ifndef {:s}\n".format(cpp_flag))
  f.write("#define {:s}\n".format(cpp_flag))
  f.write("#define {:s} {:d}\n".format(size_sym, tab_size))
  f.write("#define {:s} 0x{:x}\n".format(mask_sym, tab_size - 1))
  f.write("const int16_t {:s}[{:s}] = {{\n".format(basename, size_sym))


def postscript(f):
  """Write stuff at the bottom of the file."""
  f.write("};\n")
  f.write("\n")
  f.write("#endif\n")


sins = function(np.arange(tab_size) / tab_size * 2 * np.pi)
row_len = 8
with open(filename, "w") as f:
  preamble(f)
  for base in np.arange(0, tab_size, row_len):
    for offset in np.arange(row_len):
      val = int(round(32767 * sins[base + offset]))
      _ = f.write("{:d},".format(val))
    _ = f.write("\n")
  postscript(f)

print("wrote", filename)

