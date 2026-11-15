# The archive family (ROADMAP 0.9.0): zip, tar, tar.gz and .gz, read through descriptors. Linked
# into the isolated helper (and tests and development tools), never into Nordend. zlib inflates.
add_library(nordend_formats_archive src/archive.cpp src/stream.cpp src/tar.cpp src/zip.cpp)
add_library(nordend::formats::archive ALIAS nordend_formats_archive)
target_include_directories(nordend_formats_archive PUBLIC include)
target_link_libraries(nordend_formats_archive
    PUBLIC nordend::archive
    PRIVATE PkgConfig::ZLIB nordend::warnings)
