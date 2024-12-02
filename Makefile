restricted-ip-forwarder: restricted-ip-forwarder.c
	gcc48 -o restricted-ip-forwarder restricted-ip-forwarder.c -g -pthread

linux: restricted-ip-forwarder.c
	gcc -o restricted-ip-forwarder restricted-ip-forwarder.c -g -lpthread
