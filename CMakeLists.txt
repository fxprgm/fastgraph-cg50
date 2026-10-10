# FastGraph: a fast function grapher for the fx-CG50. Build with: fxsdk build-cg
cmake_minimum_required(VERSION 3.15)
project(FastGraph)

include(GenerateG3A)
include(Fxconv)
find_package(Gint 2.9 REQUIRED)

set(SOURCES
  src/main.c
  src/expr.c
  src/ff.c
  src/list.c
  src/graph.c
  src/prof.c
)
set(ASSETS_cg
  assets-cg/font8x13b.png
)

fxconv_declare_assets(${ASSETS_cg} WITH_METADATA)

add_executable(fastgraph ${SOURCES} ${ASSETS_cg})
target_compile_options(fastgraph PRIVATE -Wall -Wextra -O2 -g -flto)
target_link_libraries(fastgraph Gint::Gint -lopenlibm)

generate_g3a(TARGET fastgraph OUTPUT "FastGraph.g3a"
  NAME "FastGraph" ICONS assets-cg/icon-uns.png assets-cg/icon-sel.png)
