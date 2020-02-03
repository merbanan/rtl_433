########################################################################
# Build device decoders library
########################################################################

# All decoder sources
file(GLOB DECODER_SRCS *.c)

# commodity libraries
# consider -fvisibility=hidden
# Proper object library type was only introduced with CMake 2.8.8
add_library(r_decoders STATIC
    ${DECODER_SRCS}
)

# Explicitly say that we want C99
set_target_properties(r_decoders PROPERTIES C_STANDARD 99)
