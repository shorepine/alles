idf_component_register(SRCS alles.c
							multicast.c
							buttons.c
							sounds.c
							power.c
							sync.c
							amy/amy.c
							amy/algorithms.c
							amy/oscillators.c
							amy/pcm.c
							amy/filters.c
							amy/envelope.c
							amy/partials.c
							amy/dsps_biquad_f32_ae32.S
                    INCLUDE_DIRS amy)

set_source_files_properties(alles.c amy/amy.c
    PROPERTIES COMPILE_FLAGS
    -Wno-strict-aliasing
)

