SUBDIRS := alt_program sample_server sample_http auth_server prometheus_test # all projects
# SUBDIRS := sample_http # dev only

.PHONY: all clean $(SUBDIRS) $(SUBDIRS:%=%-clean)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(SUBDIRS:%=%-clean)

$(SUBDIRS:%=%-clean):
	$(MAKE) -C $(@:-clean=) clean