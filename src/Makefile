.PHONY: all 1st 2nd 3rd 4th 5th 6th 7th 8th 9th 10th
all :  1st; @echo "[ 0] $(shell date +%S)"

1st :  2nd; @echo "[ 1] $(now1)"; sleep 1
2nd :  3rd; @echo "[ 2] $(now2)"; sleep 1
3rd :  4th; @echo "[ 3] $(now3)"; sleep 1
4th :  5th; @echo "[ 4] $(now1)"; sleep 1
5th :  6th; @echo "[ 5] $(now2)"; sleep 1
6th :  7th; @echo "[ 6] $(now3)"; sleep 1
7th :  8th; @echo "[ 7] $(now1)"; sleep 1
8th :  9th; @echo "[ 8] $(now2)"; sleep 1
9th : 10th; @echo "[ 9] $(now3)"; sleep 1
10th:     ; @echo "[10] $(now1)"; sleep 1

override SRCS := deem.c utf8.c
override OBJS := $(SRCS:%=%.o)

-load deem.so
deem.so: $(OBJS)
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $^

%.c.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(lazy now1,$(shell date +%S; echo lazy1 >&2))
$(lazy now2,$(shell date +%S; echo lazy2 >&2))
$(lazy now3,$(shell date +%S; echo lazy3 >&2))
