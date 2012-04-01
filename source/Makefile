# -*- mode: makefile; -*-
#
# Copyright (c) 2012, Anton Staaf
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the project nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

depth			:= ..
products		:= swddude swdprobe swddump swdhost

swddude[type]		:= program
swddude[cpp_files]	:= swd_dp.cpp swd_mpsse.cpp target.cpp swddude.cpp
swddude[cpp_files]	+= mpsse_config.cpp mpsse.cpp
swddude[libs]		:= error:error
swddude[libs]		+= log:log
swddude[libs]		+= files:files
swddude[libs]		+= command_line:command_line
swddude[libs]		+= system/ftdi:ftdi

swddump[type]		:= program
swddump[cpp_files]	:= swd_dp.cpp swd_mpsse.cpp target.cpp swddump.cpp
swddump[cpp_files]	+= mpsse_config.cpp mpsse.cpp
swddump[libs]		:= error:error
swddump[libs]		+= log:log
swddump[libs]		+= command_line:command_line
swddump[libs]		+= system/ftdi:ftdi

swdprobe[type]		:= program
swdprobe[cpp_files]	:= swd_dp.cpp swd_mpsse.cpp target.cpp swdprobe.cpp
swdprobe[cpp_files]	+= mpsse_config.cpp mpsse.cpp
swdprobe[libs]		:= error:error
swdprobe[libs]		+= log:log
swdprobe[libs]		+= files:files
swdprobe[libs]		+= command_line:command_line
swdprobe[libs]		+= system/ftdi:ftdi

swdhost[type]		:= program
swdhost[cpp_files]	:= swd_dp.cpp swd_mpsse.cpp target.cpp swdhost.cpp
swdhost[cpp_files]	+= mpsse_config.cpp mpsse.cpp
swdhost[libs]		:= error:error
swdhost[libs]		+= log:log
swdhost[libs]		+= files:files
swdhost[libs]		+= command_line:command_line
swdhost[libs]		+= system/ftdi:ftdi

include $(depth)/build/Makefile.rules
