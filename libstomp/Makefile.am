#######################################
# The list of libraries we are building seperated by spaces.
# The 'lib_' indicates that these build products will be installed
# in the $(libdir) directory. For example /usr/lib
lib_LTLIBRARIES = libstomp.la

#######################################
# Build information for each library

# Sources for libstomp
libstomp_la_SOURCES = libstomp.c stomp_adapter_libwebsockets.c

# Linker options libTestProgram
libstomp_la_LDFLAGS = -lwebsockets

# Compiler options. Here we are adding the include directory
# to be searched for headers included in the source code.
libstomp_la_CPPFLAGS = -I$(top_srcdir)/include

