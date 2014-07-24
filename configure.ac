#
#   BSD LICENSE
#
#   Copyright(c) 2014 Midokura SARL.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
AC_PREREQ(2.59)
AC_INIT([lwip-dpdk], [1.0], [takayuki@midokura.com], [lwip-dpdk])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR([tools])
AC_PROG_CC
AC_PROG_INSTALL
AC_ARG_ENABLE([debug],
              [AS_HELP_STRING([--enable-debug], [enable debug options])],
              [], [enable_debug=no])
AC_SUBST([enable_debug])
AC_ARG_VAR([RTE_SDK], [Intel DPDK source path])
if test -z "$RTE_SDK"; then
    RTE_SDK='$(abs_srcdir)/dpdk'
fi
AC_ARG_VAR([RTE_TARGET], [Intel DPDK target system])
if test -z "$RTE_TARGET"; then
    RTE_TARGET=x86_64-native-linuxapp-gcc
fi
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
