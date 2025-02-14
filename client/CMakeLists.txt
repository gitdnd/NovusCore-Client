project(client-demo VERSION 1.0.0 DESCRIPTION "Wow demo client for NovusCore")

file(GLOB_RECURSE CLIENT_DEMO_FILES "*.cpp" "*.h")

add_executable(${PROJECT_NAME} ${CLIENT_DEMO_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER ${ROOT_FOLDER})

#set the visual studio working directory to the parent path of the client executable
set_target_properties(
    ${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../")

find_assign_files(${CLIENT_DEMO_FILES})

include_directories(../../dep/glfw/include)

add_compile_definitions(NOMINMAX _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(${PROJECT_NAME} PRIVATE
	common::common
	gameplay::gameplay
	render::render
	network::network
	input::input
	scenemanager::scenemanager
	glfw ${GLFW_LIBRARIES}
	Entt::Entt
	taskflow::taskflow
	scripting::scripting
	imgui::imgui
)

add_custom_command(
    TARGET ${PROJECT_NAME} 
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${DXCOMPILER_DYNAMIC_LIB_PATH}"
        "$<TARGET_FILE_DIR:client-demo>/dxcompiler.dll"
)

target_precompile_headers(${PROJECT_NAME} PRIVATE "pch.h")
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})