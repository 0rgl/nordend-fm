# What Nordend does with an archive (ROADMAP 0.9.0): its entries as plain data. No reader of
# archives: those are in formats/archive, run by the base engine.
add_library(nordend_archive src/archive.cpp)
add_library(nordend::archive ALIAS nordend_archive)
target_include_directories(nordend_archive PUBLIC include)
target_link_libraries(nordend_archive PRIVATE nordend::warnings)
