TEMPLATE        = app
LANGUAGE        = C++
SOURCES         = qmc_desc.cpp
CONFIG          += qt warn_on
INCLUDEPATH     += ../../../src/include
INCLUDEPATH     += ../../../src/libpcp_qmc/src
release:DESTDIR	= build/debug
debug:DESTDIR	= build/release
LIBS            += -L../../../src/libpcp/src
LIBS            += -L../../../src/libpcp_qmc/src
LIBS            += -L../../../src/libpcp_qmc/src/$$DESTDIR
LIBS            += -lpcp_qmc -lpcp
QT		-= gui
