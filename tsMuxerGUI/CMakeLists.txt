cmake_minimum_required (VERSION 3.12)
project(tsMuxerGUI CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6
  REQUIRED COMPONENTS Widgets LinguistTools
  OPTIONAL_COMPONENTS Multimedia)

# When cross-compiling for macOS, the pre-built Qt cmake config may reference
# the deprecated AGL framework which is absent from modern/osxcross SDKs.
# The reference lives in Qt's internal WrapOpenGL target (created by
# Qt6GuiDependencies.cmake) and propagates transitively through Qt6::Gui.
if(APPLE AND CMAKE_CROSSCOMPILING)
  # The pre-built Qt cmake config references the deprecated AGL framework
  # which is absent from modern/osxcross SDKs.  Strip it from Qt's internal
  # WrapOpenGL target (created by Qt6GuiDependencies.cmake).
  foreach(_qt_tgt WrapOpenGL::WrapOpenGL Qt6::Gui Qt6::Widgets)
    if(TARGET ${_qt_tgt})
      get_target_property(_libs ${_qt_tgt} INTERFACE_LINK_LIBRARIES)
      if(_libs)
        list(FILTER _libs EXCLUDE REGEX "AGL")
        set_target_properties(${_qt_tgt} PROPERTIES INTERFACE_LINK_LIBRARIES "${_libs}")
      endif()
    endif()
  endforeach()

  # The host rcc may have zstd support that the target Qt lacks, causing
  # unresolved _qt_resourceFeatureZstd symbols.  Disable zstd in rcc.
  set(CMAKE_AUTORCC_OPTIONS "--no-zstd")
endif()

set(tsmuxer_gui_libs Qt6::Widgets)

# Linux static builds in our Docker environment can have Qt6Multimedia
# available without its runtime backend deps (e.g. PulseAudio), which causes
# link failures. Keep multimedia optional and skip it for that build flavor.
if(Qt6Multimedia_FOUND)
  if(TSMUXER_UNIFIED AND WIN32)
    # the single executable plays its sounds with PlaySound, which avoids
    # pulling Qt Multimedia (and its FFmpeg/TLS dependencies) into it
    message(STATUS "Qt6Multimedia found but not used by the single executable")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TSMUXER_STATIC_BUILD)
    message(STATUS "Qt6Multimedia found but disabled for static Linux build")
  else()
    list(APPEND tsmuxer_gui_libs Qt6::Multimedia)
    set(TSMUXER_HAVE_MULTIMEDIA TRUE)
  endif()
endif()

set(tsmuxer_ts_files
  translations/tsmuxergui_en.ts
  translations/tsmuxergui_ru.ts
  translations/tsmuxergui_fr.ts
  translations/tsmuxergui_zh.ts
  translations/tsmuxergui_de.ts
  translations/tsmuxergui_he.ts
  translations/tsmuxergui_es.ts
  translations/tsmuxergui_ja.ts
)

set(lang_qrc "translations.qrc")
configure_file(${lang_qrc} ${lang_qrc} COPYONLY)

qt6_add_translation(QM_FILES ${tsmuxer_ts_files})

# Make the embedded-resource (rcc) step depend on the compiled .qm, so a build always embeds the
# CURRENT translations. Without this, autorcc can embed a stale .qm and strings added since the last
# .qm build ship untranslated (this caused strings added from 2.12.0 onward to appear in English).
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${lang_qrc}
                            PROPERTIES OBJECT_DEPENDS "${QM_FILES}")

set(tsmuxer_gui_sources
  main.cpp
  tsmuxerwindow.cpp
  muxForm.cpp
  lang_codes.cpp
  tsmuxerwindow.ui
  muxForm.ui
  checkboxedheaderview.cpp
  images.qrc
  fontsettingstablemodel.cpp
  systemcolours.cpp
  muxQueue.cpp
  discExplorer.cpp
  engineLog.cpp
  ${QM_FILES}
  ${CMAKE_CURRENT_BINARY_DIR}/${lang_qrc}
)

# The audio delay preview plays the film in mpv and drives it over its IPC pipe (QLocalSocket, in Qt Network).
# Windows only: the Docker image has no player, and a picture sent to a web browser cannot be judged for lip sync.
if(WIN32)
  find_package(Qt6 QUIET COMPONENTS Network)
  if(Qt6Network_FOUND)
    list(APPEND tsmuxer_gui_sources mpvPreview.cpp)
    list(APPEND tsmuxer_gui_libs Qt6::Network)
    set(TSMUXER_MPV_PREVIEW TRUE)
  endif()
endif()

if (TSMUXER_UNIFIED)
  # one executable: the GUI, which also runs the muxer engine on the command line
  set(tsmuxer_gui_target tsMuxeR)
  list(APPEND tsmuxer_gui_sources unifiedLauncher.cpp)
  list(APPEND tsmuxer_gui_libs tsmuxer)
else()
  set(tsmuxer_gui_target tsMuxerGUI)
endif()

