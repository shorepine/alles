"""Simulates the DX7 FM algorithms fully in Python."""

import numpy as np
import scipy.signal  # for sosfilt HPF

import fm


#### Pure-python DX7 simulation (emulates what Alles does).
# import matplotlib.pyplot as plt
# import fm
# import dx7_simulator
# 
# patch_num = 1  # E.PNO 13.1
# dx7_patch = fm.DX7Patch.from_patch_number(patch_num)
# wave = dx7_simulator.synth_dx7_patch(dx7_patch)
# plt.plot(wave)
# fm.play_np_array(0.1*wave)

def calc_loglin_eg_env(breakpoints, keyup_time=0.5, frame_rate=1000, do_exp=True):
    """Take alles breakpoints derived from DX7 rate,level parameters and generate the actual envelope."""
    # This is what alles has to do to reconstruct DX7 envelopes from the set of breakpoints.
    current_level = 0
    current_time = 0
    last_target_time = 0
    output_levels = np.zeros(0)
    for segment, breakpoint in enumerate(breakpoints):
        # Work in DX7 levels.
        target_level = fm.linear_to_dx7level(breakpoint[1])
        target_time = breakpoint[0]
        release_segment = (segment == len(breakpoints) - 1)
        if release_segment:
            # Release time is diff between last two bps, regardless of elapsed time.
            segment_duration = target_time - last_target_time
            # Make the release segment have at least one sample
            segment_duration = np.maximum(segment_duration, 1/frame_rate)
            # If we've reached the release segment but the key is still down,
            # insert some constant-level envelope until we hit keyup time.
            if current_time < keyup_time:
                output_levels = np.concatenate(
                    [output_levels, 
                     current_level * np.ones(int(frame_rate * (keyup_time - current_time)))])
                current_time = keyup_time 
        else:
            segment_duration = target_time - current_time
        segment_was_truncated = False
        if np.round(segment_duration * frame_rate) > 0:  # Can be < 0 because current time is quantized.
            if target_time > keyup_time and not release_segment:
                # If we hit keyup_time but we're not yet in the release segment, 
                # truncate this segment and move on (so we can get to release).
                actual_seg_duration = segment_duration - (target_time - keyup_time)
                segment_was_truncated = True
            else:
                actual_seg_duration = segment_duration
            sample_time_within_segment = np.arange(np.round(actual_seg_duration * frame_rate)) / frame_rate

            MIN_LEVEL = 34
            ATTACK_RANGE = 75
            
            def map_attack_level(level):
                """Convenience to map the upside-down offset attack exponential to the equivalent level on a decaying exponential."""
                # This is used when calculating the time constant from the initial and final levels and the segment duration.
                return 1 - np.maximum(level - MIN_LEVEL, 0) / ATTACK_RANGE

            if target_level > current_level:  # Attack segment
                # Derive t_const from delta_t and delta_level
                # if L = a.exp(-t/t_c)
                # then L1/L0 = exp(-t1/t_c)/exp(-t0/t_c)
                # so log(L1/L0) = (-t1/t_c) - (-t0/t_c) = (t0 - t1) /t_c
                # so t_c = (t1 - t0) / log(L0 / L1)
                # Have to convert the L = A + B (1 - exp(-kt)) levels into exp(-kt) = 1 - (L - A) / B levels
                mapped_current_level = map_attack_level(current_level)
                mapped_target_level = map_attack_level(target_level)
                t_const = segment_duration / np.log(mapped_current_level / mapped_target_level)
                # We start from the time that matches the current level.
                # L0 = exp(-t0 / t_c), so t0 = -t_c * log(L0)
                t0 = -t_const * np.log(mapped_current_level)
                # This is the magic equation that shapes the DX7 attack envelopes.
                samples = MIN_LEVEL + ATTACK_RANGE * (1 - np.exp(-(t0 + sample_time_within_segment)/t_const))
                #print("current_level=", current_level, "target_level=", target_level, 
                #      "t0=", t0, "tc=", t_const, "time=", target_time)
            else:
                level_change_per_sec = (target_level - current_level) / segment_duration
                samples = current_level + level_change_per_sec * sample_time_within_segment
                #print("current_level=", current_level, "target_level=", target_level, 
                #      "chgpersec=", level_change_per_sec, "time=", target_time)
            output_levels = np.concatenate([output_levels, samples])
        current_time = len(output_levels) / frame_rate
        if segment_was_truncated:
            # If we stopped early, start next segment from whereever we got to.
            current_level = output_levels[-1]
        else:
            # Normal segment completion, act like we got to the target level (to avoid errors sampling sharp peaks).
            current_level = target_level
        last_target_time = target_time
    if not do_exp:
        return output_levels
    else:
        return 2 ** ((output_levels - 99) / 8)

