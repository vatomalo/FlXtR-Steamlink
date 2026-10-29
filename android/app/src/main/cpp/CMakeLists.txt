cmake_minimum_required(VERSION 3.22)
project(FlXtRAndroid C CXX)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
# The response decoder is built as a shared library, never an Android executable.
set(RESOLVER "${REPO}/build/resolver")
add_library(flxtr_resolver SHARED "${RESOLVER}/img_data.c" "${REPO}/src/resolver_bridge.c"
    "${RESOLVER}/wasm-rt-impl.c" "${RESOLVER}/wasm-rt-mem-impl.c" "${RESOLVER}/wasm-rt-exceptions-impl.c" decoder_jni.c)
target_include_directories(flxtr_resolver PRIVATE "${RESOLVER}" "${REPO}/src")
target_compile_features(flxtr_resolver PRIVATE c_std_11)
target_compile_definitions(flxtr_resolver PRIVATE _POSIX_C_SOURCE=200809L NDEBUG WASM_RT_USE_MMAP=0 WASM_RT_MEMCHECK_BOUNDS_CHECK=1)
target_link_libraries(flxtr_resolver PRIVATE m)
