SUBDIRS := alt_program sample_server sample_http

.PHONY: all clean $(SUBDIRS) $(SUBDIRS:%=%-clean)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(SUBDIRS:%=%-clean)

$(SUBDIRS:%=%-clean):
	$(MAKE) -C $(@:-clean=) clean
