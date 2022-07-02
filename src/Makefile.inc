CPPFLAGS:= \
	-g3 \
	-Ofast \
	-I $(TOPDIR)/src/include \
	-I $(TOPDIR)/switch$(HOPROTO)/include \
	-L $(TOPDIR)/switch$(HOPROTO)/lib \
	-Wall \
	-Wno-write-strings \
	-Wno-int-to-pointer-cast \
        -DNO_SSL_COMPATIBLE_INTERFACE
