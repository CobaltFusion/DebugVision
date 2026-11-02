# dvcli --stdin exits at the end of the input and writes every line, also the last one without a newline
# usage: cmake -DDVCLI=<path of dvcli> -P test_stdin_end.cmake

execute_process(
  COMMAND ${DVCLI} --stdin -c
  INPUT_FILE ${CMAKE_CURRENT_LIST_DIR}/test_stdin_end.txt
  OUTPUT_VARIABLE out
  RESULT_VARIABLE rc
  TIMEOUT 20
)
if(NOT rc STREQUAL "0")
  message(FATAL_ERROR "dvcli did not exit at the end of the input (result: ${rc})\n${out}")
endif()
foreach(line "first line of the input" "last line without a newline")
  string(FIND "${out}" "${line}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "'${line}' is missing from the output:\n${out}")
  endif()
endforeach()
