# raylib-libretro binary
add_executable(raylib-libretro
    raylib-libretro.c
)
target_link_libraries(raylib-libretro PUBLIC
    raylib-libretro-static
)
target_include_directories(raylib-libretro PUBLIC
    ../vendor/raygui/src
    ../vendor/raylib-app
)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources)
    add_custom_command(TARGET raylib-libretro POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:raylib-libretro>/resources
    )
endif()

install(TARGETS raylib-libretro DESTINATION bin)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources)
    install(DIRECTORY resources DESTINATION bin)
endif()
install(FILES ../README.md DESTINATION share/raylib-libretro)
install(FILES ../LICENSE DESTINATION share/raylib-libretro)
