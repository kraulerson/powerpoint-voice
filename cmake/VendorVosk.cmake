# Make the vendored Vosk library actually loadable, WITHOUT modifying the vendored
# artifact (BUG-49).
#
# The shipped file is `libvosk.dyld` and its LC_ID_DYLIB is the bare leaf name
# `libvosk.dylib`. dyld expands LC_RPATH only for install names that begin with
# `@rpath/`, so for a bare leaf no rpath we add is ever consulted — the library
# fails to load at process start, bundle or not. Upstream gets away with it because
# Python's ctypes loads by absolute path, which bypasses install-name resolution
# entirely; C++ linking does not.
#
# So we fix a COPY at build time. third_party/ keeps its pinned, SHA-256-verified
# bytes (third_party/PROVENANCE.md) and the repo stays reproducible.
function(pptv_prepare_vosk OUT_LIB)
  set(_src "${CMAKE_SOURCE_DIR}/third_party/vosk/lib/macos-universal2/libvosk.dyld")
  set(_dst "${CMAKE_BINARY_DIR}/vosk/libvosk.dylib")
  if(NOT EXISTS "${_src}")
    message(FATAL_ERROR "vendored vosk missing: ${_src}")
  endif()
  add_custom_command(
    OUTPUT "${_dst}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vosk"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_dst}"
    # 1. give it an @rpath install name so dyld will consult LC_RPATH at all
    COMMAND install_name_tool -id "@rpath/libvosk.dylib" "${_dst}"
    # 2. re-sign: install_name_tool invalidates the ad-hoc signature, and on arm64
    #    an invalid signature is a hard load failure with a DIFFERENT error, which
    #    is how this wastes an afternoon if you fix only step 1.
    COMMAND codesign -f -s - "${_dst}"
    DEPENDS "${_src}"
    COMMENT "Preparing vendored libvosk (rpath install name + re-sign)"
    VERBATIM)
  add_custom_target(pptv_vosk_lib DEPENDS "${_dst}")
  set(${OUT_LIB} "${_dst}" PARENT_SCOPE)
endfunction()
