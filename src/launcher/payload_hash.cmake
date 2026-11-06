# Writes the SHA256 of each file listed in FILE_LIST to OUTPUT, and only when one of them changed, so the timestamp of
# OUTPUT tells whether the contents of the embedded files changed. File timestamps alone are not enough: installers,
# such as the CopperSpice one, keep the original (older) timestamps of the files they replace.
# usage: cmake -DFILE_LIST=<file with one path per line> -DOUTPUT=<hash file> -P payload_hash.cmake

file(STRINGS "${FILE_LIST}" files)
set(hashes "")
foreach(file IN LISTS files)
  file(SHA256 "${file}" hash)
  string(APPEND hashes "${hash} ${file}\n")
endforeach()

set(previous "")
if (EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" previous)
endif()
if (NOT hashes STREQUAL previous)
  file(WRITE "${OUTPUT}" "${hashes}")
endif()
