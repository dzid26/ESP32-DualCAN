# Berry scripting language — ESP-IDF component wrapper
#
# Structure:
#   src/       vendored upstream berry/src/ (minus be_repl.c, be_filelib.c)
#   generate/  coc-generated headers (committed; src/ includes them via
#              #include "../generate/...")
#   port/      our config (berry_conf.h), modtab, and platform port
#   berry/     optional git submodule for updating (not needed to build)
#
# To update vendored sources, see scripts/update_berry.sh.

file(GLOB berry_core_srcs "src/*.c")
file(GLOB berry_port_srcs "port/*.c")

idf_component_register(
    SRCS ${berry_core_srcs} ${berry_port_srcs}
    INCLUDE_DIRS "src" "port"
    REQUIRES log
)
