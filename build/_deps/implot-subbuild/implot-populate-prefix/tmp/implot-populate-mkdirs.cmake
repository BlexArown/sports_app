# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/sports_app/build/_deps/implot-src"
  "/home/sports_app/build/_deps/implot-build"
  "/home/sports_app/build/_deps/implot-subbuild/implot-populate-prefix"
  "/home/sports_app/build/_deps/implot-subbuild/implot-populate-prefix/tmp"
  "/home/sports_app/build/_deps/implot-subbuild/implot-populate-prefix/src/implot-populate-stamp"
  "/home/sports_app/build/_deps/implot-subbuild/implot-populate-prefix/src"
  "/home/sports_app/build/_deps/implot-subbuild/implot-populate-prefix/src/implot-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/sports_app/build/_deps/implot-subbuild/implot-populate-prefix/src/implot-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/sports_app/build/_deps/implot-subbuild/implot-populate-prefix/src/implot-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
