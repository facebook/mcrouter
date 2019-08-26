bin_PROGRAMS = mcpiper

mcpiper_SOURCES = \
	AnsiColorCodeStream-inl.h \
	AnsiColorCodeStream.cpp \
	AnsiColorCodeStream.h \
	ClientServerMcParser-inl.h \
	ClientServerMcParser.h \
	Color.h \
	Config.cpp \
	Config.h \
	FifoReader.cpp \
	FifoReader.h \
	main.cpp \
	McPiper.cpp \
	McPiper.h \
	MessagePrinter-inl.h \
	MessagePrinter.cpp \
	MessagePrinter.h \
	PrettyFormat.h \
	StyleAwareStream.h \
	SnifferParser-inl.h \
	SnifferParser.h \
	StyleAwareStream-inl.h \
	StyleAwareStream.h \
	StyledString.cpp \
	StyledString.h \
	Util.cpp \
	Util.h \
	ValueFormatter.h

mcpiper_LDADD = \
	$(top_srcdir)/lib/libmcrouter.a \
	-lprotocol \
	-lthriftcpp2 \
	-lthriftprotocol \
	-ltransport \
	-lthrift-core \
	-lfizz \
	-lfmt \
	-lwangle \
	-lfolly

mcpiper_CPPFLAGS = -I$(top_srcdir)/..
