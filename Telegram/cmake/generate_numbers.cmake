function(generate_numbers target_name numbers_file)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    set(generated_files
        ${gen_dst}/numbers.cpp
        ${gen_dst}/numbers.h
    )
    add_custom_command(
    OUTPUT
        ${generated_files}
    COMMAND
        codegen_numbers
        -o${gen_dst}
        ${numbers_file}
    COMMENT "Generating numbers (${target_name})"
    DEPENDS
        codegen_numbers
        ${gen_src}
    )
    generate_target(${target_name} numbers "${generated_files}" ${gen_dst})
endfunction()
