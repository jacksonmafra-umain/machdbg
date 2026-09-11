set(widgets_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../gui/Src")

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../zydis_wrapper
    ${CMAKE_CURRENT_BINARY_DIR}/zydis_wrapper
)

add_subdirectory(
    ${widgets_SOURCE_DIR}/ThirdPartyLibs/md4c
    ${CMAKE_CURRENT_BINARY_DIR}/md4c
)

include(Qt.cmake)

# Capstone comes from Homebrew (milestone 6, decision 1), so it is found rather than built.
#
# The include directory is NOT the one pkg-config reports. `pkg-config --cflags capstone`
# answers `<prefix>/include/capstone`, under which `#include <capstone/capstone.h>` -- the
# include every Capstone example uses -- does not resolve. The header lives at
# `<prefix>/include/capstone/capstone.h`, so the root to add is `<prefix>/include`. Measured by
# trying it: a build that passes the reported flags through fails at the first header.
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAPSTONE REQUIRED IMPORTED_TARGET capstone)

set(CAPSTONE_INCLUDE_ROOTS "")
foreach(capstone_include ${CAPSTONE_INCLUDE_DIRS})
    get_filename_component(capstone_include_name "${capstone_include}" NAME)
    if(capstone_include_name STREQUAL "capstone")
        get_filename_component(capstone_include_parent "${capstone_include}" DIRECTORY)
        list(APPEND CAPSTONE_INCLUDE_ROOTS "${capstone_include_parent}")
    endif()
    list(APPEND CAPSTONE_INCLUDE_ROOTS "${capstone_include}")
endforeach()
list(REMOVE_DUPLICATES CAPSTONE_INCLUDE_ROOTS)

set(hook_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Bridge.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Configuration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Configuration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CrossAccessible.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CrossAccessible.h
    ${CMAKE_CURRENT_SOURCE_DIR}/MagicMenu.h
    ${CMAKE_CURRENT_SOURCE_DIR}/MiscUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MiscUtil.h
    ${CMAKE_CURRENT_SOURCE_DIR}/StringUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StringUtil.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Imports.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RegisterContext.h
    ${CMAKE_CURRENT_SOURCE_DIR}/QDarkApplication.h
    ${CMAKE_CURRENT_SOURCE_DIR}/QDarkApplication.cpp
)

