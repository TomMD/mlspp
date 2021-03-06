###
### Environment Config
###

cmake_minimum_required(VERSION 3.3)

project(mlspp)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenSSL
find_package(OpenSSL 1.1 REQUIRED)

# Maybe set up linters
option(MLSPP_LINT "Whether to run linters" OFF)

if(MLSPP_LINT)
  message(STATUS "Enabling linters...")

  # Find clang-tidy
  find_program(
    CLANG_TIDY_EXE
    NAMES "clang-tidy"
    DOC "Path to clang-tidy executable"
    )
  if(NOT CLANG_TIDY_EXE)
    message(STATUS "clang-tidy not found.")
  else()
    set(DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-checks=*,-clang-analyzer-alpha.*,-fuchsia-overloaded-operator,-fuchsia-default-arguments")
  endif()
  
  # Enable pedantic mode
  # XXX(rlb@ipv.sx): I would include -Wextra here, but it fails on
  # gtest.  The right answer is probably just to apply -Werror to the
  # library and not to the tests, since the library is where these
  # things matter most.
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Werror -Wshadow -pedantic-errors")
  
  # Enable ASan and UBSan (TSan not relevant; MSan Linux-only)
  set(DEBUG_FLAGS "-fPIE -fno-omit-frame-pointer -g")
  set(SANITIZERS "-fno-sanitize-recover=all -fsanitize=undefined -fsanitize=address") 
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${DEBUG_FLAGS} ${SANITIZERS}")
  set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} ${DEBUG_FLAGS} ${SANITIZERS}")
else()
  message(STATUS "Linters disabled!")
endif()

###
### Library Config
###

set(LIB_NAME "${PROJECT_NAME}")

set(LIBRARY_SRC_PATH  "${PROJECT_SOURCE_DIR}/src" )
set(LIBRARY_INCLUDE_PATH  "${PROJECT_SOURCE_DIR}/include")

file(GLOB LIB_SOURCE_FILES "${LIBRARY_SRC_PATH}/*.cpp")

add_library(${LIB_NAME} STATIC ${LIB_SOURCE_FILES})
target_include_directories(${LIB_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR}
                                       PRIVATE ${LIBRARY_INCLUDE_PATH})
target_link_libraries(${LIB_NAME} ${OPENSSL_LIBRARIES})

if(CLANG_TIDY_EXE)
  set_target_properties(
    ${LIB_NAME} PROPERTIES
    CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
  )
endif()

###
### Tests
###

enable_testing()
add_subdirectory(test)


###
### Applications
###

add_subdirectory(cmd)
