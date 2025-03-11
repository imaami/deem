#TARGETS := test

# Show only main targets in tab completion.
ifeq (.DEFAULT,$(MAKECMDGOALS))
-load deem.so
deem.so:; +$(MAKE) -f $(THIS_DIR)deem.mk
endif

# Prevent tab-completion and direct build of sub-targets.
ifneq (,$(filter $(PUBLISH),$(or $(MAKECMDGOALS),all)))
test:
	@echo "Hello, world!"
endif
