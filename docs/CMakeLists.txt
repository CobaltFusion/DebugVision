# The user manual: docs/manual.md is converted to HTML at build time and compiled into debugvision
# (--help, Help > User Manual) and dvcli (--webhelp). The converter is md2html from md4c, built from
# its sources (see cmake/Dependencies.cmake).

add_executable(dv_md2html
  ${md4c_SOURCE_DIR}/src/md4c.c
  ${md4c_SOURCE_DIR}/src/md4c-html.c
  ${md4c_SOURCE_DIR}/src/entity.c
  ${md4c_SOURCE_DIR}/md2html/md2html.c
  ${md4c_SOURCE_DIR}/md2html/cmdline.c
)
target_include_directories(dv_md2html PRIVATE ${md4c_SOURCE_DIR}/src)
target_compile_definitions(dv_md2html PRIVATE
  MD_VERSION_MAJOR=0 MD_VERSION_MINOR=5 MD_VERSION_RELEASE=2
  $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

add_custom_command(
  OUTPUT ${DV_MANUAL_HTML}
  COMMAND dv_md2html --github ${CMAKE_CURRENT_SOURCE_DIR}/manual.md -o ${CMAKE_CURRENT_BINARY_DIR}/manual.body.html
  COMMAND ${CMAKE_COMMAND} -DBODY=${CMAKE_CURRENT_BINARY_DIR}/manual.body.html -DTEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/manual.html.in
          -DOUTPUT=${DV_MANUAL_HTML} -P ${PROJECT_SOURCE_DIR}/cmake/MarkdownPage.cmake
  DEPENDS manual.md manual.html.in ${PROJECT_SOURCE_DIR}/cmake/MarkdownPage.cmake dv_md2html
  COMMENT "Converting docs/manual.md to HTML"
)
add_custom_target(dv_manual DEPENDS ${DV_MANUAL_HTML})
