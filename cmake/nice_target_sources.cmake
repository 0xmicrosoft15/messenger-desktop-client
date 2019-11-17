function(nice_target_sources target_name src_loc list)
    set(writing_now "")
    set(private_sources "")
    set(public_sources "")
    set(interface_sources "")
    set(not_win_sources "")
    set(not_mac_sources "")
    set(not_linux_sources "")
    foreach(file ${list})
        if (${file} STREQUAL "PRIVATE" OR ${file} STREQUAL "PUBLIC" OR ${file} STREQUAL "INTERFACE")
            set(writing_now ${file})
        else()
            set(full_name ${src_loc}/${file})
            if (${file} MATCHES "(^|/)win/" OR ${file} MATCHES "(^|/)winrc/" OR ${file} MATCHES "(^|/)windows/" OR ${file} MATCHES "[_\\/]win\\.")
                list(APPEND not_mac_sources ${full_name})
                list(APPEND not_linux_sources ${full_name})
            elseif (${file} MATCHES "(^|/)mac/" OR ${file} MATCHES "(^|/)darwin/" OR ${file} MATCHES "(^|/)osx/" OR ${file} MATCHES "[_\\/]mac\\." OR ${file} MATCHES "[_\\/]darwin\\." OR ${file} MATCHES "[_\\/]osx\\.")
                list(APPEND not_win_sources ${full_name})
                list(APPEND not_linux_sources ${full_name})
            elseif (${file} MATCHES "(^|/)linux/" OR ${file} MATCHES "[_\\/]linux\\.")
                list(APPEND not_win_sources ${full_name})
                list(APPEND not_mac_sources ${full_name})
            elseif (${file} MATCHES "(^|/)posix/" OR ${file} MATCHES "[_\\/]posix\\.")
                list(APPEND not_win_sources ${full_name})
            endif()
            if ("${writing_now}" STREQUAL "PRIVATE")
                list(APPEND private_sources ${full_name})
            elseif ("${writing_now}" STREQUAL "PUBLIC")
                list(APPEND public_sources ${full_name})
            elseif ("${writing_now}" STREQUAL "INTERFACE")
                list(APPEND interface_sources ${full_name})
            else()
                message(FATAL_ERROR "Unknown sources scope for target ${target_name}")
            endif()
            source_group(TREE ${src_loc} PREFIX Sources FILES ${full_name})
        endif()
    endforeach()

    if (NOT "${public_sources}" STREQUAL "")
        target_sources(${target_name} PUBLIC ${public_sources})
    endif()
    if (NOT "${private_sources}" STREQUAL "")
        target_sources(${target_name} PRIVATE ${private_sources})
    endif()
    if (NOT "${interface_sources}" STREQUAL "")
        target_sources(${target_name} INTERFACE ${interface_sources})
    endif()
    if (WIN32)
        set_source_files_properties(${not_win_sources} PROPERTIES HEADER_FILE_ONLY TRUE)
        set_source_files_properties(${not_win_sources} PROPERTIES SKIP_AUTOGEN TRUE)
    elseif (APPLE)
        set_source_files_properties(${not_mac_sources} PROPERTIES HEADER_FILE_ONLY TRUE)
        set_source_files_properties(${not_mac_sources} PROPERTIES SKIP_AUTOGEN TRUE)
    elseif (LINUX)
        set_source_files_properties(${not_linux_sources} PROPERTIES HEADER_FILE_ONLY TRUE)
        set_source_files_properties(${not_linux_sources} PROPERTIES SKIP_AUTOGEN TRUE)
    endif()
endfunction()