# Basic FM with feedback, vectorized except when feedback is nonzero
def fm_waveform(freq, amp=1.0, freq_mod=None, duration=1.0, sr=44100, feedback=0):
    """Render an FM waveform from raw parameters"""
    t = np.arange(int(sr * duration)) / sr
    if np.isscalar(amp):
        amp = amp * np.ones(len(t))
    if np.isscalar(freq):
        freq = freq * np.ones(len(t))
    dphi_dt = 2 * np.pi * freq / sr
    phi = np.cumsum(dphi_dt)
    if freq_mod is None:
        freq_mod = np.zeros(len(t))
    if feedback == 0:
        # No feedback, vectorize.
        carrier = amp * np.cos(phi + 2 * np.pi * freq_mod)
    else:
        # Have to do slow loop to calculate feedback sample-by-sample.
        carrier = np.zeros(len(phi))
        last_two = np.zeros(2)
        for i in range(len(phi)):
            sample = np.cos(phi[i] + 2 * np.pi * (freq_mod[i] + feedback * np.mean(last_two)))
            last_two[1] = last_two[0]
            last_two[0] = sample
            carrier[i] = amp[i] * sample
    return carrier

def dx7_op_waveform(op, f0=440, mod=None, sr=44100, duration=1.0, keyup_time=0.5, feedback=0):
    """Render an FM waveform from the DX7Operator structure."""
    n_samps = int(duration * sr)
    if op.ratiotuning:
        frequency = f0 * fm.coarse_fine_ratio(op.freq_coarse, op.freq_fine, op.freq_detune)
    else:
        frequency = fm.coarse_fine_fixed_hz(op.freq_coarse, op.freq_fine, op.freq_detune)
    if op.rates is not None:
        bps = fm.calc_loglin_eg_breakpoints(op.rates, op.levels)
        env = calc_loglin_eg_env(bps, frame_rate=sr, do_exp=True, keyup_time=keyup_time)
    else:
        env = np.ones(n_samps)
    if len(env) < n_samps:
        env = np.concatenate([env, fm.dx7level_to_linear(op.levels[-1]) * np.ones(n_samps - len(env))])
    else:
        env = env[:n_samps]
    env *= 2 * fm.dx7level_to_linear(op.opamp)    
    carrier = fm_waveform(frequency, amp=env, 
                          freq_mod=mod, sr=sr, duration=duration, feedback=feedback)
    print("Op: rates:", op.rates, "levels:", op.levels, "freq: %.1f" % frequency, "fb", feedback, "amp:", op.opamp)
    return carrier

