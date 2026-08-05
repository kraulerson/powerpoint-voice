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
  # Linux ships a normal libvosk.so that links and loads without help; only macOS
  # needs the install-name repair below. The engine compiles on BOTH, so it must
  # link on both — putting this behind if(APPLE) is what broke the Ubuntu CI build.
  if(APPLE)
    set(_src "${CMAKE_SOURCE_DIR}/third_party/vosk/lib/macos-universal2/libvosk.dyld")
    set(_dst "${CMAKE_BINARY_DIR}/vosk/libvosk.dylib")
  elseif(UNIX)
    set(_src "${CMAKE_SOURCE_DIR}/third_party/vosk/lib/linux-x86_64/libvosk.so")
    set(_dst "${CMAKE_BINARY_DIR}/vosk/libvosk.so")
  else()
    message(FATAL_ERROR "no vendored vosk for this platform")
  endif()
  if(NOT EXISTS "${_src}")
    message(FATAL_ERROR "vendored vosk missing: ${_src}")
  endif()

  if(NOT APPLE)
    add_custom_command(
      OUTPUT "${_dst}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vosk"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_dst}"
      DEPENDS "${_src}"
      COMMENT "Staging vendored libvosk"
      VERBATIM)
    add_custom_target(pptv_vosk_lib DEPENDS "${_dst}")
    set(${OUT_LIB} "${_dst}" PARENT_SCOPE)
    return()
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

# Extract the speech model into the app bundle at BUILD time (BUG-50).
#
# It ships as a 39 MB zip and nothing copied it anywhere, so it was not in the
# bundle at all. Extraction must happen at build time, not first run: TM-011 forbids
# writing a cache to disk during a talk, and a first-launch unzip of 39 MB in front
# of an audience is exactly the kind of delay that has no upside.
function(pptv_prepare_vosk_model OUT_DIR)
  set(_zip "${CMAKE_SOURCE_DIR}/third_party/vosk/model/vosk-model-small-en-us-0.15.zip")
  set(_out "${CMAKE_BINARY_DIR}/vosk/model")
  set(_stamp "${CMAKE_BINARY_DIR}/vosk/model.stamp")
  if(NOT EXISTS "${_zip}")
    message(FATAL_ERROR "vendored vosk model missing: ${_zip}")
  endif()
  add_custom_command(
    OUTPUT "${_stamp}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${_out}" ${CMAKE_COMMAND} -E tar xf "${_zip}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
    DEPENDS "${_zip}"
    COMMENT "Extracting vendored Vosk model (39 MB, once)"
    VERBATIM)
  add_custom_target(pptv_vosk_model DEPENDS "${_stamp}")
  set(${OUT_DIR} "${_out}/vosk-model-small-en-us-0.15" PARENT_SCOPE)
endfunction()
