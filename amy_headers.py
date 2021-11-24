# amy_headers.py
# Generate headers for libAMY



def generate_alles_pcm_header(pcm_sample_rate=22050):
    from sf2utils.sf2parse import Sf2File
    import resampy
    import numpy as np
    import struct
    # These are the indexes that we liked and fit into the flash on Alles ESP32. You can download the sf2 files here:
    # https://github.com/vigliensoni/soundfonts/blob/master/hs_tr808/HS-TR-808-Drums.sf2
    # https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/MuseScore_General.sf2
    # Put them in the sounds/ folder. 
    fns = ( ("sounds/HS-TR-808-Drums.sf2", False), ('sounds/MuseScore_General.sf2', True))
    good = [0, 3, 8, 11, 14, 16, 17, 18, 20, 23, 25, 26, 29, 30, 31, 32, 37, 39, 40, 42, 47, 49, 50, 52, 58, 63, 69, 74, 76, 80, 83, 85, 86, 95, 96, 99, 100, 101, 107, 108, 109, 112, 116, 117, 118, 120, 127, \
             130, 134, 136, 145, 149, 155, 161, 165, 166, 170, 171, 175, 177, 178, 183, 192, 197, 198, 200, 204]
    offsets = []
    offset = 0
    int16s = []
    samples = []
    sample_counter = 0
    my_sample_counter = 0
    orig_map = {}
    for (fn, is_inst) in fns:
        sf2 = Sf2File(open(fn, 'rb'))
        if is_inst:
            for i,inst in enumerate(sf2.instruments[:-1]):
                b = inst.bags[int(len(inst.bags)/2)]
                if(sample_counter in good):
                    samples.append(b.sample)
                    orig_map[my_sample_counter] = sample_counter
                    my_sample_counter += 1
                sample_counter += 1
        else:
            for sample in sf2.samples[:-1]:
                if(sample_counter in good):
                    samples.append(sample)
                    orig_map[my_sample_counter] = sample_counter
                    my_sample_counter += 1
                sample_counter += 1
    for sample in samples:
        try:
            s = {}
            s["name"] = sample.name
            floaty =(np.frombuffer(bytes(sample.raw_sample_data),dtype='int16'))/32768.0
            resampled = resampy.resample(floaty, sample.sample_rate, pcm_sample_rate)
            samples_int16 = np.int16(resampled*32768)
            #floats.append(resampled)
            int16s.append(samples_int16)
            s["offset"] = offset 
            s["length"] = resampled.shape[0]
            s["loopstart"] = int(float(sample.start_loop) / float(sample.sample_rate / pcm_sample_rate))
            s["loopend"] = int(float(sample.end_loop) / float(sample.sample_rate / pcm_sample_rate))
            s["midinote"] = sample.original_pitch
            offset = offset + resampled.shape[0]
            offsets.append(s)
        except AttributeError:
            print("skipping %s" % (sample.name))
    
    all_samples = np.hstack(int16s)
    # Write packed .bin file of pcm[] as well as .h file to write as an ESP32 binary partition
    b = open("main/amy/pcm.bin", "wb")
    for i in range(all_samples.shape[0]):
        b.write(struct.pack('<h', all_samples[i]))
    b.close()
    p = open("main/amy/pcm.h", "w")
    p.write("// Automatically generated by amy_headers.generate_pcm_header()\n")
    p.write("#ifndef __PCM_H\n#define __PCM_H\n")
    p.write("#define PCM_SAMPLES %d\n#define PCM_LENGTH %d\n#define PCM_SAMPLE_RATE %d\n" % (len(offsets), all_samples.shape[0], pcm_sample_rate))
    p.write("const pcm_map_t pcm_map[%d] = {\n" % (len(offsets)))
    for i,o in enumerate(offsets):
        p.write("    /* [%d] %d */ {%d, %d, %d, %d, %d}, /* %s */\n" %(i, orig_map[i], o["offset"], o["length"], o["loopstart"], o["loopend"], o["midinote"], o["name"]))
    p.write("};\n")
    p.write("\n#endif  // __PCM_H\n")
    p.close()

    p = open("main/amy/pcm_desktop.h", 'w')
    p.write("// Automatically generated by amy_headers.generate_pcm_header()\n")
    p.write("#ifndef __PCM_DESKTOP_H\n#define __PCM_DESKTOP_H\n")
    p.write("const int16_t pcm_desktop[%d] = {\n" % (all_samples.shape[0]))
    column = 15
    count = 0
    for i in range(int(all_samples.shape[0]/column)):
        p.write("    %s,\n" % (",".join([("%d" % (d)).ljust(8) for d in all_samples[i*column:(i+1)*column]])))
        count = count + column
    print("count %d all_samples.shape %d" % (count, all_samples.shape[0]))
    if(count != all_samples.shape[0]):
        p.write("    %s\n" % (",".join([("%d" % (d)).ljust(8) for d in all_samples[count:]])))
    p.write("};\n")
    p.write("\n#endif  // __PCM_DESKTOP_H\n")



def cos_lut(table_size, harmonics_weights, harmonics_phases=None):
    import numpy as np
    if harmonics_phases is None:
        harmonics_phases = np.zeros(len(harmonics_weights))
    table = np.zeros(table_size)
    phases = np.arange(table_size) * 2 * np.pi / table_size
    for harmonic_number, harmonic_weight in enumerate(harmonics_weights):
        table += harmonic_weight * np.cos(
            phases * harmonic_number + harmonics_phases[harmonic_number])
    return table




