cmake_minimum_required(VERSION 3.5)
project(drogon_postgres_example CXX)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0074 NEW)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(TSAN_FLAGS "-fsanitize=thread -g -O1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TSAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TSAN_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
else()
    message(WARNING "Thread Sanitizer is only supported with GCC and Clang compilers")
endif()


# Instead of building PostgreSQL from source, use the system's PostgreSQL
find_package(PostgreSQL REQUIRED)

include(FetchContent)

# Download and configure Drogon
FetchContent_Declare(
    drogon
    GIT_REPOSITORY https://github.com/drogonframework/drogon.git
    GIT_TAG master
)

set(BUILD_POSTGRESQL ON CACHE BOOL "Build PostgreSQL integration" FORCE)

set(BUILD_EXAMPLES OFF)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)
set(BUILD_MYSQL OFF)
set(BUILD_SQLITE OFF)
set(BUILD_REDIS OFF)
set(USE_MYSQL OFF)
set(USE_SQLITE3 OFF)
set(BUILD_TESTING OFF)
set(BUILD_CTL OFF)
set(COZ_PROFILING OFF)
set(PostgreSQL_TYPE_INCLUDE_DIR ${PostgreSQL_INCLUDE_DIR} CACHE PATH "PostgreSQL type include directory")
set(LIBPQ_BATCH_MODE OFF)

FetchContent_MakeAvailable(drogon)

add_executable(drogon_postgres_example main.cpp)
target_link_libraries(drogon_postgres_example PRIVATE drogon ${PostgreSQL_LIBRARIES})
target_include_directories(drogon_postgres_example PRIVATE ${PostgreSQL_INCLUDE_DIRS})
