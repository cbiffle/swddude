depth			:= ../..
products		:= swddude

swddude[type]		:= program
swddude[cpp_files]	:= swddude.cpp
swddude[libs]		:= error:error
swddude[libs]		+= log:log
swddude[libs]		+= command_line:command_line
swddude[libs]		+= system/ftdi:ftdi

include $(depth)/build/Makefile.rules