set(widgets_SOURCES
    ${widgets_SOURCE_DIR}/Accessible/AccessibleAbstractTableView.cpp
    ${widgets_SOURCE_DIR}/Accessible/AccessibleAbstractTableView.h
    ${widgets_SOURCE_DIR}/Accessible/AccessibleAbstractTableViewCell.cpp
    ${widgets_SOURCE_DIR}/Accessible/AccessibleAbstractTableViewCell.h
    ${widgets_SOURCE_DIR}/Accessible/AccessibleAbstractTableViewCellTitle.cpp
    ${widgets_SOURCE_DIR}/Accessible/AccessibleAbstractTableViewCellTitle.h
    ${widgets_SOURCE_DIR}/Accessible/AccessibleDisassembly.cpp
    ${widgets_SOURCE_DIR}/Accessible/AccessibleDisassembly.h
    ${widgets_SOURCE_DIR}/Accessible/AccessibleHexDump.cpp
    ${widgets_SOURCE_DIR}/Accessible/AccessibleHexDump.h
    ${widgets_SOURCE_DIR}/Accessible/AccessibleStdTable.cpp
    ${widgets_SOURCE_DIR}/Accessible/AccessibleStdTable.h
    ${widgets_SOURCE_DIR}/BasicView/AbstractStdTable.cpp
    ${widgets_SOURCE_DIR}/BasicView/AbstractStdTable.h
    ${widgets_SOURCE_DIR}/BasicView/AbstractTableView.cpp
    ${widgets_SOURCE_DIR}/BasicView/AbstractTableView.h
    ${widgets_SOURCE_DIR}/BasicView/Disassembly.cpp
    ${widgets_SOURCE_DIR}/BasicView/Disassembly.h
    ${widgets_SOURCE_DIR}/BasicView/HexDump.cpp
    ${widgets_SOURCE_DIR}/BasicView/HexDump.h
    ${widgets_SOURCE_DIR}/BasicView/HistoryLineEdit.cpp
    ${widgets_SOURCE_DIR}/BasicView/HistoryLineEdit.h
    ${widgets_SOURCE_DIR}/BasicView/StdTable.cpp
    ${widgets_SOURCE_DIR}/BasicView/StdTable.h
    ${widgets_SOURCE_DIR}/Disassembler/Architecture.cpp
    ${widgets_SOURCE_DIR}/Disassembler/Architecture.h
    ${widgets_SOURCE_DIR}/Disassembler/QZydis.cpp
    ${widgets_SOURCE_DIR}/Disassembler/QZydis.h
    ${widgets_SOURCE_DIR}/Disassembler/ZydisTokenizer.cpp
    ${widgets_SOURCE_DIR}/Disassembler/ZydisTokenizer.h
    ${widgets_SOURCE_DIR}/Gui/BrowseDialog.cpp
    ${widgets_SOURCE_DIR}/Gui/BrowseDialog.h
    ${widgets_SOURCE_DIR}/Gui/BrowseDialog.ui
    ${widgets_SOURCE_DIR}/Gui/ColumnReorderDialog.cpp
    ${widgets_SOURCE_DIR}/Gui/ColumnReorderDialog.h
    ${widgets_SOURCE_DIR}/Gui/ColumnReorderDialog.ui
    ${widgets_SOURCE_DIR}/Gui/ComboBoxDialog.cpp
    ${widgets_SOURCE_DIR}/Gui/ComboBoxDialog.h
    ${widgets_SOURCE_DIR}/Gui/ComboBoxDialog.ui
    ${widgets_SOURCE_DIR}/Gui/DisassemblyPopup.cpp
    ${widgets_SOURCE_DIR}/Gui/DisassemblyPopup.h
    ${widgets_SOURCE_DIR}/Gui/LineEditDialog.cpp
    ${widgets_SOURCE_DIR}/Gui/LineEditDialog.h
    ${widgets_SOURCE_DIR}/Gui/LineEditDialog.ui
    ${widgets_SOURCE_DIR}/Gui/TypeWidget.cpp
    ${widgets_SOURCE_DIR}/Gui/TypeWidget.h
    ${widgets_SOURCE_DIR}/Gui/WordEditDialog.cpp
    ${widgets_SOURCE_DIR}/Gui/WordEditDialog.h
    ${widgets_SOURCE_DIR}/Gui/WordEditDialog.ui
    ${widgets_SOURCE_DIR}/Gui/RichTextItemDelegate.cpp
    ${widgets_SOURCE_DIR}/Gui/RichTextItemDelegate.h
    ${widgets_SOURCE_DIR}/Gui/ReleaseNotesDialog.cpp
    ${widgets_SOURCE_DIR}/Gui/ReleaseNotesDialog.h
    ${widgets_SOURCE_DIR}/Gui/ReleaseNotesDialog.ui
    ${widgets_SOURCE_DIR}/Gui/ImageTextBrowser.cpp
    ${widgets_SOURCE_DIR}/Gui/ImageTextBrowser.h
    ${widgets_SOURCE_DIR}/Memory/MemoryPage.cpp
    ${widgets_SOURCE_DIR}/Memory/MemoryPage.h
    ${widgets_SOURCE_DIR}/Utils/ActionHelpers.h
    ${widgets_SOURCE_DIR}/Utils/CachedFontMetrics.h
    ${widgets_SOURCE_DIR}/Utils/CodeFolding.cpp
    ${widgets_SOURCE_DIR}/Utils/CodeFolding.h
    ${widgets_SOURCE_DIR}/Utils/EncodeMap.cpp
    ${widgets_SOURCE_DIR}/Utils/EncodeMap.h
    ${widgets_SOURCE_DIR}/Utils/MenuBuilder.cpp
    ${widgets_SOURCE_DIR}/Utils/MenuBuilder.h
    ${widgets_SOURCE_DIR}/Utils/MethodInvoker.h
    ${widgets_SOURCE_DIR}/Utils/HexValidator.cpp
    ${widgets_SOURCE_DIR}/Utils/HexValidator.h
    ${widgets_SOURCE_DIR}/Utils/RichTextPainter.cpp
    ${widgets_SOURCE_DIR}/Utils/RichTextPainter.h
    ${widgets_SOURCE_DIR}/Utils/ValidateExpressionThread.cpp
    ${widgets_SOURCE_DIR}/Utils/ValidateExpressionThread.h
    ${widgets_SOURCE_DIR}/Utils/VaHistory.h
    ${widgets_SOURCE_DIR}/Gui/RegistersView.cpp
    ${widgets_SOURCE_DIR}/Gui/RegistersView.h
    ${widgets_SOURCE_DIR}/Accessible/AccessibleRegistersView.cpp
    ${widgets_SOURCE_DIR}/Accessible/AccessibleRegistersView.h
)

include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(x64dbg_widgets STATIC
    ${hook_SOURCES}
    ${widgets_SOURCES}
    resources.qrc
)
add_library(x64dbg::widgets ALIAS x64dbg_widgets)

target_include_directories(x64dbg_widgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${widgets_SOURCE_DIR}
    ${widgets_SOURCE_DIR}/Utils
)

target_include_directories(x64dbg_widgets PRIVATE
    ${widgets_SOURCE_DIR}/BasicView
)

source_group(TREE ${widgets_SOURCE_DIR} FILES ${widgets_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX hook FILES ${hook_SOURCES})

target_compile_definitions(x64dbg_widgets PRIVATE
    QT_NO_DEPRECATED_WARNINGS
)

target_link_libraries(x64dbg_widgets PUBLIC
    ${QT_LIBRARIES}
    zydis_wrapper
    md4c-html
    PkgConfig::CAPSTONE
)

target_include_directories(x64dbg_widgets PUBLIC ${CAPSTONE_INCLUDE_ROOTS})

# https://doc.qt.io/qt-6/wasm.html#asyncify
if(EMSCRIPTEN)
    #target_link_options(${CMKR_TARGET} PUBLIC -sASYNCIFY -Os)
endif()
