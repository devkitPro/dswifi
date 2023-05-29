cmake_minimum_required(VERSION 3.13)

project(dswifi
	LANGUAGES C
)

include(GNUInstallDirs)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${NDS_ROOT}" CACHE PATH "" FORCE)
endif()

# Define static library target
add_library(dswifi9 STATIC)

# Add compiler flags
target_compile_options(dswifi9 PRIVATE
	-Wall -Werror
)

# Add include directories
target_include_directories(dswifi9 PRIVATE include)

target_sources(dswifi9 PRIVATE
	source/wfc.c
	source/dswifi9.c

	# sgIP sources
	source/sgIP/sgIP.c
	source/sgIP/sgIP_memblock.c
	source/sgIP/sgIP_Hub.c
	source/sgIP/sgIP_IP.c
	source/sgIP/sgIP_ARP.c
	source/sgIP/sgIP_ICMP.c
	source/sgIP/sgIP_TCP.c
	source/sgIP/sgIP_UDP.c
	source/sgIP/sgIP_DHCP.c
	source/sgIP/sgIP_DNS.c
	source/sgIP/sgIP_sockets.c
)

# Install the library
install(
	TARGETS dswifi9
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install headers
install(
	DIRECTORY ${PROJECT_SOURCE_DIR}/include/
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	FILES_MATCHING
		PATTERN "*.h"
)

# Install the license
install(
	FILES dswifi_license.txt
	DESTINATION ${CMAKE_INSTALL_PREFIX}
)
