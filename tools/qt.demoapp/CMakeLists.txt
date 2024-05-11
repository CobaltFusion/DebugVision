
add_executable(qt.demoapp
	AsyncFileScanner.cpp
	AsyncFileScanner.h
	main.cpp
	MainWindow.cpp
	MainWindow.h
	WidgetFileTextViewer.cpp
	WidgetFileTextViewer.h
	WidgetFileTreeView.cpp
	WidgetFileTreeView.h
	# special files
	pch.h
	images/images.qrc
)
target_precompile_headers(qt.demoapp PRIVATE pch.h)
target_link_libraries(qt.demoapp PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent)
set_target_properties(qt.demoapp PROPERTIES AUTOMOC ON AUTORCC ON)

if (WIN32)
	# display the Win32 console only for debug builds
	set_target_properties(qt.demoapp PROPERTIES WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>)
endif()
