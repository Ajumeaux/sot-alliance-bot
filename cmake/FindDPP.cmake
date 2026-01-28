# Cherche le header principal de DPP
find_path(DPP_INCLUDE_DIR NAMES dpp/dpp.h)

# Cherche la lib (libdpp.so ou libdpp.a)
find_library(DPP_LIBRARIES NAMES dpp)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
    DPP DEFAULT_MSG
    DPP_LIBRARIES
    DPP_INCLUDE_DIR
)
