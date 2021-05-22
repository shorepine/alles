"""make_clipping_lut.py - generate the lookup table used for soft clipping."""
import numpy as np

# Soft clipping lookup table scratchpad.

SAMPLE_MAX = 32767
LIN_MAX = 29491  #// int(round(0.9 * 32768))
NONLIN_RANGE = 4915  # // size of nonlinearity lookup table = round(1.5 * (INT16_MAX - LIN_MAX))

clipping_lookup_table = np.arange(LIN_MAX + NONLIN_RANGE)
print(len(clipping_lookup_table))

for x in range(NONLIN_RANGE):
  x_dash = float(x) / NONLIN_RANGE
  clipping_lookup_table[x + LIN_MAX] = LIN_MAX + int(np.floor(NONLIN_RANGE * (x_dash - x_dash * x_dash * x_dash / 3.0)))

#plt.plot(clipping_lookup_table[25000:]) # - np.arange(25000, len(clipping_lookup_table)))
print(max(clipping_lookup_table))

filename = "clipping_lookup_table.h"

with open(filename, "w") as f:
  f.write("// Automatically generated.\n// Clipping lookup table\n")
  f.write("#define LIN_MAX %d\n" % LIN_MAX)
  f.write("#define NONLIN_RANGE %d\n" % NONLIN_RANGE)
  f.write("#define NONLIN_MAX (LIN_MAX + NONLIN_RANGE)\n")
  f.write("const uint16_t clipping_lookup_table[NONLIN_RANGE] = {\n")
  samples_per_row = 8
  for row_start in range(0, NONLIN_RANGE, samples_per_row):
    for sample in range(row_start, min(NONLIN_RANGE, row_start + samples_per_row)):
      f.write("%d," % clipping_lookup_table[LIN_MAX + sample])
    f.write("\n")
  f.write("};\n")
print("wrote", filename)
