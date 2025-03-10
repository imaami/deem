all: deem.so

override SRCS := deem.c utf8.c
override OBJS := $(SRCS:%=%.o)

$(if $(strip $(filter-out clean,$(MAKECMDGOALS)) \
     $(wildcard deem.so)),$(eval -load deem.so))
deem.so: $(OBJS)
	@+$(CC) $(CFLAGS) -std=gnu23 -flto=auto -fPIC -shared -o $@ $^

%.c.o: %.c
	$(msg CC,$<)
	@+$(CC) $(CFLAGS) -std=gnu23 -flto=auto -fPIC -c -o $@ $<

.PHONY: all clean
clean:; @rm -f deem.so $(OBJS)