# A LUTset is a list of LUTentries describing downsampled versions of the same
# basic waveform, sorted with the longest (highest-bandwidth) first.
def create_lutset(LUTentry, harmonic_weights, harmonic_phases=None, 
                                    length_factor=8, bandwidth_factor=None):
    import numpy as np
    if bandwidth_factor is None:
        bandwidth_factor = np.sqrt(0.5)
    """Create an ordered list of LUTs with decreasing harmonic content.

    These can then be used in interp_from_lutset to make an adaptive-bandwidth
    interpolation.

    Args:
        harmonic_weights: vector of amplitudes for cosine harmonic components.
        harmonic_phases: initial phases for each harmonic, in radians. Zero 
            (default) indicates cosine phase.
        length_factor: Each table's length is at least this factor times the order
            of the highest harmonic it contains. Thus, this is a lower bound on the
            number of samples per cycle for the highest harmonic. Higher factors make
            the interpolation easier.
        bandwidth_factor: Target ratio between the highest harmonics in successive
            table entries. Default is sqrt(0.5), so after two tables, bandwidth is
            reduced by 1/2 (and length with follow).

    Returns:
        A list of LUTentry objects, sorted in decreasing order of the highest 
        harmonic they contain. Each LUT's length is a power of 2, and as small as
        possible while respecting the length_factor for the highest contained 
        harmonic.
    """
    if harmonic_phases is None:
        harmonic_phases = np.zeros(len(harmonic_weights))
    # Calculate the length of the longest LUT we need. Must be a power of 2, 
    # must have at least length_factor * highest_harmonic samples.
    # Harmonic 0 (dc) doesn't count.
    float_num_harmonics = float(len(harmonic_weights))
    lutsets = []
    done = False
    # harmonic 0 is DC; there's no point in generating that table.
    while float_num_harmonics >= 2:
        num_harmonics = int(round(float_num_harmonics))
        highest_harmonic = num_harmonics - 1    # because zero doesn't count.
        lut_size = int(2 ** np.ceil(np.log(length_factor * highest_harmonic) / np.log(2)))
        lutsets.append(LUTentry(
                table=cos_lut(lut_size, harmonic_weights[:num_harmonics], 
                                harmonic_phases[:num_harmonics]),    # / lut_size,
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
        f.write("    const float *table;\n")
        f.write("    int table_size;\n")
        f.write("    int highest_harmonic;\n")
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
            f.write("    {{{:s}_lutable_{:d}, {:d}, {:d}}},\n".format(
                variable_base, i, len(lutset[i].table), 
                lutset[i].highest_harmonic))
        # Final entry is null to indicate end of table.
        f.write("    {NULL, 0, 0},\n")
        f.write("};\n")
        f.write("\n")
        f.write("#endif // LUTSET_x_DEFINED\n")
    print("wrote", filename)



def make_clipping_lut(filename):
    import numpy as np
    # Soft clipping lookup table scratchpad.
    SAMPLE_MAX = 32767
    LIN_MAX = 29491  #// int(round(0.9 * 32768))
    NONLIN_RANGE = 4915  # // size of nonlinearity lookup table = round(1.5 * (INT16_MAX - LIN_MAX))

    clipping_lookup_table = np.arange(LIN_MAX + NONLIN_RANGE)

    for x in range(NONLIN_RANGE):
        x_dash = float(x) / NONLIN_RANGE
        clipping_lookup_table[x + LIN_MAX] = LIN_MAX + int(np.floor(NONLIN_RANGE * (x_dash - x_dash * x_dash * x_dash / 3.0)))

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


def generate_all():
    import numpy as np
    import collections
    # Implement the multiple lookup tables.
    # A LUT is stored as an array of values (table) and the harmonic number of the
    # highest harmonic they contain (i.e., the number of cycles it completes in the
    # entire table, so must be <= len(table)/2.)
    LUTentry = collections.namedtuple('LUTentry', ['table', 'highest_harmonic'])

    # Impulses.
    impulse_lutset = create_lutset(LUTentry, np.ones(128))
    write_lutset_to_h('main/amy/impulse_lutset.h', 'impulse', impulse_lutset)

    # Triangle wave lutset
    n_harms = 64
    coefs = (np.arange(n_harms) % 2) * (
        np.maximum(1, np.arange(n_harms, dtype=float))**(-2))
    triangle_lutset = create_lutset(LUTentry, coefs, np.arange(len(coefs)) * -np.pi / 2)
    write_lutset_to_h('main/amy/triangle_lutset.h', 'triangle', triangle_lutset)

    # Sinusoid "lutset" (only one table)
    sine_lutset = create_lutset(LUTentry, np.array([0, 1]),  harmonic_phases = -np.pi / 2 * np.ones(2), length_factor=256)
    write_lutset_to_h('main/amy/sine_lutset.h', 'sine', sine_lutset)

    # Clipping LUT
    make_clipping_lut('main/amy/clipping_lookup_table.h')

    # PCM
    generate_alles_pcm_header()

    # Partials and FM are now in partials.py and fm.py






