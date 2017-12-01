#######################################
# The list of executables we are building seperated by spaces
# the 'bin_' indicates that these build products will be installed
# in the $(bindir) directory. For example /usr/bin
#bin_PROGRAMS=test_stomp

# Because a.out is only a sample program we don't want it to be installed.
# The 'noinst_' prefix indicates that the following targets are not to be
# installed.
noinst_PROGRAMS=test_stomp

#######################################
# Build information for each executable. The variable name is derived
# by use the name of the executable with each non alpha-numeric character is
# replaced by '_'. So a.out becomes a_out and the appropriate suffex added.
# '_SOURCES' for example.

ACLOCAL_AMFLAGS=-I ../m4

# Sources for the a.out 
test_stomp_SOURCES= test_stomp.c

# Libraries for a.out
test_stomp_LDADD = $(top_srcdir)/libstomp/libstomp.la

# Linker options for a.out
test_stomp_LDFLAGS = -rpath `cd $(top_srcdir);pwd`/libstomp/.libs

# Compiler options for a.out
test_stomp_CPPFLAGS = -I$(top_srcdir)/include
