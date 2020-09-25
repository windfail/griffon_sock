# Set the minimum version of CMake that can be used
# To find the cmake version run
# $ cmake --version
cmake_minimum_required(VERSION 3.5)

# Set the project name
project (netsock)
SET(CMAKE_BUILD_TYPE "Release")
# Create a sources variable with a link to all cpp files to compile
set(SOURCES
    src/ssl_relay.cpp
    src/raw_relay.cpp
    src/relay_server.cpp
    src/server.cpp
    src/main.cpp
)
set(CMAKE_CXX_STANDARD 14)

# Add an executable with the above sources
add_executable(${PROJECT_NAME} ${SOURCES})
#target_link_options(${PROJECT_NAME}
#  PRIVATE "-lboost_system"
#  PRIVATE "-pthread")

# find a boost install with the libraries filesystem and system
find_package(Boost 1.71 REQUIRED COMPONENTS system log)

add_definitions(-DBOOST_LOG_DYN_LINK)
# check if boost was found
if(Boost_FOUND)
    message ("boost found")
else()
    message (FATAL_ERROR "Cannot find Boost")
endif()

target_link_libraries( ${PROJECT_NAME}
    PRIVATE
	Boost::log
	Boost::system
	pthread
	ssl
	crypto
)

# Set the directories that should be included in the build command for this target
# when running g++ these will be included as -I/directory/path/
target_include_directories(${PROJECT_NAME}
    PRIVATE
	${PROJECT_SOURCE_DIR}/include
)
