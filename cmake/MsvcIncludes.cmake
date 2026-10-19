# Detect the compiler's actual UTF-8 include prefix. VSLANG can fall back to a
# localized installation, and an incorrectly decoded prefix breaks Ninja deps.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
  set(NG_PROBE_DIR "${CMAKE_BINARY_DIR}/CMakeFiles/ng-include-probe")
  file(MAKE_DIRECTORY "${NG_PROBE_DIR}")
  file(WRITE "${NG_PROBE_DIR}/ng-include-probe.h" "")
  file(WRITE "${NG_PROBE_DIR}/probe.cpp" "#include \"ng-include-probe.h\"\n")
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c
      "${NG_PROBE_DIR}/probe.cpp" "/Fo${NG_PROBE_DIR}/probe.obj"
    RESULT_VARIABLE NG_PROBE_RESULT OUTPUT_VARIABLE NG_PROBE_OUTPUT
    ERROR_VARIABLE NG_PROBE_ERROR ENCODING UTF-8)
  string(REGEX MATCH "(^|\n)([^\r\n]+: +)[A-Za-z]:[^\r\n]*ng-include-probe.h" NG_PROBE_MATCH "${NG_PROBE_OUTPUT}")
  if(NG_PROBE_RESULT EQUAL 0 AND NG_PROBE_MATCH)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
  else()
    message(FATAL_ERROR "Unable to detect the MSVC include prefix for Ninja")
  endif()
endif()
