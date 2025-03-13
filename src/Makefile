override THIS_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

-load $(THIS_DIR)deem.so

$(library hello.so, \
  hello.c           \
)

ifeq (.DEFAULT,$(MAKECMDGOALS))
# This is a hack to build deem.so on tab-completion
# without having it show up in the list of targets.
$(if $(wildcard $(THIS_DIR)deem.so),,$(shell \
  $(MAKE) -sf $(THIS_DIR)deem.mk deem.so >/dev/null >&2))
else
.PHONY: deem.so
deem.so: $(THIS_DIR)deem.so; @:
$(THIS_DIR)deem.so:; @+$(MAKE) -f $(@:.so=.mk) $(@F)
endif
