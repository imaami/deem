.PHONY: all
all:
	@echo "argc: $(argc )"

-load deem.so
deem.so: deem.c
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $<
