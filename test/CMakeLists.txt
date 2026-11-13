cmake_minimum_required(VERSION 3.5...3.30)

include_directories( ${CMAKE_CURRENT_SOURCE_DIR} )

# meshopt decoding layer (KHR/EXT_meshopt_compression): meshoptimizer codecs plus
# the C-ABI adapter that implements cgltf's cgltf_meshopt_decode_hook.
#
# This must be an OBJECT library: cgltf references the hook through a weak symbol,
# and the linker does not extract static-archive members to satisfy weak undefined
# references, which would silently leave the hook NULL. Object files are always
# pulled into the consuming target instead.
set( MESHOPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/meshoptimizer )
add_library( cgltf_meshopt OBJECT
    ${MESHOPT_DIR}/cgltf_meshopt.cpp
    ${MESHOPT_DIR}/vertexcodec.cpp
    ${MESHOPT_DIR}/indexcodec.cpp
    ${MESHOPT_DIR}/vertexfilter.cpp )
target_include_directories( cgltf_meshopt PUBLIC ${MESHOPT_DIR} )
target_compile_definitions( cgltf_meshopt PRIVATE
    CGLTF_MESHOPT_HEADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../cgltf.h" )
if(MSVC)
    target_compile_options(cgltf_meshopt PRIVATE /W3)
    target_compile_definitions(cgltf_meshopt PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(cgltf_meshopt PRIVATE -Wall -Wextra -pedantic)
endif()

# Test executables parse glTF through cgltf only; including the decoder objects makes
# the strong hook definition override cgltf's weak NULL default at link time.
function( cgltf_link_meshopt EXE_NAME )
    target_sources( ${EXE_NAME} PRIVATE $<TARGET_OBJECTS:cgltf_meshopt> )
    target_include_directories( ${EXE_NAME} PRIVATE ${MESHOPT_DIR} )
endfunction()

set( EXE_NAME cgltf_test )
add_executable( ${EXE_NAME} main.c )
set_property( TARGET ${EXE_NAME} PROPERTY C_STANDARD 99 )
cgltf_link_meshopt( ${EXE_NAME} )
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /W4 /WX)
    add_definitions( -D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${EXE_NAME} PUBLIC -fsanitize=address)
    target_link_options(${EXE_NAME} PUBLIC -fsanitize=address)
endif()
install( TARGETS ${EXE_NAME} RUNTIME DESTINATION bin )

set( EXE_NAME test_conversion )
add_executable( ${EXE_NAME} test_conversion.cpp )
set_property( TARGET ${EXE_NAME} PROPERTY CXX_STANDARD 11 )
cgltf_link_meshopt( ${EXE_NAME} )
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /W4 /WX)
    add_definitions( -D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${EXE_NAME} PUBLIC -fsanitize=address)
    target_link_options(${EXE_NAME} PUBLIC -fsanitize=address)
endif()
install( TARGETS ${EXE_NAME} RUNTIME DESTINATION bin )

set( EXE_NAME test_write )
add_executable( ${EXE_NAME} test_write.cpp )
set_property( TARGET ${EXE_NAME} PROPERTY CXX_STANDARD 11 )
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /W4 /WX)
    add_definitions( -D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${EXE_NAME} PUBLIC -fsanitize=address)
    target_link_options(${EXE_NAME} PUBLIC -fsanitize=address)
endif()
install( TARGETS ${EXE_NAME} RUNTIME DESTINATION bin )

set( EXE_NAME test_write_glb )
add_executable( ${EXE_NAME} test_write_glb.cpp )
set_property( TARGET ${EXE_NAME} PROPERTY CXX_STANDARD 11 )
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /W4 /WX)
    add_definitions( -D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${EXE_NAME} PUBLIC -fsanitize=address)
    target_link_options(${EXE_NAME} PUBLIC -fsanitize=address)
endif()
install( TARGETS ${EXE_NAME} RUNTIME DESTINATION bin )

set( EXE_NAME test_math )
add_executable( ${EXE_NAME} test_math.cpp )
set_property( TARGET ${EXE_NAME} PROPERTY CXX_STANDARD 11 )
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /W4 /WX)
    add_definitions( -D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${EXE_NAME} PUBLIC -fsanitize=address)
    target_link_options(${EXE_NAME} PUBLIC -fsanitize=address)
endif()
install( TARGETS ${EXE_NAME} RUNTIME DESTINATION bin )

set( EXE_NAME test_strings )
add_executable( ${EXE_NAME} test_strings.cpp )
set_property( TARGET ${EXE_NAME} PROPERTY CXX_STANDARD 11 )
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /W4 /WX)
    add_definitions( -D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${EXE_NAME} PUBLIC -fsanitize=address)
    target_link_options(${EXE_NAME} PUBLIC -fsanitize=address)
endif()
install( TARGETS ${EXE_NAME} RUNTIME DESTINATION bin )

# Fixture generator: emits the ref/ext glTF pair using the meshoptimizer encoder.
set( FIXTURE_GEN gen_meshopt_fixtures )
add_executable( ${FIXTURE_GEN}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_meshopt_fixtures.cpp
    ${MESHOPT_DIR}/vertexcodec.cpp
    ${MESHOPT_DIR}/indexcodec.cpp
    ${MESHOPT_DIR}/vertexfilter.cpp )
target_include_directories( ${FIXTURE_GEN} PRIVATE ${MESHOPT_DIR} )
target_compile_features( ${FIXTURE_GEN} PRIVATE cxx_std_11 )
if(NOT MSVC)
    target_compile_options(${FIXTURE_GEN} PRIVATE -Wall -Wextra -pedantic)
endif()

# End-to-end meshopt decode tests, driven by the generated fixtures.
set( EXE_NAME test_meshopt )
add_executable( ${EXE_NAME} test_meshopt.cpp )
set_property( TARGET ${EXE_NAME} PROPERTY CXX_STANDARD 11 )
cgltf_link_meshopt( ${EXE_NAME} )
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /W4 /WX)
    target_compile_definitions(${EXE_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${EXE_NAME} PUBLIC -fsanitize=address)
    target_link_options(${EXE_NAME} PUBLIC -fsanitize=address)
endif()
add_custom_target( run_meshopt_tests
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/meshopt-fixtures
    COMMAND $<TARGET_FILE:${FIXTURE_GEN}> ${CMAKE_BINARY_DIR}/meshopt-fixtures
    COMMAND $<TARGET_FILE:${EXE_NAME}> ${CMAKE_BINARY_DIR}/meshopt-fixtures
    DEPENDS ${EXE_NAME} ${FIXTURE_GEN}
    VERBATIM )

# Same fixture, but with the decode pass compiled out: compressed files must fail
# with cgltf_result_unsupported_extension while uncompressed files stay readable.
set( EXE_NAME cgltf_test_nohook )
add_executable( ${EXE_NAME} main.c )
set_property( TARGET ${EXE_NAME} PROPERTY C_STANDARD 99 )
target_compile_definitions( ${EXE_NAME} PRIVATE CGLTF_MESHOPT_DECODE=0 )
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /W4 /WX)
    target_compile_definitions(${EXE_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(${EXE_NAME} PUBLIC -fsanitize=address)
    target_link_options(${EXE_NAME} PUBLIC -fsanitize=address)
endif()
