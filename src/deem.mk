# Prevent tab-completion and direct build of sub-targets.
ifneq (,$(filter clean-deem.so deem.so,$(MAKECMDGOALS)))

override THIS_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

override SRC_deem.so := deem.c utf8.c
override OBJ_deem.so := $(SRC_deem.so:%=%.o-fpic)
override DEP_deem.so := $(SRC_deem.so:%=%.d)

override CFLAGS_deem.so := -std=gnu23 -flto=auto -fPIC

deem.so: $(THIS_DIR)deem.so

$(THIS_DIR)deem.so: $(OBJ_deem.so:%=$(THIS_DIR)%)
	@+$(CC) $(CFLAGS) $(CFLAGS_deem.so) -shared -o $@ -MMD $^

%.c.o-fpic: %.c
	@+$(CC) $(CFLAGS) $(CFLAGS_deem.so) -o $@ -c -MMD $<

clean-deem.so:
	@$(RM) $(@:clean-%=$(THIS_DIR)%) $(OBJ_$(@:clean-%=%):%=$(THIS_DIR)%)

.PHONY: deem.so clean-deem.so

-include $(DEP_deem.so:%=$(THIS_DIR)%)
endif
