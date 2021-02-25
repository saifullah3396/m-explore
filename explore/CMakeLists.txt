cmake_minimum_required(VERSION 3.1)
project(explore_lite)

## Find catkin macros and libraries
find_package(catkin REQUIRED COMPONENTS
  actionlib
  actionlib_msgs
  costmap_2d
  geometry_msgs
  map_msgs
  move_base_msgs
  nav_msgs
  roscpp
  std_msgs
  tf
  visualization_msgs
)

# OpenCV is required for merging without initial positions
find_package(OpenCV REQUIRED)
if("${OpenCV_VERSION}" VERSION_LESS "3.0")
  message(FATAL_ERROR "This package needs OpenCV >= 3.0")
endif()
if("${OpenCV_VERSION}" VERSION_LESS "4.0")
  message(WARNING "This package supports OpenCV 3, but some features may not be "
    "available. Upgrade to OpenCV 4 to take advantage of all features.")
endif()

###################################
## catkin specific configuration ##
###################################
catkin_package(
  CATKIN_DEPENDS
    actionlib
    actionlib_msgs
    costmap_2d
    geometry_msgs
    map_msgs
    move_base_msgs
    nav_msgs
    roscpp
    std_msgs
    tf
    visualization_msgs
  DEPENDS
    OpenCV
)

###########
## Build ##
###########
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

## Specify additional locations of header files
include_directories(
  ${catkin_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
  include
)

add_executable(explore
  src/costmap_client.cpp
  src/explore.cpp
  src/frontier_search.cpp
)
add_dependencies(explore ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})
target_link_libraries(explore ${catkin_LIBRARIES} ${OpenCV_LIBRARIES})

#############
## Install ##
#############

# install nodes
install(TARGETS explore
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

# install roslaunch files
install(DIRECTORY launch/
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/launch
)

#############
## Testing ##
#############
if(CATKIN_ENABLE_TESTING)
  find_package(roslaunch REQUIRED)

  # test all launch files
  roslaunch_add_file_check(launch)
endif()
