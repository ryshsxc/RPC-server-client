CC=cc
RPC_SYSTEM=rpc.o

.PHONY: format all

all: $(RPC_SYSTEM) 
$(RPC_SYSTEM): rpc.c rpc.h
	$(CC) -Wall -c -o $@ $< -g

clean:
	rm -f $(RPC_SYSTEM) $(RPC_SERVER) $(RPC_CLINT)
# RPC_SYSTEM_A=rpc.a
# $(RPC_SYSTEM_A): rpc.o
# 	ar rcs $(RPC_SYSTEM_A) $(RPC_SYSTEM)

format:
	clang-format -style=file -i *.c *.h
