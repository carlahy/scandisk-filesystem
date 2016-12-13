CFLAGS = -g -Wall
ALL:	dos_ls dos_cp dos_scandisk
dos_ls:	dos_ls.o dos.o
	$(CC) $(CFLAGS) -o dos_ls dos_ls.c dos.c

dos_cp:	dos_cp.o dos.o
	$(CC) $(CFLAGS) -o dos_cp dos_cp.c dos.c

dos_scandisk: dos_scandisk.o dos.o
	$(CC) $(CFLAGS) -o dos_scandisk dos_scandisk.c dos.c
