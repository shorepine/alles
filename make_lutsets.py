"""make_lutsets - generate sets of lookup tables for libblosca.

libblosca is a library for band-limited oscillators.  Following Miller Puckette,
a band-limited square wave is created by constructing a train of band-limited
impulses of alternating sign, then integrating them along time to get pulses.  A
band-limited periodic impulse train can be constructed via direct synthesis: The
Fourier transform of an impulse train is equal-magnitude cosine terms at every
harmonic, so a band-limited impulse is simply the sum of every cosine harmonic
up to the bandwidth limit.  We can write this into a lookup table, then
synthesize a square wave by interpolating the single period to the desired pulse
period, superimposing pulses of alternating sign, then integrating.

The remaining question is how many harmonics to use when creating the table.
Ideally, we want as many harmonics as can fit in the resulting spectrum, without
aliasing.  But the resampling to get different pitches from the same lookup
table mean that the final bandwidth will be different from the original table:
If we interpolate to generate more samples per cycle than in the table, we're
slowing down the waveform, so reducing its bandwidth; if we end up with fewer
samples per cycle than in the table, we're speed it up and expanding its
bandwidth.

To generate full-spectral impulses over a wide range of pitches, we need
multiple band-limited periodic impulse lookup tables, with varying numbers of
harmonics included.  Then, when asked to synthesize a particular pitch, we
choose the table with the greatest bandwidth that will not exceed the Nyquist
(and thus cause aliasing) for that pitch.

The lutset data structure consists of an ordered list of bandlimited waveform
tables of decreasing bandwidth; the bandwidth is explicitly described in the
table (as the number of the highest harmonic present).  (Note that
lower-bandwidth waveforms can be safely represented at a lower sample rate, so
the later tables are generally shorter too).  When seeking to synthesize a
particular pitch, the system steps through the table, calculting the effective
bandwidth after resampling to the desired pitch, and returns the earliest table
that will not cause aliasing.

In addition to a bandlimited impulse (which is used to generate pulse and
sawtooth waves via integration), we also produce another lutset for triangle
waveforms (easier than double-integrating the impulses), and one for sines as
well (even though the sine lutset only has one entry, since sines are implicitly
as bandlimited as they can be).  
"""

import collections
import numpy as np


def cos_lut(table_size, harmonics_weights, harmonics_phases=None):
  if harmonics_phases is None:
    harmonics_phases = np.zeros(len(harmonics_weights))
  table = np.zeros(table_size)
  phases = np.arange(table_size) * 2 * np.pi / table_size
  for harmonic_number, harmonic_weight in enumerate(harmonics_weights):
    table += harmonic_weight * np.cos(
      phases * harmonic_number + harmonics_phases[harmonic_number])
  return table


# Implement the multiple lookup tables.
# A LUT is stored as an array of values (table) and the harmonic number of the
# highest harmonic they contain (i.e., the number of cycles it completes in the
# entire table, so must be <= len(table)/2.)
LUTentry = collections.namedtuple('LUTentry', ['table', 'highest_harmonic'])


