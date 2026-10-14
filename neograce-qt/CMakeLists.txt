cmake_minimum_required(VERSION 3.19)
project(NeoGrace VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# На Apple Silicon собираем нативно под arm64 (можно переопределить -DCMAKE_OSX_ARCHITECTURES)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport)

set(CORE_SOURCES
  src/core/props.cpp src/core/model.cpp src/core/agrio.cpp src/core/gracetext.cpp
  src/core/axes.cpp src/core/expr.cpp src/core/render.cpp src/core/ops.cpp src/core/setmath.cpp src/core/export.cpp src/core/i18n.cpp
)
add_library(neogracecore STATIC ${CORE_SOURCES})
target_include_directories(neogracecore PUBLIC src/core)
target_link_libraries(neogracecore PUBLIC Qt6::Core Qt6::Gui Qt6::Svg Qt6::PrintSupport)

file(GLOB UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/ui/*.h)
set(APP_ICON ${CMAKE_SOURCE_DIR}/packaging/NeoGrace.icns)
set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

add_library(neograceui STATIC ${UI_SOURCES})
target_include_directories(neograceui PUBLIC src/ui)
target_link_libraries(neograceui PUBLIC neogracecore Qt6::Widgets Qt6::Svg Qt6::PrintSupport)

add_executable(NeoGrace MACOSX_BUNDLE WIN32 src/main.cpp resources/resources.qrc ${APP_ICON})
target_link_libraries(NeoGrace PRIVATE neograceui)
set_target_properties(NeoGrace PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/Info.plist.in
  MACOSX_BUNDLE_GUI_IDENTIFIER org.neograce.NeoGrace
  MACOSX_BUNDLE_BUNDLE_NAME NeoGrace
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  MACOSX_BUNDLE_ICON_FILE NeoGrace.icns
)

# Автотесты ядра
enable_testing()
add_executable(neograce_tests tests/test_core.cpp)
target_link_libraries(neograce_tests PRIVATE neogracecore)
target_compile_definitions(neograce_tests PRIVATE NG_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
add_test(NAME core COMMAND neograce_tests)

find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
  add_executable(neograce_gui_tests tests/test_gui.cpp)
  target_link_libraries(neograce_gui_tests PRIVATE neograceui Qt6::Test)
  target_compile_definitions(neograce_gui_tests PRIVATE NG_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
  add_test(NAME gui COMMAND neograce_gui_tests)
  set_tests_properties(gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS NeoGrace BUNDLE DESTINATION . RUNTIME DESTINATION bin)
