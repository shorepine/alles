idf_component_register(SRCS alles.c
							multicast.c
							blemidi.c
							midi.c
							buttons.c
							sounds.c
							power.c
							amy/amy.c
							amy/algorithms.c
							amy/oscillators.c
							amy/filters.c
							amy/envelope.c
                    INCLUDE_DIRS amy)

set_source_files_properties(alles.c amy/amy.c
    PROPERTIES COMPILE_FLAGS
    -Wno-strict-aliasing
)

