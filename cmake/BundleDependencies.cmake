cmake_minimum_required(VERSION 3.23)

function(bundle_dependencies target bundle_target)
  function(strip_link_only lib lib_stripped_name)
    set(link_only_prefix "$<LINK_ONLY")
    string(LENGTH ${link_only_prefix} link_only_prefix_len)
    string(SUBSTRING ${lib} 0 ${link_only_prefix_len} lib_link_only_prefix)
    if(lib_link_only_prefix STREQUAL link_only_prefix)
      string(REGEX REPLACE "\\$<LINK_ONLY:([A-Za-z0-9:_-]+)>" "\\1"
                           lib_stripped ${lib})
    else()
      set(lib_stripped "")
    endif()
    set(${lib_stripped_name}
        ${lib_stripped}
        PARENT_SCOPE)
  endfunction()

  get_target_property(libs ${target} INTERFACE_LINK_LIBRARIES)
  string(GENEX_STRIP "${libs}" iface_libs)
  foreach(lib IN LISTS libs)
    strip_link_only(${lib} lib_stripped)
    if(lib_stripped)
      list(APPEND link_libs ${lib_stripped})
    endif()
  endforeach()
  set(bundle_libs "${target}")

  while(link_libs)
    list(POP_BACK link_libs lib)
    strip_link_only(${lib} lib_stripped)
    if(lib_stripped)
      set(lib ${lib_stripped})
    endif()
    if(NOT TARGET ${lib})
      continue()
    endif()

    get_target_property(alias ${lib} ALIASED_TARGET)
    if(TARGET ${alias})
      set(lib ${alias})
    endif()

    get_target_property(lib_type ${lib} TYPE)
    set(is_static_lib FALSE)
    set(is_iface_lib FALSE)
    if(${lib_type} STREQUAL STATIC_LIBRARY)
      set(is_static_lib TRUE)
    elseif(${lib_type} STREQUAL INTERFACE_LIBRARY)
      set(is_iface_lib TRUE)
    endif()

    if(is_static_lib OR is_iface_lib)
      if(is_static_lib)
        list(APPEND bundle_libs ${lib})
      endif()
      get_target_property(lib_libs ${lib} INTERFACE_LINK_LIBRARIES)
      if(lib_libs)
        foreach(lib ${lib_libs})
          list(APPEND link_libs ${lib})
        endforeach()
      endif()
    else()
      list(APPEND iface_libs $<LINK_ONLY:${lib}>)
    endif()
  endwhile()

  set(bundle_file_name
      ${CMAKE_STATIC_LIBRARY_PREFIX}${target}${CMAKE_STATIC_LIBRARY_SUFFIX})
  set(bundle_file ${CMAKE_CURRENT_BINARY_DIR}/bundle/${bundle_file_name})
  set(mir_file ${bundle_file}.ar)
  set(mir_config_file ${mir_file}.in)

  file(WRITE ${mir_config_file} "CREATE ${bundle_file}\n")
  foreach(lib ${bundle_libs})
    file(APPEND ${mir_config_file} "ADDLIB $<TARGET_FILE:${lib}>\n")
  endforeach()
  file(APPEND ${mir_config_file} "SAVE\n")
  file(APPEND ${mir_config_file} "END\n")
  file(
    GENERATE
    OUTPUT ${mir_file}
    INPUT ${mir_config_file})

  add_custom_command(
    OUTPUT ${bundle_file}
    DEPENDS ${bundle_libs} ${mir_file}
    COMMAND ${CMAKE_AR} -M < ${mir_file}
    COMMAND_EXPAND_LISTS)
  add_custom_target(${bundle_target}-bundle ALL DEPENDS ${bundle_file})
  add_library(${bundle_target} INTERFACE)
  add_dependencies(${bundle_target} INTERFACE ${bundle_target}-bundle)

  get_target_property(include_dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
  target_include_directories(${bundle_target} INTERFACE ${include_dirs})
  target_link_libraries(
    ${bundle_target}
    INTERFACE
      $<BUILD_INTERFACE:${bundle_file}>
      $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${bundle_file_name}>
      ${iface_libs})
  set_target_properties(${bundle_target} PROPERTIES ARCHIVE_OUTPUT_NAME
                                                    ${bundle_file})

  get_target_property(header_sets ${target} INTERFACE_HEADER_SETS)
  foreach(header_set ${header_sets})
    target_sources(
      ${bundle_target}
      INTERFACE FILE_SET ${header_set} BASE_DIRS
                $<TARGET_PROPERTY:${target},HEADER_DIRS_${header_set}> FILES
                $<TARGET_PROPERTY:${target},HEADER_SET_${header_set}>)
  endforeach()
endfunction()
