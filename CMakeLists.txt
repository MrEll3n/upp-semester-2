CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

PROJECT(UPP_SP2)

# pouzivame standard C++23 - pokud jej vas kompilator nepodporuje, zmente nejhure na 17 (C++17)
SET(CMAKE_CXX_STANDARD 23)

# najdeme vsechny soubory v podadresari src
FILE(GLOB_RECURSE src src/*.cpp src/*.h)

# nastavime, ze nechceme pouzivat SSL ve vychozim stavu - pokud chcete, muzete zapnout, abyste se dostali na https
SET(USE_SSL OFF CACHE BOOL "Use SSL")

IF(USE_SSL)
	ADD_DEFINITIONS(-DUSE_SSL)
	# najdeme OpenSSL
	FIND_PACKAGE(OpenSSL REQUIRED)

	INCLUDE_DIRECTORIES("${OPENSSL_INCLUDE_DIR}")
ENDIF()

FIND_PACKAGE(MPI REQUIRED)

INCLUDE_DIRECTORIES("dep/cpp-httplib" ${MPI_CXX_INCLUDE_DIRS})

ADD_EXECUTABLE(UPP-SP2 ${src})

TARGET_LINK_LIBRARIES(UPP-SP2 ${MPI_CXX_LIBRARIES})

# pokud chceme pouzivat SSL, musime prilinkovat OpenSSL
IF(USE_SSL)
	TARGET_LINK_LIBRARIES(UPP-SP2 OpenSSL::SSL OpenSSL::Crypto)
ENDIF()
