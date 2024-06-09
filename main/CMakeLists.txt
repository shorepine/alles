idf_component_register(SRCS alles.c
							alles_esp32.c
							multicast_esp32.c
							buttons.c
							sounds.c
							power.c
							../amy/src/log2_exp2.c
							../amy/src/amy.c
							../amy/src/custom.c
							../amy/src/delay.c
							../amy/src/patches.c
							../amy/src/algorithms.c
							../amy/src/oscillators.c
							../amy/src/pcm.c
							../amy/src/filters.c
							../amy/src/envelope.c
							../amy/src/partials.c
                            ../amy/src/delay.c
							../amy/src/dsps_biquad_f32_ae32.S
                    INCLUDE_DIRS ../amy/src .)


list(APPEND IDF_COMPONENTS
	mdns
)

target_compile_definitions(${COMPONENT_TARGET} PUBLIC "-DALLES")

set_source_files_properties(alles_esp32.c alles.c ../amy/src/amy.c
    PROPERTIES COMPILE_FLAGS
    -Wno-strict-aliasing
)