# Thank you MFSA for the DX7 op structure , borrowed here \/ \/ \/ 
# algorithms[algo][operator] gives bits from FmOperatorFlags describing each algo
algorithms = [
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x01],  # 0 
    [0xc1, 0x11, 0x11, 0x14, 0x01, 0x14],  # 1
    [0x01, 0x11, 0x11, 0x14, 0xc1, 0x14],  # 2
    [0xc1, 0x11, 0x14, 0x01, 0x11, 0x14],  # 3
    [0x41, 0x11, 0x94, 0x01, 0x11, 0x14],  # 4
    [0xc1, 0x14, 0x01, 0x14, 0x01, 0x14],  # 5
    [0x41, 0x94, 0x01, 0x14, 0x01, 0x14],  # 6
    [0xc1, 0x11, 0x05, 0x14, 0x01, 0x14],  # 7
    [0x01, 0x11, 0xc5, 0x14, 0x01, 0x14],  # 8
    [0x01, 0x11, 0x05, 0x14, 0xc1, 0x14],  # 9
    [0x01, 0x05, 0x14, 0xc1, 0x11, 0x14],  # 10
    [0xc1, 0x05, 0x14, 0x01, 0x11, 0x14],  # 11
    [0x01, 0x05, 0x05, 0x14, 0xc1, 0x14],  # 12
    [0xc1, 0x05, 0x05, 0x14, 0x01, 0x14],  # 13
    [0xc1, 0x05, 0x11, 0x14, 0x01, 0x14],  # 14
    [0x01, 0x05, 0x11, 0x14, 0xc1, 0x14],  # 15
    [0xc1, 0x11, 0x02, 0x25, 0x05, 0x14],  # 16
    [0x01, 0x11, 0x02, 0x25, 0xc5, 0x14],  # 17
    [0x01, 0x11, 0x11, 0xc5, 0x05, 0x14],  # 18
    [0xc1, 0x14, 0x14, 0x01, 0x11, 0x14],  # 19
    [0x01, 0x05, 0x14, 0xc1, 0x14, 0x14],  # 20
    [0x01, 0x14, 0x14, 0xc1, 0x14, 0x14],  # 21
    [0xc1, 0x14, 0x14, 0x14, 0x01, 0x14],  # 22
    [0xc1, 0x14, 0x14, 0x01, 0x14, 0x04],  # 23
    [0xc1, 0x14, 0x14, 0x14, 0x04, 0x04],  # 24
    [0xc1, 0x14, 0x14, 0x04, 0x04, 0x04],  # 25
    [0xc1, 0x05, 0x14, 0x01, 0x14, 0x04],  # 26
    [0x01, 0x05, 0x14, 0xc1, 0x14, 0x04],  # 27
    [0x04, 0xc1, 0x11, 0x14, 0x01, 0x14],  # 28
    [0xc1, 0x14, 0x01, 0x14, 0x04, 0x04],  # 29
    [0x04, 0xc1, 0x11, 0x14, 0x04, 0x04],  # 30
    [0xc1, 0x14, 0x04, 0x04, 0x04, 0x04],  # 31
    [0xc4, 0x04, 0x04, 0x04, 0x04, 0x04],  # 32
]
# FmOperatorFlags
OUT_BUS_ONE = 0x01
OUT_BUS_TWO = 0x02
OUT_BUS_ADD = 0x04
# there is no 1 << 3
IN_BUS_ONE = 0x10
IN_BUS_TWO = 0x20
FB_IN = 0x40
FB_OUT = 0x80

# TODO: Add pitchenv
# TODO: Add LFO

def synth_dx7_patch(patch, f0=440, sr=44100, duration=1.0, keyup_time=0.5):
    num_samples = int(sr * duration)
    bus_one = np.zeros(num_samples)
    bus_two = np.zeros(num_samples)
    bus_add = np.zeros(num_samples)
    print('algo=', patch.algo)
    for opnum, op in enumerate(patch.ops):
        print('op=', 6 - opnum)
        opflags = algorithms[patch.algo][opnum]
        mod_in = np.zeros(num_samples)
        feedback = 0
        if opflags & IN_BUS_ONE:
            mod_in = bus_one
            print('mod_in = bus_one')
        if opflags & IN_BUS_TWO:
            mod_in = bus_two
            print('mod_in = bus_two')
        if opflags & FB_IN:
            feedback = 0.00125 * (2 ** patch.feedback)
            print('fb=', feedback)
            if (opflags & FB_OUT) == 0:
                print("**warning: FB_OUT different from FB_IN")
        samples = dx7_op_waveform(op, f0=f0, mod=mod_in, sr=sr, duration=duration, keyup_time=keyup_time, 
                                  feedback=feedback)
        if opflags & OUT_BUS_ONE:
            if opflags & OUT_BUS_ADD:
                # OUT_BUS_ADD | OUT_BUS_X means add on to BUS_X
                bus_one = (bus_one + samples)
                print('bus_one = bus_one + samples')
            else:
                bus_one = samples
                print('bus_one = samples')
        if opflags & OUT_BUS_TWO:
            if opflags & OUT_BUS_ADD:
                # OUT_BUS_ADD | OUT_BUS_X means add on to BUS_X
                bus_two = (bus_two + samples)
                print('bus_two = bus_two + samples')
            else:
                bus_two = samples
                print('bus_two = samples')
        if (opflags & OUT_BUS_ADD) and ((opflags & (OUT_BUS_ONE | OUT_BUS_TWO)) == 0):
            # Bare OUT_BUS_ADD (0x4).
            bus_add += samples
            print('bus_add += samples')
    # Apply HPF - 0.003/3.14 * 22050 = 22 Hz
    bus_add = scipy.signal.sosfilt([[1, -1, 0, 1, -0.995, 0]], bus_add)
    return bus_add

