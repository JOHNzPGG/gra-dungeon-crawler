# CMake generated Testfile for 
# Source directory: D:/Pliki/gra dungeon projekt/gra-dungeon-crawler
# Build directory: D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[unit]=] "D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/build/Debug/tests.exe")
  set_tests_properties([=[unit]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/CMakeLists.txt;98;add_test;D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[unit]=] "D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/build/Release/tests.exe")
  set_tests_properties([=[unit]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/CMakeLists.txt;98;add_test;D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[unit]=] "D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/build/MinSizeRel/tests.exe")
  set_tests_properties([=[unit]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/CMakeLists.txt;98;add_test;D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[unit]=] "D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/build/RelWithDebInfo/tests.exe")
  set_tests_properties([=[unit]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/CMakeLists.txt;98;add_test;D:/Pliki/gra dungeon projekt/gra-dungeon-crawler/CMakeLists.txt;0;")
else()
  add_test([=[unit]=] NOT_AVAILABLE)
endif()
subdirs("_deps/glfw-build")
subdirs("_deps/glm-build")
