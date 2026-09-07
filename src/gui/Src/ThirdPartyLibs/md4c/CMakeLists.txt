# https://github.com/mity/md4c/commit/481fbfbdf72daab2912380d62bb5f2187d438408
add_library(md4c-html STATIC
    md4c.c
    md4c.h
    md4c-entity.c
    md4c-entity.h
    md4c-html.c
    md4c-html.h
)
target_include_directories(md4c-html PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
