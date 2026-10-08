cmake_minimum_required(VERSION 3.24)
project(SketchCAD VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Ceres CONFIG REQUIRED)
add_library(sketchcad_core src/document.cpp src/sketch.cpp
            src/sketch_tool.cpp src/sketch_view.cpp src/constraint_solver.cpp
            src/constraint_diagnostics.cpp src/drag.cpp)
target_include_directories(sketchcad_core PUBLIC include)
target_link_libraries(sketchcad_core PRIVATE Ceres::ceres)
enable_testing()
find_package(GTest REQUIRED)
include(GoogleTest)
add_executable(sketch_test tests/sketch_test.cpp)
target_link_libraries(sketch_test PRIVATE sketchcad_core GTest::gtest_main)
gtest_discover_tests(sketch_test)
add_executable(document_test tests/document_test.cpp)
target_link_libraries(document_test PRIVATE sketchcad_core GTest::gtest_main)
gtest_discover_tests(document_test)
foreach(name sketch_view_test sketch_tool_test constraint_test dimension_test
  constraint_diagnostics_test
  constraint_solver_test drag_test)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE sketchcad_core GTest::gtest_main)
  gtest_discover_tests(${name})
endforeach()

option(SKETCHCAD_BUILD_APP "Build Qt Quick app" ON)
if(SKETCHCAD_BUILD_APP)
 find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 QuickTest Test)
 set(CMAKE_AUTOMOC ON)
 set(SKETCHCAD_QML app/Main.qml app/SketchViewport.qml app/DimensionEditor.qml
  app/Inspector.qml app/Theme.qml)
 file(GLOB SKETCHCAD_ICONS CONFIGURE_DEPENDS app/icons/*.svg)
 add_library(sketchcad_controller app/sketch_controller.cpp
  app/sketch_controller.h)
 target_include_directories(sketchcad_controller PUBLIC app)
 target_link_libraries(sketchcad_controller PUBLIC sketchcad_core Qt6::Core)
 add_executable(sketch_controller_test tests/sketch_controller_test.cpp)
 target_link_libraries(sketch_controller_test PRIVATE sketchcad_controller
  Qt6::Test)
 add_test(NAME sketch_controller_test COMMAND sketch_controller_test)
 qt_add_executable(sketchcad app/main.cpp)
 target_link_libraries(sketchcad PRIVATE sketchcad_controller Qt6::Quick
  Qt6::QuickControls2)
 qt_add_resources(sketchcad "qml" PREFIX "/" BASE app
  FILES ${SKETCHCAD_QML} ${SKETCHCAD_ICONS})
 add_executable(qml_test tests/qml_test.cpp)
 target_link_libraries(qml_test PRIVATE sketchcad_controller Qt6::Quick
  Qt6::QuickControls2 Qt6::QuickTest Qt6::Test)
 qt_add_resources(qml_test "test_qml" PREFIX "/" BASE app
  FILES ${SKETCHCAD_QML} ${SKETCHCAD_ICONS})
 add_test(NAME qml_test COMMAND qml_test)
 set_tests_properties(qml_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
 add_test(NAME qml_smoke COMMAND sketchcad --smoke)
 set_tests_properties(qml_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 15)
endif()
