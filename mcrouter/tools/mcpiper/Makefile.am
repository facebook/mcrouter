bin_PROGRAMS = mcpiper

mcpiper_SOURCES = \
	AnsiColorCodeStream-inl.h \
	AnsiColorCodeStream.cpp \
	AnsiColorCodeStream.h \
	ClientServerMcParser-inl.h \
	ClientServerMcParser.cpp \
	ClientServerMcParser.h \
	Color.h \
	Config.cpp \
	Config.h \
	FifoReader.cpp \
	FifoReader.h \
	mcpiper.cpp \
	ParserMap.cpp \
	ParserMap.h \
	PrettyFormat.h \
	StyleAwareStream-inl.h \
	StyleAwareStream.h \
	StyledString.cpp \
	StyledString.h \
	Util.cpp \
	Util.h \
	ValueFormatter.h 

mcpiper_LDADD = $(top_srcdir)/lib/libmcrouter.a
mcpiper_CPPFLAGS = -I$(top_srcdir)/..