if (TSMUXER_UNIFIED AND WIN32)
  add_executable(tsMuxeR WIN32 ${tsmuxer_gui_sources} icon.rc)
  if(TSMUXER_STATIC_BUILD AND NOT MSVC)
    target_link_options(tsMuxeR PRIVATE -static -static-libgcc -static-libstdc++)
    # MSYS2's harfbuzz CMake package always points at its DLL; use the static
    # archive and its pkg-config dependency chain (glib, graphite2...) instead
    if(TARGET harfbuzz::harfbuzz)
      find_library(HARFBUZZ_STATIC_LIB libharfbuzz.a REQUIRED)
      set_target_properties(harfbuzz::harfbuzz PROPERTIES
        IMPORTED_LOCATION "${HARFBUZZ_STATIC_LIB}" IMPORTED_IMPLIB "")
      find_package(PkgConfig REQUIRED)
      pkg_check_modules(HARFBUZZ_PC REQUIRED harfbuzz)
      list(TRANSFORM HARFBUZZ_PC_STATIC_LIBRARIES PREPEND "-l")
      list(JOIN HARFBUZZ_PC_STATIC_LIBRARIES " " harfbuzz_static_deps)
    endif()
    # transitive dependencies of the static OpenSSL, FreeType and HarfBuzz used
    # by Qt, which their imported targets do not declare; they must come last
    # on the link line, after libcrypto.a and libfreetype.a
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES
      " ${harfbuzz_static_deps} -liconv -lws2_32 -lcrypt32 -lbz2 -lbrotlidec -lbrotlicommon")
  endif()
  if(COMMAND qt_import_plugins)
    # PNG/ICO are built into QtGui; the extra image format and TLS plugins only
    # add dependencies (libtiff, libwebp, OpenSSL...) that tsMuxeR never uses
    qt_import_plugins(tsMuxeR EXCLUDE_BY_TYPE imageformats tls networkinformation iconengines)
  endif()
elseif (TSMUXER_UNIFIED AND UNIX AND NOT APPLE)
  # Linux single executable, used by the Docker image
  add_executable(tsMuxeR ${tsmuxer_gui_sources})
  install(TARGETS tsMuxeR DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif (WIN32)
  add_executable(tsMuxerGUI WIN32 ${tsmuxer_gui_sources} icon.rc)
elseif (APPLE)
  set(MACOSX_BUNDLE_ICON_FILE tsMuxerGUI.icns)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.networkoptix.tsmuxergui")
  set(MACOSX_BUNDLE_BUNDLE_NAME "tsMuxerGUI")
  # Info.plist.in interpolates these three and nothing set them, so CMake substituted empty
  # strings and the shipped bundle carried no version at all: Finder's Get Info showed a blank
  # where every other application shows a number, while the window's own title bar had it.
  # Measured rather than assumed: CFBundleShortVersionString read out of a real built artefact
  # was the empty string.
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_NUMBER}")
  set(MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION_NUMBER}")
  set(MACOSX_BUNDLE_LONG_VERSION_STRING "${VERSION_NUMBER}")
  set_source_files_properties(tsMuxerGUI.icns PROPERTIES
         MACOSX_PACKAGE_LOCATION "Resources")
  add_executable(tsMuxerGUI MACOSX_BUNDLE ${tsmuxer_gui_sources} tsMuxerGUI.icns)
  set_target_properties(tsMuxerGUI PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  )
  # Copy the tsMuxeR CLI binary into the app bundle after building
  add_custom_command(TARGET tsMuxerGUI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:tsmuxer>"
      "$<TARGET_BUNDLE_CONTENT_DIR:tsMuxerGUI>/MacOS/tsMuxeR"
    COMMENT "Copying tsMuxeR CLI into app bundle"
  )
# UNIX must be last here, as it's going to be true for APPLE and CYGWIN as well,
# but we want those to be handled by the code above.
elseif (UNIX)
  add_executable(tsMuxerGUI ${tsmuxer_gui_sources})
  install(TARGETS tsMuxerGUI DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES tsMuxerGUI.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES images/icon.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps/
          RENAME tsMuxerGUI.png)
endif()

target_link_libraries(${tsmuxer_gui_target} ${tsmuxer_gui_libs})

# PlaySound, used for the end-of-mux sound on Windows where Qt Multimedia is not built.
# See myPlaySound in tsmuxerwindow.cpp.
if(WIN32)
  target_link_libraries(${tsmuxer_gui_target} winmm)
endif()

# "Play sound at end" is guarded by QT_MULTIMEDIA_LIB in the source. qmake defined that
# macro by itself for QT += multimedia; CMake does not, and linking Qt6::Multimedia sets
# nothing. So the test was silently false in every CMake build and the feature has always
# fallen through to QApplication::beep(), which on Windows is usually inaudible. It last
# worked in a qmake build. Defined here in step with the linking above, so the static Linux
# build that deliberately leaves Multimedia out still takes the beep path.
if(TSMUXER_HAVE_MULTIMEDIA)
  target_compile_definitions(${tsmuxer_gui_target} PRIVATE QT_MULTIMEDIA_LIB=1)
endif()

if(TSMUXER_MPV_PREVIEW)
  target_compile_definitions(${tsmuxer_gui_target} PRIVATE TSMUXER_MPV_PREVIEW=1)
endif()

# On macOS, ensure tsMuxeR CLI is built before the GUI (for the copy step)
if (APPLE)
  add_dependencies(tsMuxerGUI tsmuxer)
endif()
