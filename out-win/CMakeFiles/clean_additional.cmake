# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "apps\\gf_toolsuite_gui\\CMakeFiles\\gf_toolsuite_gui_autogen.dir\\AutogenUsed.txt"
  "apps\\gf_toolsuite_gui\\CMakeFiles\\gf_toolsuite_gui_autogen.dir\\ParseCache.txt"
  "apps\\gf_toolsuite_gui\\gf_toolsuite_gui_autogen"
  )
endif()