# A LUTset is a list of LUTentries describing downsampled versions of the same
# basic waveform, sorted with the longest (highest-bandwidth) first.
def create_lutset(harmonic_weights, harmonic_phases=None, 
                  length_factor=8, bandwidth_factor=np.sqrt(0.5)):
  """Create an ordered list of LUTs with decreasing harmonic content.

  These can then be used in interp_from_lutset to make an adaptive-bandwidth
  interpolation.

  Args:
    harmonic_weights: vector of amplitudes for cosine harmonic components.
    harmonic_phases: initial phases for each harmonic, in radians.  Zero 
      (default) indicates cosine phase.
    length_factor: Each table's length is at least this factor times the order
      of the highest harmonic it contains.  Thus, this is a lower bound on the
      number of samples per cycle for the highest harmonic.  Higher factors make
      the interpolation easier.
    bandwidth_factor: Target ratio between the highest harmonics in successive
      table entries.  Default is sqrt(0.5), so after two tables, bandwidth is
      reduced by 1/2 (and length with follow).

  Returns:
    A list of LUTentry objects, sorted in decreasing order of the highest 
    harmonic they contain.  Each LUT's length is a power of 2, and as small as
    possible while respecting the length_factor for the highest contained 
    harmonic.
  """
  if harmonic_phases is None:
    harmonic_phases = np.zeros(len(harmonic_weights))
  # Calculate the length of the longest LUT we need.  Must be a power of 2, 
  # must have at least length_factor * highest_harmonic samples.
  # Harmonic 0 (dc) doesn't count.
  float_num_harmonics = float(len(harmonic_weights))
  lutsets = []
  done = False
  # harmonic 0 is DC; there's no point in generating that table.
  while float_num_harmonics >= 2:
    num_harmonics = int(round(float_num_harmonics))
    highest_harmonic = num_harmonics - 1  # because zero doesn't count.
    lut_size = int(2 ** np.ceil(np.log(length_factor * highest_harmonic) /
                                np.log(2)))
    print(float_num_harmonics, num_harmonics, lut_size)
    lutsets.append(LUTentry(
        table=cos_lut(lut_size, 
                      harmonic_weights[:num_harmonics], 
                      harmonic_phases[:num_harmonics]),  # / lut_size,
        highest_harmonic=highest_harmonic))
    float_num_harmonics = bandwidth_factor * float_num_harmonics
  return lutsets


def write_lutset_to_h(filename, variable_base, lutset):
  """Savi out a lutset as a C-compatible header file."""
  num_luts = len(lutset)
  with open(filename, "w") as f:
    f.write("// Automatically-generated LUTset\n")
    f.write("#ifndef LUTSET_{:s}_DEFINED\n".format(variable_base.upper()))
    f.write("#define LUTSET_{:s}_DEFINED\n".format(variable_base.upper()))
    f.write("\n")
    # Define the structure.
    f.write("#ifndef LUTENTRY_DEFINED\n")
    f.write("#define LUTENTRY_DEFINED\n")
    f.write("typedef struct {\n")
    f.write("  const float *table;\n")
    f.write("  int table_size;\n")
    f.write("  int highest_harmonic;\n")
    f.write("} lut_entry;\n")
    f.write("#endif // LUTENTRY_DEFINED\n")
    f.write("\n")
    # Define the content of the individual tables.
    samples_per_row = 8
    for i in range(num_luts):
      table_size = len(lutset[i].table)
      f.write("const float {:s}_lutable_{:d}[{:d}] = {{\n".format(
        variable_base, i, table_size))
      for row_start in range(0, table_size, samples_per_row):
        for sample_index in range(row_start, 
                                  min(row_start + samples_per_row, table_size)):
          f.write("{:f},".format(lutset[i].table[sample_index]))
        f.write("\n")
      f.write("};\n")
      f.write("\n")
    # Define the table of LUTs.
    f.write("lut_entry {:s}_lutset[{:d}] = {{\n".format(
      variable_base, num_luts + 1))
    for i in range(num_luts):
      f.write("  {{{:s}_lutable_{:d}, {:d}, {:d}}},\n".format(
        variable_base, i, len(lutset[i].table), 
        lutset[i].highest_harmonic))
    # Final entry is null to indicate end of table.
    f.write("  {NULL, 0, 0},\n")
    f.write("};\n")
    f.write("\n")
    f.write("#endif // LUTSET_x_DEFINED\n")
  print("wrote", filename)

# Impulses.
impulse_lutset = create_lutset(np.ones(128))
write_lutset_to_h('impulse_lutset.h', 'impulse', impulse_lutset)

# Triangle wave lutset
n_harms = 64
coefs = (np.arange(n_harms) % 2) * (
  np.maximum(1, np.arange(n_harms, dtype=float))**(-2))
triangle_lutset = create_lutset(coefs, np.arange(len(coefs)) * -np.pi / 2)
write_lutset_to_h('triangle_lutset.h', 'triangle', triangle_lutset)

# Sinusoid "lutset" (only one table)
sine_lutset = create_lutset(np.array([0, 1]), length_factor=256)
write_lutset_to_h('sine_lutset.h', 'sine', sine_lutset)


  
