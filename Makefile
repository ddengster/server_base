# SUBDIRS := alt_program sample_server sample_http auth_server # all projects
SUBDIRS := auth_server # dev only

.PHONY: all clean $(SUBDIRS) $(SUBDIRS:%=%-clean)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(SUBDIRS:%=%-clean)

$(SUBDIRS:%=%-clean):
	$(MAKE) -C $(@:-clean=) clean
